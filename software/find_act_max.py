#!/usr/bin/env python3
# Znajduje zakresy aktywacji (act_max) do kwantyzacji int8 sieci DS-CNN.
#
# ARM w quant_test.py tylko PRZYJMUJE --act_max, ale ich NIE mierzy. Ten skrypt
# przepuszcza zbior walidacyjny przez model (bez fake-quant, act_max=0), podglada
# max(|aktywacja|) w 12 punktach (dokladnie tam, gdzie quant_models wstawia
# fake_quant) i proponuje act_max = najblizsza potega 2 >= tego maksimum.
#
# WAZNE: model zalezy od szumu mikrofonu w cechach -> uruchamiaj z
#   KWS_MIC_NOISE=/home/mduda/Documents/silent.wav
# inaczej zakresy nie odpowiadaja treningowi.
#
# Uzycie (z katalogu software):
#   KWS_MIC_NOISE=/home/mduda/Documents/silent.wav ./.venv/bin/python find_act_max.py \
#       --checkpoint work/DS_CNN/training/best/ds_cnn_9313.ckpt-20000_bnfused
#
# Wynik: tabela zakresow + gotowy ciag "--act_max ..." do wklejenia w quant_test.py.
import os
import argparse
import math
import numpy as np
import tensorflow as tf
import quant_models as models
import input_data

# --- parametry DOKLADNIE jak w treningu/quant_test ---
SR, CLIP, WSZ, WST, DCT = 16000, 1000, 40.0, 20.0, 10
MSI = [5, 64, 10, 4, 2, 2, 64, 3, 3, 1, 1, 64, 3, 3, 1, 1,
       64, 3, 3, 1, 1, 64, 3, 3, 1, 1]
WANTED = 'go,backward,left,right,stop'

# 12 punktow pomiaru w KOLEJNOSCI indeksow act_max[0..11].
# Nazwy tensorow potwierdzone zwiadem po grafie (surowe wyjscia PRZED ReLU,
# bo wlasnie tam quant_models.py wstawia fake_quant).
ACT_TENSORS = [
    ('act_max[0]  input',       'Reshape:0'),
    ('act_max[1]  conv_1',      'DS-CNN/conv_1/BiasAdd:0'),
    ('act_max[2]  ds1_dw',      'DS-CNN/conv_ds_1/dw_conv/BiasAdd:0'),
    ('act_max[3]  ds1_pw',      'DS-CNN/conv_ds_1/pw_conv/BiasAdd:0'),
    ('act_max[4]  ds2_dw',      'DS-CNN/conv_ds_2/dw_conv/BiasAdd:0'),
    ('act_max[5]  ds2_pw',      'DS-CNN/conv_ds_2/pw_conv/BiasAdd:0'),
    ('act_max[6]  ds3_dw',      'DS-CNN/conv_ds_3/dw_conv/BiasAdd:0'),
    ('act_max[7]  ds3_pw',      'DS-CNN/conv_ds_3/pw_conv/BiasAdd:0'),
    ('act_max[8]  ds4_dw',      'DS-CNN/conv_ds_4/dw_conv/BiasAdd:0'),
    ('act_max[9]  ds4_pw',      'DS-CNN/conv_ds_4/pw_conv/BiasAdd:0'),
    ('act_max[10] avg_pool',    'DS-CNN/avg_pool/AvgPool:0'),
    ('act_max[11] fc1_logits',  'DS-CNN/fc1/BiasAdd:0'),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--checkpoint', required=True,
                    help='Checkpoint _bnfused do zmierzenia.')
    ap.add_argument('--data_dir', default='/home/mduda/Documents/Thesis/database/')
    ap.add_argument('--set', default='validation',
                    choices=['validation', 'testing', 'training'])
    ap.add_argument('--batch_size', type=int, default=100)
    FLAGS = ap.parse_args()

    if os.environ.get('KWS_MIC_NOISE') is None:
        print('UWAGA: KWS_MIC_NOISE nie ustawione -> zakresy liczone BEZ szumu '
              'mikrofonu, niezgodnie z treningiem. Ustaw KWS_MIC_NOISE=.../silent.wav\n')

    sess = tf.InteractiveSession()
    words = input_data.prepare_words_list(WANTED.split(','))
    ms = models.prepare_model_settings(len(words), SR, CLIP, WSZ, WST, DCT)
    audio = input_data.AudioProcessor('', FLAGS.data_dir, 10.0, 10.0,
                                      WANTED.split(','), 10, 10, ms)

    fp = tf.placeholder(tf.float32, [None, ms['fingerprint_size']],
                        name='fingerprint_input')
    # act_max = same zera -> ZADEN fake_quant nie jest wstawiany (czysty float)
    models.create_model(fp, ms, 'ds_cnn', MSI, [0] * 12, is_training=False)
    models.load_variables_from_checkpoint(sess, FLAGS.checkpoint)

    g = tf.get_default_graph()
    tensors = [g.get_tensor_by_name(nm) for _, nm in ACT_TENSORS]
    maxabs = np.zeros(len(tensors), dtype=np.float64)

    set_size = audio.set_size(FLAGS.set)
    print('Mierze zakresy na zbiorze "%s" (N=%d)...' % (FLAGS.set, set_size))
    for i in range(0, set_size, FLAGS.batch_size):
        fps, _ = audio.get_data(FLAGS.batch_size, i, ms, 0.0, 0.0, 0,
                                FLAGS.set, sess)
        outs = sess.run(tensors, feed_dict={fp: fps})
        for k, o in enumerate(outs):
            m = float(np.max(np.abs(o)))
            if m > maxabs[k]:
                maxabs[k] = m

    print('\n%-22s %10s %9s %7s %7s' %
          ('punkt', 'max|akt|', 'act_max', 'int_b', 'dec_b'))
    print('-' * 60)
    act_max_vals = []
    for k, (name, _) in enumerate(ACT_TENSORS):
        m = maxabs[k]
        int_bits = int(math.ceil(math.log2(m))) if m > 0 else 0
        am = float(2 ** int_bits)       # najblizsza potega 2 >= max|akt|
        dec_bits = 7 - int_bits         # przesuniecie bitowe na MCU (Qm.n)
        act_max_vals.append(am)
        print('%-22s %10.4f %9g %7d %7d' % (name, m, am, int_bits, dec_bits))

    print('\nGotowy argument do quant_test.py:')
    print('  --act_max ' + ' '.join('%g' % v for v in act_max_vals))


if __name__ == '__main__':
    main()
