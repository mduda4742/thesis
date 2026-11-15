#include "audio_dsp.h"
#include "pdm2pcm_glo.h"

#include "mfcc_tables.h"
#include "mfcc_selftest.h"

PDM_Filter_Handler_t PDM_FilterHandler_Left;
PDM_Filter_Handler_t PDM_FilterHandler_Right;
PDM_Filter_Config_t  PDM_FilterConfig;

static arm_mfcc_instance_f32 mfcc_inst;
static float32_t mfcc_in [MFCC_FFT_LEN];        // pSrc (overwrite by mfcc_compute)
static float32_t mfcc_tmp[2 * MFCC_FFT_LEN];    // pTmp  - 2*FFT (cmplx_mag read 2*fftLen)

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
    PDM_FilterConfig.mic_gain              = 13;
    PDM_FilterConfig.decimation_factor     = PDM_FILTER_DEC_FACTOR_64;
    
    PDM_Filter_setConfig(&PDM_FilterHandler_Left, &PDM_FilterConfig);
    PDM_Filter_setConfig(&PDM_FilterHandler_Right, &PDM_FilterConfig);

    arm_mfcc_init_1024_f32(&mfcc_inst, MFCC_N_MEL, MFCC_N_DCT,
                          mfcc_dct_coefs, mfcc_filter_pos, mfcc_filter_len,
                          mfcc_filter_coefs, mfcc_window_coefs);
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
