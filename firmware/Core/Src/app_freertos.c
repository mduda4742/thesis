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
#define PCM_1_SEC_SIZE 16000

uint16_t pdm_rx_buffer[PDM_BUFFER_SIZE];
int16_t pcm_buffer[PCM_1_SEC_SIZE];

volatile uint32_t pcm_buffer_index = 0;
volatile uint8_t recording_complete = 0;

extern SAI_HandleTypeDef hsai_BlockA1;

osThreadId_t dspTaskHandle;
const osThreadAttr_t dspTask_attributes = {
  .name = "dspTask",
  .priority = (osPriority_t) osPriorityHigh,
  .stack_size = 4096
};

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 128 * 4
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

void StartDspTask(void *argument);

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
  Audio_DSP_Init();

  HAL_SAI_Receive_DMA(&hsai_BlockA1, (uint8_t *)pdm_rx_buffer, PDM_BUFFER_SIZE);

  for(;;)
  {
    uint32_t flags = osThreadFlagsWait(FLAG_HALF_CPLT | FLAG_FULL_CPLT, osFlagsWaitAny, osWaitForever);

    if (!(flags & (FLAG_HALF_CPLT | FLAG_FULL_CPLT))) continue;
    if (recording_complete) continue;

    uint8_t *pdm_chunk_ptr = NULL;

    if (flags & FLAG_HALF_CPLT) {
      pdm_chunk_ptr = (uint8_t *)&pdm_rx_buffer[0];
    } 
    else if (flags & FLAG_FULL_CPLT) {
      pdm_chunk_ptr = (uint8_t *)&pdm_rx_buffer[128];
    }

    if (pdm_chunk_ptr != NULL) 
    {
      Audio_DSP_ProcessFrame(pdm_chunk_ptr, &pcm_buffer[pcm_buffer_index]);

      pcm_buffer_index += PCM_SAMPLES_PER_MS;

      if (pcm_buffer_index >= PCM_1_SEC_SIZE)
      {
        recording_complete = 1;
        HAL_SAI_DMAStop(&hsai_BlockA1);
      }
    }
  }
}

/* USER CODE END Application */

