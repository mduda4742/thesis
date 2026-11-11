#!/usr/bin/env python3
# Generuje firmware/Core/Inc/ds_cnn_weights.h dla NASZEJ sieci (5 slow):
#   - wagi/biasy int8: brane 1:1 z software/weights.h (wyjscie quant_test.py, zweryfikowane 91.74%),
#     tylko przemianowane z nazw TF (DS-CNN_conv_1_weights_0) na nazwy ARM (CONV1_WT),
#   - 20 przesuniec (*_BIAS_LSHIFT / *_OUT_RSHIFT) + AVEPOOL_OUT_LSHIFT, policzone z dec_bits:
#       dec_bits wag/biasow  -> z checkpointu _bnfused (ta sama formula co quant_test),
#       dec_bits aktywacji   -> z act_max (find_act_max.py).
#     Wzor CMSIS-NN:  BIAS_LSHIFT = in_dec + wt_dec - bias_dec
#                     OUT_RSHIFT  = in_dec + wt_dec - out_dec
#
# Uzycie (z katalogu software):
#   ./.venv/bin/python export_ds_cnn_weights.py
import os
import re
import math
import numpy as np
import tensorflow as tf

HERE = os.path.dirname(os.path.abspath(__file__))
WEIGHTS_IN = os.path.join(HERE, 'weights.h')
CKPT = os.path.join(HERE, 'work/DS_CNN/training/best/ds_cnn_9313.ckpt-20000_bnfused')
OUT = os.path.join(HERE, '..', 'firmware', 'Core', 'Inc', 'ds_cnn_weights.h')

# act_max zmierzone przez find_act_max.py. Kolejnosc = punkty fake_quant:
# [input, conv1, ds1_dw, ds1_pw, ds2_dw, ds2_pw, ds3_dw, ds3_pw, ds4_dw, ds4_pw, avgpool, logits]
ACT_MAX = [64, 8, 16, 16, 16, 16, 16, 16, 16, 16, 4, 64]

# Warstwy: (label, ckpt_wt, ckpt_bias, tf_wt, tf_bias, arm_wt, arm_bias)
#   ckpt_*  - nazwa w checkpoincie (do dec_bits),
#   tf_*    - nazwa w weights.h (po replace '/'->'_', ':'->'_'),
#   arm_*   - nazwa #define, ktorej uzywa ds_cnn.c.
# ARM CONV2..CONV5 = nasze conv_ds_1..4.
LAYERS = [
 ('CONV1', 'DS-CNN/conv_1/weights', 'DS-CNN/conv_1/biases',
  'DS-CNN_conv_1_weights_0', 'DS-CNN_conv_1_biases_0', 'CONV1_WT', 'CONV1_BIAS'),
 ('CONV2_DS', 'DS-CNN/conv_ds_1/dw_conv/depthwise_weights', 'DS-CNN/conv_ds_1/dw_conv/biases',
  'DS-CNN_conv_ds_1_dw_conv_depthwise_weights_0', 'DS-CNN_conv_ds_1_dw_conv_biases_0', 'CONV2_DS_WT', 'CONV2_DS_BIAS'),
 ('CONV2_PW', 'DS-CNN/conv_ds_1/pw_conv/weights', 'DS-CNN/conv_ds_1/pw_conv/biases',
  'DS-CNN_conv_ds_1_pw_conv_weights_0', 'DS-CNN_conv_ds_1_pw_conv_biases_0', 'CONV2_PW_WT', 'CONV2_PW_BIAS'),
 ('CONV3_DS', 'DS-CNN/conv_ds_2/dw_conv/depthwise_weights', 'DS-CNN/conv_ds_2/dw_conv/biases',
  'DS-CNN_conv_ds_2_dw_conv_depthwise_weights_0', 'DS-CNN_conv_ds_2_dw_conv_biases_0', 'CONV3_DS_WT', 'CONV3_DS_BIAS'),
 ('CONV3_PW', 'DS-CNN/conv_ds_2/pw_conv/weights', 'DS-CNN/conv_ds_2/pw_conv/biases',
  'DS-CNN_conv_ds_2_pw_conv_weights_0', 'DS-CNN_conv_ds_2_pw_conv_biases_0', 'CONV3_PW_WT', 'CONV3_PW_BIAS'),
 ('CONV4_DS', 'DS-CNN/conv_ds_3/dw_conv/depthwise_weights', 'DS-CNN/conv_ds_3/dw_conv/biases',
  'DS-CNN_conv_ds_3_dw_conv_depthwise_weights_0', 'DS-CNN_conv_ds_3_dw_conv_biases_0', 'CONV4_DS_WT', 'CONV4_DS_BIAS'),
 ('CONV4_PW', 'DS-CNN/conv_ds_3/pw_conv/weights', 'DS-CNN/conv_ds_3/pw_conv/biases',
  'DS-CNN_conv_ds_3_pw_conv_weights_0', 'DS-CNN_conv_ds_3_pw_conv_biases_0', 'CONV4_PW_WT', 'CONV4_PW_BIAS'),
 ('CONV5_DS', 'DS-CNN/conv_ds_4/dw_conv/depthwise_weights', 'DS-CNN/conv_ds_4/dw_conv/biases',
  'DS-CNN_conv_ds_4_dw_conv_depthwise_weights_0', 'DS-CNN_conv_ds_4_dw_conv_biases_0', 'CONV5_DS_WT', 'CONV5_DS_BIAS'),
 ('CONV5_PW', 'DS-CNN/conv_ds_4/pw_conv/weights', 'DS-CNN/conv_ds_4/pw_conv/biases',
  'DS-CNN_conv_ds_4_pw_conv_weights_0', 'DS-CNN_conv_ds_4_pw_conv_biases_0', 'CONV5_PW_WT', 'CONV5_PW_BIAS'),
 ('FINAL_FC', 'DS-CNN/fc1/weights', 'DS-CNN/fc1/biases',
  'DS-CNN_fc1_weights_0', 'DS-CNN_fc1_biases_0', 'FINAL_FC_WT', 'FINAL_FC_BIAS'),
]

# Przeplyw aktywacji: (label, in_act_idx, out_act_idx). AVEPOOL osobno (in=9 -> out=10).
SHIFT_FLOW = [
 ('CONV1', 0, 1),
 ('CONV2_DS', 1, 2), ('CONV2_PW', 2, 3),
 ('CONV3_DS', 3, 4), ('CONV3_PW', 4, 5),
 ('CONV4_DS', 5, 6), ('CONV4_PW', 6, 7),
 ('CONV5_DS', 7, 8), ('CONV5_PW', 8, 9),
 ('FINAL_FC', 10, 11),
]


def act_dec(act_max):
    return 7 - int(round(math.log2(act_max)))


def wt_dec(reader, ckpt_name):
    a = reader.get_tensor(ckpt_name)
    mx = max(abs(float(a.min())), abs(float(a.max())))
    return 7 - int(math.ceil(math.log2(mx)))


def parse_weights_h(path):
    txt = open(path).read()
    d = {}
    for m in re.finditer(r'#define\s+(\S+)\s+\{([^}]*)\}', txt):
        vals = [v.strip() for v in m.group(2).split(',') if v.strip() != '']
        d[m.group(1)] = vals
    return d


def wrap(vals, per=20):
    return ",\n".join("  " + ", ".join(vals[i:i+per]) for i in range(0, len(vals), per))


def main():
    reader = tf.train.load_checkpoint(CKPT)
    wdict = parse_weights_h(WEIGHTS_IN)
    adec = [act_dec(a) for a in ACT_MAX]

    # dec_bits wag/biasow z checkpointu
    dw = {}
    for (label, cw, cb, tw, tb, aw, ab) in LAYERS:
        dw[label + '_wt'] = wt_dec(reader, cw)
        dw[label + '_bias'] = wt_dec(reader, cb)

    # przesuniecia
    shifts = []
    print('%-10s %4s %4s %4s %4s | %6s %6s' %
          ('warstwa', 'in', 'wt', 'bia', 'out', 'LSHIFT', 'RSHIFT'))
    for (label, iin, iout) in SHIFT_FLOW:
        in_d, out_d = adec[iin], adec[iout]
        wt_d, bias_d = dw[label + '_wt'], dw[label + '_bias']
        lsh = in_d + wt_d - bias_d
        rsh = in_d + wt_d - out_d
        shifts.append((label + '_BIAS_LSHIFT', lsh))
        shifts.append((label + '_OUT_RSHIFT', rsh))
        print('%-10s %4d %4d %4d %4d | %6d %6d' % (label, in_d, wt_d, bias_d, out_d, lsh, rsh))
    avepool_lshift = adec[10] - adec[9]
    print('AVEPOOL    (in=%d out=%d) -> OUT_LSHIFT %d' % (adec[9], adec[10], avepool_lshift))

    # naglowek
    H = []
    H.append("// AUTOGENEROWANE przez software/export_ds_cnn_weights.py - NIE edytowac recznie.")
    H.append("// Wagi int8: z software/weights.h (quant_test.py). Przesuniecia: z dec_bits wag+aktywacji.")
    H.append("#ifndef DS_CNN_WEIGHTS_H")
    H.append("#define DS_CNN_WEIGHTS_H")
    H.append("")
    H.append("// --- przesuniecia bitowe (Qm.n) policzone dla NASZEJ kwantyzacji ---")
    for name, val in shifts:
        H.append("#define %-24s %d" % (name, val))
    H.append("#define %-24s %d" % ('AVEPOOL_OUT_LSHIFT', avepool_lshift))
    H.append("")
    H.append("// --- wagi/biasy int8 (te same co w software/weights.h) ---")
    for (label, cw, cb, tw, tb, aw, ab) in LAYERS:
        for tf_name, arm_name in ((tw, aw), (tb, ab)):
            if tf_name not in wdict:
                raise KeyError('brak %s w %s' % (tf_name, WEIGHTS_IN))
            vals = wdict[tf_name]
            # cala tablica w JEDNEJ linii (jak ds_cnn_weights.h ARM) - makro nie znosi \n
            H.append("#define %s {%s}" % (arm_name, ", ".join(vals)))
            H.append("")
    H.append("#endif // DS_CNN_WEIGHTS_H")

    with open(OUT, 'w') as f:
        f.write("\n".join(H) + "\n")
    print("\nZapisano:", os.path.normpath(OUT))


if __name__ == '__main__':
    main()
