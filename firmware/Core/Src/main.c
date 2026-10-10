/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "dma.h"
#include "hrtim.h"
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "sai.h"
#include "pdm2pcm_glo.h"
#include "arm_math.h"
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

COM_InitTypeDef BspCOMInit;

/* USER CODE BEGIN PV */

float32_t mfcc_test_variable = 3.14f;

#define PDM_BUFFER_SIZE 256

#define PCM_SAMPLES_PER_MS 16
#define PCM_HALF_BUFFER (PCM_SAMPLES_PER_MS * 2)

#define PCM_1_SEC_SIZE 16000

PDM_Filter_Handler_t PDM_FilterHandler_Left;
PDM_Filter_Handler_t PDM_FilterHandler_Right;
PDM_Filter_Config_t PDM_FilterConfig;

uint16_t pdm_rx_buffer[PDM_BUFFER_SIZE];

int16_t pcm_buffer[PCM_1_SEC_SIZE];

arm_biquad_casd_df1_inst_f32 hpf_instance;
float32_t hpf_state[4]; // 4 stany pamięci dla jednego kaskadowego bloku IIR

// Wyliczone współczynniki Butterworth HPF (Fs=16000, Fc=80)
// CMSIS-DSP wymaga specyficznego formatu znaków: {b0, b1, b2, -a1, -a2}
float32_t hpf_coeffs[5] = {0.9780f, -1.9561f, 0.9780f, 1.9556f, -0.9565f};

volatile uint8_t half_buffer_ready = 0;
volatile uint8_t full_buffer_ready = 0;

volatile uint8_t recording_complete = 0;
volatile uint32_t pcm_buffer_index = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void Audio_Init(void) 
{
    // UWAGA: Biblioteka PDM_Filter zablokuje się (HardFault lub brak działania), 
    // jeśli sprzętowy moduł CRC nie zostanie wcześniej uruchomiony!
    __HAL_RCC_CRC_CLK_ENABLE();

    // -- Konfiguracja LEWEGO mikrofonu --
    PDM_FilterHandler_Left.bit_order        = PDM_FILTER_BIT_ORDER_MSB; // Zależy od mikrofonu
    PDM_FilterHandler_Left.endianness       = PDM_FILTER_ENDIANNESS_LE; // Little Endian dla STM32
    PDM_FilterHandler_Left.high_pass_tap    = 2122358088; // Współczynnik usuwania DC Offset (ok 0.99)
    PDM_FilterHandler_Left.in_ptr_channels  = 2; // Skaczemy co 2 bajty w buforze wejściowym
    PDM_FilterHandler_Left.out_ptr_channels = 1; // Zostawiamy dziurę na drugi kanał w buforze wyj.
    PDM_Filter_Init(&PDM_FilterHandler_Left);

    // -- Konfiguracja PRAWEGO mikrofonu --
    PDM_FilterHandler_Right.bit_order        = PDM_FILTER_BIT_ORDER_MSB;
    PDM_FilterHandler_Right.endianness       = PDM_FILTER_ENDIANNESS_LE;
    PDM_FilterHandler_Right.high_pass_tap    = 2122358088;
    PDM_FilterHandler_Right.in_ptr_channels  = 2;
    PDM_FilterHandler_Right.out_ptr_channels = 1;
    PDM_Filter_Init(&PDM_FilterHandler_Right);

    // -- Konfiguracja wspólna (Filtru) --
    PDM_FilterConfig.output_samples_number = PCM_SAMPLES_PER_MS; // 16 próbek dla jednego kanału!
    PDM_FilterConfig.mic_gain              = 15;   // Wzmocnienie (kalibruj eksperymentalnie)
    PDM_FilterConfig.decimation_factor     = PDM_FILTER_DEC_FACTOR_64;
    
    PDM_Filter_setConfig(&PDM_FilterHandler_Left, &PDM_FilterConfig);
    PDM_Filter_setConfig(&PDM_FilterHandler_Right, &PDM_FilterConfig);

    arm_biquad_cascade_df1_init_f32(&hpf_instance, 1, hpf_coeffs, hpf_state);
}

void Process_PDM_Data(uint16_t *pdm_data_chunk) 
{
    if (recording_complete) return; // Jeśli mamy sekundę, zignoruj resztę

    // 1. Oszukujemy kompilator: rzutujemy paczkę 16-bit na wskaźnik 8-bitowy (bajtowy)
    uint8_t *in_byte = (uint8_t *)pdm_data_chunk;
    
    // 2. Obliczamy aktualne miejsce zapisu w dużym buforze PCM
    int16_t temp_left[PCM_SAMPLES_PER_MS];
    int16_t temp_right[PCM_SAMPLES_PER_MS];
    float32_t temp_f32[PCM_SAMPLES_PER_MS]; // Nasz nowy bufor roboczy FPU

    // 3. Konwersja PRAWEGO kanału
    // in_byte[0] to LSB z 16-bitowego słowa
    // pcm_out[1] zapisuje wynik w prawej "szczelinie" stereo
    PDM_Filter(
        (void*)&in_byte[0], 
        (void*)&temp_right[0], 
        &PDM_FilterHandler_Right
    );

    // 4. Konwersja LEWEGO kanału
    // in_byte[1] to MSB z 16-bitowego słowa
    // pcm_out[0] zapisuje wynik w lewej "szczelinie" stereo
    PDM_Filter(
        (void*)&in_byte[1], 
        (void*)&temp_left[0], 
        &PDM_FilterHandler_Left
    );

    // 5. Konwersja do float32_t
    // KROK A: Downmix (Mono) i konwersja do float32_t
    for (int i = 0; i < PCM_SAMPLES_PER_MS; i++) 
    {
        temp_f32[i] = (float32_t)( ((int32_t)temp_left[i] + (int32_t)temp_right[i]) / 2 );
    }

    // KROK B: Przepuszczenie paczki 16 próbek przez potężny filtr IIR Biquad
    arm_biquad_cascade_df1_f32(&hpf_instance, temp_f32, temp_f32, PCM_SAMPLES_PER_MS);

    // KROK C: Powrót z floatów na 16-bitowe liczby całkowite do głównego bufora
    for (int i = 0; i < PCM_SAMPLES_PER_MS; i++) 
    {
        pcm_buffer[pcm_buffer_index + i] = (int16_t)temp_f32[i];
    }

    // 5. Przesuwamy wskaźnik o 32 nowo dodane próbki (16 lewych + 16 prawych)
    pcm_buffer_index += PCM_SAMPLES_PER_MS;
    
    // 6. Sprawdzamy czy nagraliśmy już całą sekundę
    if(pcm_buffer_index >= PCM_1_SEC_SIZE) {
        recording_complete = 1;
        HAL_SAI_DMAStop(&hsai_BlockA1); // Zatrzymujemy DMA
    }
}

void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai) 
{
  half_buffer_ready = 1;
}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai) 
{
  full_buffer_ready = 1;
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    // Upewniamy się, że to przerwanie pochodzi od Timera 6
    if (htim->Instance == TIM6) 
    {
        // Sprawdzamy naszą flagę
        if (recording_complete == 0) 
        {
            // Migaj diodą - funkcja Toggle zmienia stan na przeciwny
            // UWAGA: Zmień LED_GPIO_Port i LED_Pin na etykiety, 
            // które nadałeś swojej diodzie w CubeMX (np. GPIOA, GPIO_PIN_5)
            HAL_GPIO_TogglePin(LED2_GPIO_PORT, LED2_PIN);
        }
        else 
        {
            // Jeśli zatrzymano nagrywanie, upewnij się, że dioda zgasła
            // (zakładając, że RESET gasi diodę na Twojej płytce)
            HAL_GPIO_WritePin(LED2_GPIO_PORT, LED2_PIN, GPIO_PIN_RESET);
        }
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_HRTIM1_Init();
  MX_TIM6_Init();
  /* USER CODE BEGIN 2 */

  HAL_HRTIM_WaveformOutputStart(&hhrtim1, HRTIM_OUTPUT_TA1);  // Enable the generation of the waveform signal on the designated output
  HAL_HRTIM_WaveformCounterStart(&hhrtim1, HRTIM_TIMERID_TIMER_A);  // Start the counter of the Timer A operating in waveform mode

  MY_SAI1_Init();  // Initialize the SAI1 peripheral with custom settings

  Audio_Init();

  HAL_TIM_Base_Start_IT(&htim6);

  HAL_SAI_Receive_DMA(&hsai_BlockA1, (uint8_t *)pdm_rx_buffer, PDM_BUFFER_SIZE);

  /* USER CODE END 2 */

  /* Initialize led */
  BSP_LED_Init(LED_GREEN);

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  /* Initialize COM1 port (115200, 8 bits (7-bit data + 1 stop bit), no parity */
  BspCOMInit.BaudRate   = 115200;
  BspCOMInit.WordLength = COM_WORDLENGTH_8B;
  BspCOMInit.StopBits   = COM_STOPBITS_1;
  BspCOMInit.Parity     = COM_PARITY_NONE;
  BspCOMInit.HwFlowCtl  = COM_HWCONTROL_NONE;
  if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE)
  {
    Error_Handler();
  }

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    if (half_buffer_ready) {
      half_buffer_ready = 0;
      Process_PDM_Data(&pdm_rx_buffer[0]);
    }

    if (full_buffer_ready) {
      full_buffer_ready = 0;
      Process_PDM_Data(&pdm_rx_buffer[PDM_BUFFER_SIZE / 2]);
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV3;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV8;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
