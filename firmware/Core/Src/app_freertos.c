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

#define PDM_BUFFER_SIZE 256
#define PCM_SAMPLES_PER_MS 16

#define WINDOW_SIZE_MS 40
#define WINDOW_SIZE (WINDOW_SIZE_MS * PCM_SAMPLES_PER_MS)

#define WINDOW_STRIDE_MS 20
#define WINDOW_STRIDE (WINDOW_STRIDE_MS * PCM_SAMPLES_PER_MS)

uint16_t pdm_rx_buffer[PDM_BUFFER_SIZE];
int16_t window[WINDOW_SIZE];

volatile uint32_t window_idx = 0;

extern SAI_HandleTypeDef hsai_BlockA1;

osThreadId_t dspTaskHandle;
const osThreadAttr_t dspTask_attributes = {
  .name = "dspTask",
  .priority = (osPriority_t) osPriorityHigh,
  .stack_size = 1024
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
static uint8_t frame_write = 0;
static uint8_t frame_count = 0;

static volatile uint32_t dbg_cyc_proc     = 0;   // ostatni ProcessFrame [cykle]
static volatile uint32_t dbg_cyc_mfcc     = 0;   // ostatni mfcc_compute [cykle]
static volatile uint32_t dbg_cyc_iter_max = 0;   // NAJGORSZA iteracja [cykle]
static volatile float    dbg_us_iter_max  = 0;   // NAJGORSZA iteracja [ms]

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

void StartDspTask(void *argument);

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

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; // wlacz jednostke trace
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk; // wlacz licznik cykli

  HAL_SAI_Receive_DMA(&hsai_BlockA1, (uint8_t *)pdm_rx_buffer, PDM_BUFFER_SIZE);

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
      uint32_t t0 = DWT->CYCCNT;
      Audio_DSP_ProcessFrame(pdm_chunk_ptr, &window[window_idx]);
      dbg_cyc_proc = DWT->CYCCNT - t0;
      
      window_idx += PCM_SAMPLES_PER_MS;

      if (window_idx >= WINDOW_SIZE)
      {
        uint32_t tm = DWT->CYCCNT;
        mfcc_compute(window, mfcc_ring[frame_write]);

        frame_write = (frame_write + 1) % MFCC_N_FRAMES;
        if (frame_count < MFCC_N_FRAMES) frame_count++; 

        memmove(window, &window[WINDOW_STRIDE], (WINDOW_SIZE - WINDOW_STRIDE) * sizeof(int16_t));
        window_idx = WINDOW_SIZE - WINDOW_STRIDE;
        dbg_cyc_mfcc = DWT->CYCCNT - tm;
      }

      uint32_t iter = DWT->CYCCNT - t0;
      if (iter > dbg_cyc_iter_max) 
      {
        dbg_cyc_iter_max = iter;
        dbg_us_iter_max = (float)iter * 1000000.0f / (float)SystemCoreClock;
      }

      // HAL_SAI_DMAStop(&hsai_BlockA1);

    }
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

