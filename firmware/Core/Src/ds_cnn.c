/*
 * Copyright (C) 2018 Arm Limited or its affiliates. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Przerobione z ds_cnn.cpp (klasa C++ DS_CNN : public NN) na czysty C:
 *  - wagi: byly skladowymi klasy (DS_CNN::conv1_wt ...) -> teraz pliko-statyczne,
 *  - bufor roboczy: byl 'new q7_t[...]' w konstruktorze -> teraz statyczna tablica,
 *  - run_nn() -> funkcja ds_cnn_run(); CIALO (wywolania arm_*) bez zmian,
 *  - klasa bazowa NN (MFCC/framing) usunieta - nie uzywamy jej.
 */
#include "cmsis_nn_compat.h"   // __SXTB16_RORn: read_and_pad w naglowkach NN
#include "ds_cnn.h"            // arm_math.h (q7_t) - tu dsp/utils.h definiuje MAX/MIN
#include "ds_cnn_weights.h"    // wartosci wag (CONV1_WT ...)
// CMSIS-DSP (dsp/utils.h) i CMSIS-NN definiuja MAX/MIN identycznie. Undef przed naglowkami
// NN kasuje nieszkodliwe ostrzezenie "redefined" (NN zdefiniuje je ponizej od zera).
#undef MAX
#undef MIN
#include "arm_nnfunctions.h"   // arm_convolve_*, arm_relu_q7, arm_fully_connected_q7

void arm_avepool_q7_HWC_nonsquare (
        const q7_t * Im_in,         
        const uint16_t dim_im_in_x,   
        const uint16_t dim_im_in_y,   
        const uint16_t ch_im_in,    
        const uint16_t dim_kernel_x,  
        const uint16_t dim_kernel_y,  
        const uint16_t padding_x,     
        const uint16_t padding_y,     
        const uint16_t stride_x,      
        const uint16_t stride_y,      
        const uint16_t dim_im_out_x,  
        const uint16_t dim_im_out_y,  
        q7_t * bufferA,             
        q7_t * Im_out,
        const uint16_t out_lshift);

/* --- wagi/biasy: bitowo te same co w klasie, tylko jako pliko-statyczne const --- */
static const q7_t conv1_wt[CONV1_OUT_CH*CONV1_KX*CONV1_KY]        = CONV1_WT;
static const q7_t conv1_bias[CONV1_OUT_CH]                       = CONV1_BIAS;
static const q7_t conv2_ds_wt[CONV1_OUT_CH*CONV2_DS_KX*CONV2_DS_KY] = CONV2_DS_WT;
static const q7_t conv2_ds_bias[CONV1_OUT_CH]                    = CONV2_DS_BIAS;
static const q7_t conv2_pw_wt[CONV2_OUT_CH*CONV1_OUT_CH]         = CONV2_PW_WT;
static const q7_t conv2_pw_bias[CONV2_OUT_CH]                    = CONV2_PW_BIAS;
static const q7_t conv3_ds_wt[CONV2_OUT_CH*CONV3_DS_KX*CONV3_DS_KY] = CONV3_DS_WT;
static const q7_t conv3_ds_bias[CONV2_OUT_CH]                    = CONV3_DS_BIAS;
static const q7_t conv3_pw_wt[CONV3_OUT_CH*CONV2_OUT_CH]         = CONV3_PW_WT;
static const q7_t conv3_pw_bias[CONV3_OUT_CH]                    = CONV3_PW_BIAS;
static const q7_t conv4_ds_wt[CONV3_OUT_CH*CONV4_DS_KX*CONV4_DS_KY] = CONV4_DS_WT;
static const q7_t conv4_ds_bias[CONV3_OUT_CH]                    = CONV4_DS_BIAS;
static const q7_t conv4_pw_wt[CONV4_OUT_CH*CONV3_OUT_CH]         = CONV4_PW_WT;
static const q7_t conv4_pw_bias[CONV4_OUT_CH]                    = CONV4_PW_BIAS;
static const q7_t conv5_ds_wt[CONV4_OUT_CH*CONV5_DS_KX*CONV5_DS_KY] = CONV5_DS_WT;
static const q7_t conv5_ds_bias[CONV4_OUT_CH]                    = CONV5_DS_BIAS;
static const q7_t conv5_pw_wt[CONV5_OUT_CH*CONV4_OUT_CH]         = CONV5_PW_WT;
static const q7_t conv5_pw_bias[CONV5_OUT_CH]                    = CONV5_PW_BIAS;
static const q7_t final_fc_wt[CONV5_OUT_CH*OUT_DIM]              = FINAL_FC_WT;
static const q7_t final_fc_bias[OUT_DIM]                         = FINAL_FC_BIAS;

/* --- bufor roboczy: bylo new q7_t[SCRATCH_BUFFER_SIZE] + wskazniki w konstruktorze.
 *     Teraz statycznie; wskazniki to stale offsety w scratch_pad (jak w oryginale). --- */
static q7_t scratch_pad[SCRATCH_BUFFER_SIZE];
static q7_t *const buffer1    = scratch_pad;
static q7_t *const buffer2    = scratch_pad + (CONV1_OUT_CH*CONV1_OUT_X*CONV1_OUT_Y);
static q7_t *const col_buffer = scratch_pad + (CONV1_OUT_CH*CONV1_OUT_X*CONV1_OUT_Y)
                                            + (CONV2_OUT_CH*CONV2_OUT_X*CONV2_OUT_Y);

void ds_cnn_run(q7_t *in_data, q7_t *out_data)
{
  //CONV1 : regular convolution
  arm_convolve_HWC_q7_basic_nonsquare(in_data, CONV1_IN_X, CONV1_IN_Y, 1, conv1_wt, CONV1_OUT_CH, CONV1_KX, CONV1_KY, CONV1_PX, CONV1_PY, CONV1_SX, CONV1_SY, conv1_bias, CONV1_BIAS_LSHIFT, CONV1_OUT_RSHIFT, buffer1, CONV1_OUT_X, CONV1_OUT_Y, (q15_t*)col_buffer, NULL);
  arm_relu_q7(buffer1,CONV1_OUT_X*CONV1_OUT_Y*CONV1_OUT_CH);

  //CONV2 : DS + PW conv
  arm_depthwise_separable_conv_HWC_q7_nonsquare(buffer1,CONV2_IN_X,CONV2_IN_Y,CONV1_OUT_CH,conv2_ds_wt,CONV1_OUT_CH,CONV2_DS_KX,CONV2_DS_KY,CONV2_DS_PX,CONV2_DS_PY,CONV2_DS_SX,CONV2_DS_SY,conv2_ds_bias,CONV2_DS_BIAS_LSHIFT,CONV2_DS_OUT_RSHIFT,buffer2,CONV2_OUT_X,CONV2_OUT_Y,(q15_t*)col_buffer, NULL);
  arm_relu_q7(buffer2,CONV2_OUT_X*CONV2_OUT_Y*CONV2_OUT_CH);
  arm_convolve_1x1_HWC_q7_fast_nonsquare(buffer2, CONV2_OUT_X, CONV2_OUT_Y, CONV1_OUT_CH, conv2_pw_wt, CONV2_OUT_CH, 1, 1, 0, 0, 1, 1, conv2_pw_bias, CONV2_PW_BIAS_LSHIFT, CONV2_PW_OUT_RSHIFT, buffer1, CONV2_OUT_X, CONV2_OUT_Y, (q15_t*)col_buffer, NULL);
  arm_relu_q7(buffer1,CONV2_OUT_X*CONV2_OUT_Y*CONV2_OUT_CH);

  //CONV3 : DS + PW conv
  arm_depthwise_separable_conv_HWC_q7_nonsquare(buffer1,CONV3_IN_X,CONV3_IN_Y,CONV2_OUT_CH,conv3_ds_wt,CONV2_OUT_CH,CONV3_DS_KX,CONV3_DS_KY,CONV3_DS_PX,CONV3_DS_PY,CONV3_DS_SX,CONV3_DS_SY,conv3_ds_bias,CONV3_DS_BIAS_LSHIFT,CONV3_DS_OUT_RSHIFT,buffer2,CONV3_OUT_X,CONV3_OUT_Y,(q15_t*)col_buffer, NULL);
  arm_relu_q7(buffer2,CONV3_OUT_X*CONV3_OUT_Y*CONV3_OUT_CH);
  arm_convolve_1x1_HWC_q7_fast_nonsquare(buffer2, CONV3_OUT_X, CONV3_OUT_Y, CONV2_OUT_CH, conv3_pw_wt, CONV3_OUT_CH, 1, 1, 0, 0, 1, 1, conv3_pw_bias, CONV3_PW_BIAS_LSHIFT, CONV3_PW_OUT_RSHIFT, buffer1, CONV3_OUT_X, CONV3_OUT_Y, (q15_t*)col_buffer, NULL);
  arm_relu_q7(buffer1,CONV3_OUT_X*CONV3_OUT_Y*CONV3_OUT_CH);

  //CONV4 : DS + PW conv
  arm_depthwise_separable_conv_HWC_q7_nonsquare(buffer1,CONV4_IN_X,CONV4_IN_Y,CONV3_OUT_CH,conv4_ds_wt,CONV3_OUT_CH,CONV4_DS_KX,CONV4_DS_KY,CONV4_DS_PX,CONV4_DS_PY,CONV4_DS_SX,CONV4_DS_SY,conv4_ds_bias,CONV4_DS_BIAS_LSHIFT,CONV4_DS_OUT_RSHIFT,buffer2,CONV4_OUT_X,CONV4_OUT_Y,(q15_t*)col_buffer, NULL);
  arm_relu_q7(buffer2,CONV4_OUT_X*CONV4_OUT_Y*CONV4_OUT_CH);
  arm_convolve_1x1_HWC_q7_fast_nonsquare(buffer2, CONV4_OUT_X, CONV4_OUT_Y, CONV3_OUT_CH, conv4_pw_wt, CONV4_OUT_CH, 1, 1, 0, 0, 1, 1, conv4_pw_bias, CONV4_PW_BIAS_LSHIFT, CONV4_PW_OUT_RSHIFT, buffer1, CONV4_OUT_X, CONV4_OUT_Y, (q15_t*)col_buffer, NULL);
  arm_relu_q7(buffer1,CONV4_OUT_X*CONV4_OUT_Y*CONV4_OUT_CH);

  //CONV5 : DS + PW conv
  arm_depthwise_separable_conv_HWC_q7_nonsquare(buffer1,CONV5_IN_X,CONV5_IN_Y,CONV4_OUT_CH,conv5_ds_wt,CONV4_OUT_CH,CONV5_DS_KX,CONV5_DS_KY,CONV5_DS_PX,CONV5_DS_PY,CONV5_DS_SX,CONV5_DS_SY,conv5_ds_bias,CONV5_DS_BIAS_LSHIFT,CONV5_DS_OUT_RSHIFT,buffer2,CONV5_OUT_X,CONV5_OUT_Y,(q15_t*)col_buffer, NULL);
  arm_relu_q7(buffer2,CONV5_OUT_X*CONV5_OUT_Y*CONV5_OUT_CH);
  arm_convolve_1x1_HWC_q7_fast_nonsquare(buffer2, CONV5_OUT_X, CONV5_OUT_Y, CONV4_OUT_CH, conv5_pw_wt, CONV5_OUT_CH, 1, 1, 0, 0, 1, 1, conv5_pw_bias, CONV5_PW_BIAS_LSHIFT, CONV5_PW_OUT_RSHIFT, buffer1, CONV5_OUT_X, CONV5_OUT_Y, (q15_t*)col_buffer, NULL);
  arm_relu_q7(buffer1,CONV5_OUT_X*CONV5_OUT_Y*CONV5_OUT_CH);

  //Average pool
  arm_avepool_q7_HWC_nonsquare (buffer1,CONV5_OUT_X,CONV5_OUT_Y,CONV5_OUT_CH,CONV5_OUT_X,CONV5_OUT_Y,0,0,1,1,1,1,NULL,buffer2, AVEPOOL_OUT_LSHIFT);

  arm_fully_connected_q7(buffer2, final_fc_wt, CONV5_OUT_CH, OUT_DIM, FINAL_FC_BIAS_LSHIFT, FINAL_FC_OUT_RSHIFT, final_fc_bias, out_data, (q15_t*)col_buffer);
}
