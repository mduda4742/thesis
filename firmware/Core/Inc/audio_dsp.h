#ifndef AUDIO_DSP_H
#define AUDIO_DSP_H

#include <stdint.h>
#include "stm32g4xx_hal.h"

#define PCM_SAMPLES_PER_MS  16          

/* PDM2PCM filter init */
void Audio_DSP_Init(void);

/* Process 1ms PDM frame to PCM */
void Audio_DSP_ProcessFrame(uint8_t *pdm_in, int16_t *pcm_out);

#endif /* AUDIO_DSP_H */