#ifndef AUDIO_DSP_H
#define AUDIO_DSP_H

#include <stdint.h>
#include "stm32g4xx_hal.h"
#include "arm_math.h"   

#define PCM_SAMPLES_PER_MS  16          

/* PDM2PCM filter init */
void Audio_DSP_Init(void);

/* Process 1ms PDM frame to PCM */
void Audio_DSP_ProcessFrame(uint8_t *pdm_in, int16_t *pcm_out);

/* Compute MFCC features from a PCM frame */
void mfcc_compute(const int16_t *frame, float32_t *out);

/* Run a self-test for the MFCC computation (compare to reference values)*/
void mfcc_selftest(void);

#endif /* AUDIO_DSP_H */