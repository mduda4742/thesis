/*
 * Copyright (C) 2018 Arm Limited or its affiliates. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __DS_CNN_H__
#define __DS_CNN_H__

#include "arm_math.h"   // q7_t, q15_t (weights and local_NN moved to ds_cnn.c)

#define SAMP_FREQ 16000
#define MFCC_DEC_BITS 1
#define FRAME_SHIFT_MS 20
#define FRAME_SHIFT ((int16_t)(SAMP_FREQ * 0.001 * FRAME_SHIFT_MS))
#define NUM_FRAMES 49
#define NUM_MFCC_COEFFS 10
#define MFCC_BUFFER_SIZE (NUM_FRAMES*NUM_MFCC_COEFFS)
#define FRAME_LEN_MS 40
#define FRAME_LEN ((int16_t)(SAMP_FREQ * 0.001 * FRAME_LEN_MS))

#define IN_DIM (NUM_FRAMES*NUM_MFCC_COEFFS)
#define OUT_DIM 7   // _silence_, _unknown_, go, backward, left, right, stop

#define CONV1_OUT_CH 64
#define CONV1_IN_X NUM_MFCC_COEFFS
#define CONV1_IN_Y NUM_FRAMES
#define CONV1_KX 4
#define CONV1_KY 10
#define CONV1_SX 2
#define CONV1_SY 2
#define CONV1_PX 1
#define CONV1_PY 4
#define CONV1_OUT_X 5
#define CONV1_OUT_Y 25

#define CONV2_OUT_CH 64
#define CONV2_IN_X CONV1_OUT_X
#define CONV2_IN_Y CONV1_OUT_Y
#define CONV2_DS_KX 3
#define CONV2_DS_KY 3
#define CONV2_DS_SX 1
#define CONV2_DS_SY 1
#define CONV2_DS_PX 1
#define CONV2_DS_PY 1
#define CONV2_OUT_X 5
#define CONV2_OUT_Y 25

#define CONV3_OUT_CH 64
#define CONV3_IN_X CONV2_OUT_X
#define CONV3_IN_Y CONV2_OUT_Y
#define CONV3_DS_KX 3
#define CONV3_DS_KY 3
#define CONV3_DS_SX 1
#define CONV3_DS_SY 1
#define CONV3_DS_PX 1
#define CONV3_DS_PY 1
#define CONV3_OUT_X CONV3_IN_X
#define CONV3_OUT_Y CONV3_IN_Y

#define CONV4_OUT_CH 64
#define CONV4_IN_X CONV3_OUT_X
#define CONV4_IN_Y CONV3_OUT_Y
#define CONV4_DS_KX 3
#define CONV4_DS_KY 3
#define CONV4_DS_SX 1
#define CONV4_DS_SY 1
#define CONV4_DS_PX 1
#define CONV4_DS_PY 1
#define CONV4_OUT_X CONV4_IN_X
#define CONV4_OUT_Y CONV4_IN_Y

#define CONV5_OUT_CH 64
#define CONV5_IN_X CONV4_OUT_X
#define CONV5_IN_Y CONV4_OUT_Y
#define CONV5_DS_KX 3
#define CONV5_DS_KY 3
#define CONV5_DS_SX 1
#define CONV5_DS_SY 1
#define CONV5_DS_PX 1
#define CONV5_DS_PY 1
#define CONV5_OUT_X CONV5_IN_X
#define CONV5_OUT_Y CONV5_IN_Y

#define SCRATCH_BUFFER_SIZE (2*2*CONV1_OUT_CH*CONV2_DS_KX*CONV2_DS_KY + 2*CONV2_OUT_CH*CONV2_OUT_X*CONV2_OUT_Y)
// max of im2col: (2*2*64*3*3)=2304 + max consecutive layer activations: (64x25x5 + 64x25x5)=16000

// Was: class DS_CNN : public NN { run_nn(...), weights as members }.
// Now plain C: one function; weights and buffers are file-static in ds_cnn.c.
#ifdef __cplusplus
extern "C" {
#endif

void ds_cnn_run(q7_t *in_data, q7_t *out_data);

#ifdef __cplusplus
}
#endif

#endif
