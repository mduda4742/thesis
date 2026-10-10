#include "audio_dsp.h"
#include "pdm2pcm_glo.h"

PDM_Filter_Handler_t PDM_FilterHandler_Left;
PDM_Filter_Handler_t PDM_FilterHandler_Right;
PDM_Filter_Config_t  PDM_FilterConfig;

void Audio_DSP_Init(void) 
{
    __HAL_RCC_CRC_CLK_ENABLE();

    PDM_FilterHandler_Left.bit_order        = PDM_FILTER_BIT_ORDER_MSB; 
    PDM_FilterHandler_Left.endianness       = PDM_FILTER_ENDIANNESS_LE; 
    PDM_FilterHandler_Left.high_pass_tap    = 2122358088; 
    PDM_FilterHandler_Left.in_ptr_channels  = 2; 
    PDM_FilterHandler_Left.out_ptr_channels = 1; 
    PDM_Filter_Init(&PDM_FilterHandler_Left);

    PDM_FilterHandler_Right.bit_order        = PDM_FILTER_BIT_ORDER_MSB;
    PDM_FilterHandler_Right.endianness       = PDM_FILTER_ENDIANNESS_LE;
    PDM_FilterHandler_Right.high_pass_tap    = 2122358088;
    PDM_FilterHandler_Right.in_ptr_channels  = 2;
    PDM_FilterHandler_Right.out_ptr_channels = 1;
    PDM_Filter_Init(&PDM_FilterHandler_Right);

    PDM_FilterConfig.output_samples_number = PCM_SAMPLES_PER_MS;
    PDM_FilterConfig.mic_gain              = 15;
    PDM_FilterConfig.decimation_factor     = PDM_FILTER_DEC_FACTOR_64;
    
    PDM_Filter_setConfig(&PDM_FilterHandler_Left, &PDM_FilterConfig);
    PDM_Filter_setConfig(&PDM_FilterHandler_Right, &PDM_FilterConfig);
}

void Audio_DSP_ProcessFrame(uint8_t *pdm_in, int16_t *pcm_out)
{
    int16_t temp_left[PCM_SAMPLES_PER_MS];
    int16_t temp_right[PCM_SAMPLES_PER_MS];

    PDM_Filter((void*)&pdm_in[0], (void*)&temp_left[0], &PDM_FilterHandler_Left);
    PDM_Filter((void*)&pdm_in[1], (void*)&temp_right[0], &PDM_FilterHandler_Right);

    for (int i = 0; i < PCM_SAMPLES_PER_MS; i++) 
    {
        pcm_out[i] = (int16_t)(((int32_t)temp_left[i] + (int32_t)temp_right[i]) / 2);
    }
}
