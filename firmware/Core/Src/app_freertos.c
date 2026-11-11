/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "arm_math_types.h"
#include "system_stm32g4xx.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "cmsis_os2.h"
#include "audio_dsp.h"
#include <stdint.h>
#include <string.h>

#include "mfcc_tables.h"
#include "mfcc_selftest.h"
#include "ds_cnn.h"
#include "perf.h"
#include <math.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

#define FLAG_HALF_CPLT 0x01
#define FLAG_FULL_CPLT 0x02

#define FLAG_RUN_NN 0x01

#define PDM_BUFFER_SIZE 256
#define PCM_SAMPLES_PER_MS 16

#define WINDOW_SIZE_MS 40
#define WINDOW_SIZE (WINDOW_SIZE_MS * PCM_SAMPLES_PER_MS)

#define WINDOW_STRIDE_MS 20
#define WINDOW_STRIDE (WINDOW_STRIDE_MS * PCM_SAMPLES_PER_MS)

uint16_t pdm_rx_buffer[PDM_BUFFER_SIZE];
int16_t window[WINDOW_SIZE];

volatile uint32_t window_idx = 0;

// Wskaznik wizualny (dioda przez TIM6 w main.c): 1 = napelniamy ring, 0 = NN obudzony.
volatile uint8_t recording_active = 0;

extern SAI_HandleTypeDef hsai_BlockA1;

osThreadId_t dspTaskHandle;
const osThreadAttr_t dspTask_attributes = {
  .name = "dspTask",
  .priority = (osPriority_t) osPriorityHigh,
  .stack_size = 1024
};

osThreadId_t nnTaskHandle;
const osThreadAttr_t nnTask_attributes = {
  .name = "nnTask",
  .priority = (osPriority_t) osPriorityAboveNormal,
  .stack_size = 2048
};

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 128 * 4
};

#if MFCC_FFT_LEN != 1024
#error "Uzywamy arm_mfcc_init_1024_f32 - przy innym FFT_LEN zmien init na wlasciwa dlugosc."
#endif

static arm_mfcc_instance_f32 mfcc_inst;
static float32_t mfcc_in [MFCC_FFT_LEN];        // pSrc (nadpisywany)
static float32_t mfcc_tmp[2 * MFCC_FFT_LEN];    // pTmp  - UWAGA: 2*FFT (cmplx_mag czyta 2*fftLen)

static float32_t mfcc_ring[MFCC_N_FRAMES][MFCC_N_DCT];
static volatile uint8_t frame_write = 0;
static volatile uint8_t frame_count = 0;

// --- podglad wyniku sieci w debuggerze (volatile, by -O2 nie usunelo) ---
volatile q7_t  nn_out_dbg[OUT_DIM];      // surowe logity int8
volatile float nn_prob_dbg[OUT_DIM];     // prawdopodobienstwa 0..1 (×100 = %)
volatile int   nn_best_dbg;              // indeks zwyciezcy (0=sil,1=unk,2=go,3=backward,4=left,5=right,6=stop)
volatile float nn_best_prob_dbg;         // pewnosc zwyciezcy
volatile char  nn_word_dbg[12];          // nazwa rozpoznanej klasy (np. "stop")

// Kanaly pomiaru czasu (perf.h). Srednia = sum/count, czas[us] = cykle/160.
perf_ch_t perf_pdm     = { "pdm2pcm" };   // PDM->PCM (ProcessFrame)
perf_ch_t perf_mfcc    = { "mfcc" };      // policzenie MFCC jednej ramki
perf_ch_t perf_dsploop = { "dsp_loop" };  // cala iteracja watku DSP (obrobka 1 chunka)
perf_ch_t perf_snap    = { "nn_snap" };   // kopia ringu w sekcji krytycznej
perf_ch_t perf_bridge  = { "nn_bridge" }; // most float->int8
perf_ch_t perf_run     = { "nn_run" };    // ds_cnn_run (matematyka sieci)
perf_ch_t perf_nnloop  = { "nn_loop" };   // cala iteracja watku NN

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

void StartDspTask(void *argument);

void StartNNTask(void *argument);

void mfcc_compute(const int16_t *frame, float32_t *out);

void mfcc_selftest(void);

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */

  dspTaskHandle = osThreadNew(StartDspTask, NULL, &dspTask_attributes);

  nnTaskHandle = osThreadNew(StartNNTask, NULL, &nnTask_attributes);

  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

void StartDspTask(void *argument)
{ 
  Audio_DSP_Init();  // Initialize the audio DSP processing

  arm_mfcc_init_1024_f32(&mfcc_inst, MFCC_N_MEL, MFCC_N_DCT,
                          mfcc_dct_coefs, mfcc_filter_pos, mfcc_filter_len,
                          mfcc_filter_coefs, mfcc_window_coefs);

  mfcc_selftest();

  // static q7_t nn_in[IN_DIM];
  // static q7_t nn_out[OUT_DIM];
  // ds_cnn_run(nn_in, nn_out);   // TYMCZASOWY: test linkowania sieci (wagi ARM, wejscie smieciowe)

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; // wlacz jednostke trace
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk; // wlacz licznik cykli

  HAL_SAI_Receive_DMA(&hsai_BlockA1, (uint8_t *)pdm_rx_buffer, PDM_BUFFER_SIZE);
  recording_active = 1;   // start napelniania ringu -> dioda miga

  int nn_frame_counter = 0;

  for(;;)
  {
    uint32_t flags = osThreadFlagsWait(FLAG_HALF_CPLT | FLAG_FULL_CPLT, osFlagsWaitAny, osWaitForever);

    if (!(flags & (FLAG_HALF_CPLT | FLAG_FULL_CPLT))) continue;

    uint8_t *pdm_chunk_ptr = NULL;

    if (flags & FLAG_HALF_CPLT) {
      pdm_chunk_ptr = (uint8_t *)&pdm_rx_buffer[0];
    } 
    else if (flags & FLAG_FULL_CPLT) {
      pdm_chunk_ptr = (uint8_t *)&pdm_rx_buffer[128];
    }

    if (pdm_chunk_ptr != NULL) 
    {
      PERF_BEGIN(perf_dsploop);
      PERF_BEGIN(perf_pdm);
      Audio_DSP_ProcessFrame(pdm_chunk_ptr, &window[window_idx]);
      PERF_END(perf_pdm);
      
      window_idx += PCM_SAMPLES_PER_MS;

      if (window_idx >= WINDOW_SIZE)
      {
        PERF_BEGIN(perf_mfcc);
        mfcc_compute(window, mfcc_ring[frame_write]);
        PERF_END(perf_mfcc);

        frame_write = (frame_write + 1) % MFCC_N_FRAMES;
        if (frame_count < MFCC_N_FRAMES) frame_count++; 
        else if (++nn_frame_counter >= 10) 
        {
          nn_frame_counter = 0;
          recording_active = 0;   // NN obudzony -> dioda gasnie
          osThreadFlagsSet(nnTaskHandle, FLAG_RUN_NN);
          // asm volatile ("bkpt #0");   // odkomentuj do debugowania (halt psuje pomiary)
        }

        memmove(window, &window[WINDOW_STRIDE], (WINDOW_SIZE - WINDOW_STRIDE) * sizeof(int16_t));
        window_idx = WINDOW_SIZE - WINDOW_STRIDE;
      }

      PERF_END(perf_dsploop);

      // HAL_SAI_DMAStop(&hsai_BlockA1);

    }
  }
}

void StartNNTask(void *argument) 
{

  static float32_t snap[MFCC_N_FRAMES][MFCC_N_DCT];
  static q7_t nn_in[IN_DIM];
  static q7_t nn_out[OUT_DIM]; 

  for(;;) 
  {
    osThreadFlagsWait(FLAG_RUN_NN, osFlagsWaitAny, osWaitForever);
    PERF_BEGIN(perf_nnloop);

    PERF_BEGIN(perf_snap);
    taskENTER_CRITICAL();
    uint8_t w = frame_write;
    for (int i = 0; i < MFCC_N_FRAMES; i++) 
      memcpy(snap[i], mfcc_ring[(w+i) % MFCC_N_FRAMES], sizeof(snap[i]));
    taskEXIT_CRITICAL();
    PERF_END(perf_snap);

    PERF_BEGIN(perf_bridge);
    for (int k = 0; k < IN_DIM; k++)
    {
      int v = lroundf(((float32_t*)snap)[k] * (1 << MFCC_DEC_BITS));
      nn_in[k] = (v > 127) ? 127 : ((v < -128) ? -128 : v);
    }
    PERF_END(perf_bridge);

    PERF_BEGIN(perf_run);
    ds_cnn_run(nn_in, nn_out);
    PERF_END(perf_run);

    // (4) softmax -> procenty
    int best = 0; float sum = 0.0f, logit[OUT_DIM], prob[OUT_DIM];
    for (int i = 0; i < OUT_DIM; i++) { logit[i] = nn_out[i] / 2.0f; if (nn_out[i] > nn_out[best]) best = i; }
    float mx = logit[best];
    for (int i = 0; i < OUT_DIM; i++) { prob[i] = expf(logit[i] - mx); sum += prob[i]; }
    for (int i = 0; i < OUT_DIM; i++) {
      prob[i] /= sum;
      nn_prob_dbg[i] = prob[i];      // kopiujemy do volatile -> widoczne w debuggerze
      nn_out_dbg[i]  = nn_out[i];
    }
    nn_best_dbg = best;
    nn_best_prob_dbg = prob[best];

    // indeks -> nazwa klasy (podglad w debuggerze: p nn_word_dbg)
    const char *word;
    switch (nn_best_dbg) {
      case 0:  word = "_silence_"; break;
      case 1:  word = "_unknown_"; break;
      case 2:  word = "go";        break;
      case 3:  word = "backward";  break;
      case 4:  word = "left";      break;
      case 5:  word = "right";     break;
      case 6:  word = "stop";      break;
      default: word = "???";       break;
    }
    int ci = 0;
    for (; word[ci] && ci < (int)sizeof(nn_word_dbg) - 1; ci++) nn_word_dbg[ci] = word[ci];
    nn_word_dbg[ci] = '\0';

    PERF_END(perf_nnloop);
    // asm volatile ("bkpt #1");   // odkomentuj do podgladu wyniku (halt psuje pomiary)
  }
}

void mfcc_compute(const int16_t *frame, float32_t *out)
{
  for (int i = 0; i < MFCC_FRAME_LEN; i++) 
    mfcc_in[i] = (float32_t)frame[i] / 32768.0f;
  for (int i = MFCC_FRAME_LEN; i < MFCC_FFT_LEN; i++)
    mfcc_in[i] = 0.0f;


    float32_t maxv;
    uint32_t maxidx;
    arm_absmax_f32(mfcc_in, MFCC_FFT_LEN, &maxv, &maxidx);

    arm_mfcc_f32(&mfcc_inst, mfcc_in, out, mfcc_tmp);

    if (maxv != 0.0f) 
      out[0] -= MFCC_DCT0_SUM * logf(maxv);
}

void mfcc_selftest(void)
{
  float32_t test_out[MFCC_N_DCT];

  mfcc_compute(mfcc_selftest_input, test_out);

  float maxerr = 0.0f;
  for (int i = 0; i < MFCC_N_DCT; i++) {
    float e = fabsf(test_out[i] - mfcc_selftest_expected[i]);
      if (e > maxerr) maxerr = e;
  }
    // breakpoint: test_out vs mfcc_selftest_expected, maxerr
  (void)maxerr;
}

/* USER CODE END Application */

