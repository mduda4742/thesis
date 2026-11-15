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

#include "ds_cnn.h"
#include "perf.h"
#include <math.h>

#include "usart.h"
#include "uros_app.h"
#include "kws_event.h"

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

#define KWS_CONF_THRESHOLD  0.9f    // minimal probability to accept a command (0..1)
#define KWS_REFRACTORY_MS   1000U   // silent of the same world after recognition [ms]

uint16_t pdm_rx_buffer[PDM_BUFFER_SIZE];
int16_t window[WINDOW_SIZE];

volatile uint32_t window_idx = 0;

// Wskaznik wizualny (dioda przez TIM6 w main.c): 1 = napelniamy ring, 0 = NN obudzony.
volatile uint8_t recording_active = 0;

extern SAI_HandleTypeDef hsai_BlockA1;

osMessageQueueId_t kwsEventQueueHandle;

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

osThreadId_t urosTaskHandle;
const osThreadAttr_t urosTask_attributes = {
  .name = "urosTask",
  .priority = (osPriority_t) osPriorityBelowNormal,
  .stack_size = 3000 * 4
};

static float32_t mfcc_ring[MFCC_N_FRAMES][MFCC_N_DCT];
static volatile uint8_t frame_write = 0;
static volatile uint8_t frame_count = 0;

// Debugging variables
volatile float nn_prob_dbg[OUT_DIM];     // per-class probability 0..1 (order as in kws_event.h)
volatile int   nn_best_dbg;              // index of the most probable class

// Kanaly pomiaru czasu (perf.h). Srednia = sum/count, czas[us] = cykle/160.
perf_ch_t perf_pdm     = { "pdm2pcm" };   // PDM->PCM (ProcessFrame)
perf_ch_t perf_mfcc    = { "mfcc" };      // policzenie MFCC jednej ramki
perf_ch_t perf_dsploop = { "dsp_loop" };  // cala iteracja watku DSP (obrobka 1 chunka)
perf_ch_t perf_snap    = { "nn_snap" };   // kopia ringu w sekcji krytycznej
perf_ch_t perf_bridge  = { "nn_bridge" }; // most float->int8
perf_ch_t perf_run     = { "nn_run" };    // ds_cnn_run (matematyka sieci)
perf_ch_t perf_nnloop  = { "nn_loop" };   // cala iteracja watku NN

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .priority = (osPriority_t) osPriorityIdle,
  .stack_size = 128 * 4
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

void StartDspTask(void *argument);

void StartNNTask(void *argument);


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

  kwsEventQueueHandle = osMessageQueueNew(8, sizeof(kws_event_t), NULL);

  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */

  dspTaskHandle = osThreadNew(StartDspTask, NULL, &dspTask_attributes);

  nnTaskHandle = osThreadNew(StartNNTask, NULL, &nnTask_attributes);

  urosTaskHandle = osThreadNew(StartUrosTask, (void *)&hlpuart1, &urosTask_attributes);

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

  // mfcc_selftest();

  perf_init();

  HAL_SAI_Receive_DMA(&hsai_BlockA1, (uint8_t *)pdm_rx_buffer, PDM_BUFFER_SIZE);
  recording_active = 1;

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
          recording_active = 0;   // NN wake up -> led is off
          osThreadFlagsSet(nnTaskHandle, FLAG_RUN_NN);
          // asm volatile ("bkpt #0");   // DEBUG
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

  static uint32_t kws_last_seen[OUT_DIM];
  for (int i = 0; i < OUT_DIM; i++)
    kws_last_seen[i] = osKernelGetTickCount() - KWS_REFRACTORY_MS;

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

    // softmax: nn_out is int8 with 1 fractional bit, so logit = nn_out / 2
    int best = 0;
    for (int i = 1; i < OUT_DIM; i++)
      if (nn_out[i] > nn_out[best]) best = i;

    float prob[OUT_DIM], sum = 0.0f;
    for (int i = 0; i < OUT_DIM; i++) {
      prob[i] = expf((nn_out[i] - nn_out[best]) / 2.0f);   // minus max: exp() never overflows
      sum += prob[i];
    }
    for (int i = 0; i < OUT_DIM; i++) {
      prob[i] /= sum;
      nn_prob_dbg[i] = prob[i];
    }
    nn_best_dbg = best;

    if (best >= KWS_GO && prob[best] > KWS_CONF_THRESHOLD) {
      uint32_t now = osKernelGetTickCount();
      uint32_t since_last = now - kws_last_seen[best];
      kws_last_seen[best] = now;

      if (since_last >= KWS_REFRACTORY_MS) {
        kws_event_t ev = { .cmd = (kws_cmd_t)best, .prob = prob[best], .t0 = DWT->CYCCNT };
        osMessageQueuePut(kwsEventQueueHandle, &ev, 0U, 0U);
      }
    }

    PERF_END(perf_nnloop);
    // asm volatile ("bkpt #1");   // DEBUG

  }
}

/* USER CODE END Application */

