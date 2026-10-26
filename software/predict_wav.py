# Uruchamia wytrenowany model (z checkpointu) na pojedynczym pliku WAV,
# liczac cechy MFCC przez CMSIS-DSP - DOKLADNIE tak jak w treningu i na MCU.
# NIE uzywa freeze.py/label_wav.py, bo tamte licza MFCC TensorFlow-em (niezgodne).
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import argparse
import sys
import wave

import numpy as np
import tensorflow as tf
import input_data
import models


def load_wav_mono16k(path, sample_rate):
  w = wave.open(path, 'rb')
  n_ch = w.getnchannels()
  fr = w.getframerate()
  sampw = w.getsampwidth()
  raw = w.readframes(w.getnframes())
  w.close()
  if sampw != 2:
    raise ValueError('Oczekiwano 16-bit PCM, plik ma %d bajtow/probke' % sampw)
  x = np.frombuffer(raw, dtype=np.int16).astype(np.float32)
  if n_ch > 1:                       # jesli stereo -> bierzemy pierwszy kanal
    x = x[::n_ch]
  if fr != sample_rate:
    raise ValueError('Plik ma %d Hz, oczekiwano %d Hz (nagraj w 16 kHz)' % (fr, sample_rate))
  return x / 32768.0                 # skala [-1, 1], jak decode_wav w treningu


def to_one_second(x, sample_rate, trim_ms):
  # odetnij startowy transient ("plop" przy inicjalizacji DMA/PDM na MCU)
  trim = int(sample_rate * trim_ms / 1000)
  x = x[trim:]
  target = sample_rate              # 1 s
  if x.size >= target:
    x = x[:target]                  # przytnij do 1 s
  else:
    x = np.pad(x, (0, target - x.size), mode='constant')  # dopelnij cisza
  return x


def main(_):
  sess = tf.InteractiveSession()
  words_list = input_data.prepare_words_list(FLAGS.wanted_words.split(','))
  model_settings = models.prepare_model_settings(
      len(words_list), FLAGS.sample_rate, FLAGS.clip_duration_ms,
      FLAGS.window_size_ms, FLAGS.window_stride_ms, FLAGS.dct_coefficient_count)

  # MFCC przez CMSIS-DSP: instancja AudioProcessor bez __init__ (bez pobierania datasetu)
  ap = input_data.AudioProcessor.__new__(input_data.AudioProcessor)
  ap._init_cmsis_mfcc(model_settings)

  fingerprint_size = model_settings['fingerprint_size']
  fingerprint_input = tf.placeholder(tf.float32, [None, fingerprint_size],
                                     name='fingerprint_input')
  logits = models.create_model(fingerprint_input, model_settings,
                               FLAGS.model_architecture, FLAGS.model_size_info,
                               is_training=False)
  softmax = tf.nn.softmax(logits)
  models.load_variables_from_checkpoint(sess, FLAGS.checkpoint)

  # wczytaj WAV -> 1 s -> MFCC (CMSIS-DSP). Szumu NIE dodajemy: realne nagranie juz go ma.
  x = load_wav_mono16k(FLAGS.wav, FLAGS.sample_rate)
  x = to_one_second(x, FLAGS.sample_rate, FLAGS.trim_ms)
  feats = ap._mfcc_cmsis(x).reshape(1, fingerprint_size)

  probs = sess.run(softmax, feed_dict={fingerprint_input: feats})[0]

  order = np.argsort(probs)[::-1]
  print('\nPlik: %s' % FLAGS.wav)
  print('Predykcja: %s  (%.1f%%)\n' % (words_list[order[0]], 100 * probs[order[0]]))
  print('Wszystkie klasy:')
  for i in order:
    print('  %-12s %6.2f%%' % (words_list[i], 100 * probs[i]))


if __name__ == '__main__':
  parser = argparse.ArgumentParser()
  parser.add_argument('--wav', type=str, required=True,
                      help='Plik WAV do rozpoznania (16 kHz, mono, 16-bit).')
  parser.add_argument('--checkpoint', type=str, required=True,
                      help='Checkpoint z wagami, np. work/DS_CNN/training/best/ds_cnn_XXXX.ckpt-YYYY')
  parser.add_argument('--wanted_words', type=str, default='go,backward,left,right,stop',
                      help='Slowa (w tej samej kolejnosci co trening).')
  parser.add_argument('--model_architecture', type=str, default='ds_cnn')
  parser.add_argument('--model_size_info', type=int, nargs='+',
                      default=[5, 64, 10, 4, 2, 2, 64, 3, 3, 1, 1, 64, 3, 3, 1, 1,
                               64, 3, 3, 1, 1, 64, 3, 3, 1, 1])
  parser.add_argument('--sample_rate', type=int, default=16000)
  parser.add_argument('--clip_duration_ms', type=int, default=1000)
  parser.add_argument('--window_size_ms', type=float, default=40.0)
  parser.add_argument('--window_stride_ms', type=float, default=20.0)
  parser.add_argument('--dct_coefficient_count', type=int, default=10)
  parser.add_argument('--trim_ms', type=float, default=150.0,
                      help='Ile ms odciac z poczatku (startowy transient). 0 = nic.')
  FLAGS, unparsed = parser.parse_known_args()
  tf.app.run(main=main, argv=[sys.argv[0]] + unparsed)
