#!/usr/bin/env python3
# Generuje firmware/Core/Inc/mfcc_selftest.h: deterministyczna ramka testowa (640 probek
# int16) + oczekiwany wektor MFCC policzony SCIEZKA TRENINGOWA (input_data + cmsisdsp 1.9.7).
# Sluzy do walidacji na MCU, ze arm_mfcc_f32 (+ korekta coef0) liczy to samo co trening.
#
# Uzycie (z katalogu software, zeby import input_data dzialal):
#   cd ~/projects/thesis/software
#   ./.venv/bin/python export_mfcc_selftest.py [sciezka_wyjsciowa.h]
import os
import sys
import numpy as np
import cmsisdsp as dsp
import input_data
import models

SR = 16000
FRAME_LEN = 640
DEFAULT_OUT = os.path.join(os.path.dirname(__file__),
                           "..", "firmware", "Core", "Inc", "mfcc_selftest.h")


def _cfloat(v):
    s = "%.9g" % float(v)
    if not any(c in s for c in ".eEnN"):   # poprawny literal float (0 -> 0.0f)
        s += ".0"
    return s + "f"


def _wrap(toks, per):
    return ",\n".join("  " + ", ".join(toks[i:i + per]) for i in range(0, len(toks), per))


def main(out):
    ms = models.prepare_model_settings(
        len(input_data.prepare_words_list('go,backward,left,right,stop'.split(','))),
        SR, 1000, 40, 20, 10)
    ap = input_data.AudioProcessor.__new__(input_data.AudioProcessor)
    ap._init_cmsis_mfcc(ms)

    # deterministyczna ramka: suma trzech sinusow, zaokraglona do int16
    n = np.arange(FRAME_LEN)
    x = (1.0 * np.sin(2 * np.pi * 400.0 * n / SR)
         + 0.5 * np.sin(2 * np.pi * 2000.0 * n / SR))
    frame_i16 = np.round(x * 32768.0).astype(np.int16)
    frame_f32 = frame_i16.astype(np.float32) / 32768.0

    # MFCC jednej ramki sciezka treningowa (cmsisdsp.arm_mfcc_f32 - jak w _mfcc_cmsis)
    buf = np.zeros(ap._fft_len, dtype=np.float32)
    buf[:FRAME_LEN] = frame_f32
    tmp = np.zeros(ap._fft_len, dtype=np.float32)
    expected = dsp.arm_mfcc_f32(ap._mfcc_inst, buf, tmp).copy()

    H = []
    H.append("// AUTOGENEROWANE przez software/export_mfcc_selftest.py.")
    H.append("// Ramka testowa + oczekiwany MFCC (sciezka treningowa: cmsisdsp 1.9.7).")
    H.append("// Na MCU: mfcc_compute(mfcc_selftest_input, out) i porownaj out z expected.")
    H.append("#ifndef MFCC_SELFTEST_H")
    H.append("#define MFCC_SELFTEST_H")
    H.append('#include "arm_math.h"')
    H.append("")
    H.append("#define MFCC_SELFTEST_LEN %d" % FRAME_LEN)
    H.append("static const int16_t mfcc_selftest_input[%d] = {" % FRAME_LEN)
    H.append(_wrap(["%d" % v for v in frame_i16], 16))
    H.append("};")
    H.append("static const float32_t mfcc_selftest_expected[10] = {")
    H.append(_wrap([_cfloat(v) for v in expected], 5))
    H.append("};")
    H.append("#endif // MFCC_SELFTEST_H")

    with open(out, "w") as f:
        f.write("\n".join(H) + "\n")
    print("expected =", np.array2string(expected, precision=6, separator=', '))
    print("Zapisano:", os.path.normpath(out))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_OUT)
