#!/usr/bin/env python3
# Generuje firmware/Core/Inc/mfcc_tables.h - stale tablice dla arm_mfcc_f32 na MCU.
# TE SAME wartosci co liczy input_data._init_cmsis_mfcc w treningu (cmsisdsp 1.9.7),
# dzieki czemu cechy MFCC na G474RE sa zgodne co do bitu z treningiem.
#
# Uzycie:
#   cd ~/projects/thesis/software
#   ./.venv/bin/python export_mfcc_tables.py [sciezka_wyjsciowa.h]
#
# Domyslnie zapisuje do ../firmware/Core/Inc/mfcc_tables.h
import os
import sys
import numpy as np
import scipy.signal as sig
import cmsisdsp.mfcc as mfcc_cfg
from cmsisdsp.datatype import F32

# --- parametry DOKLADNIE jak w input_data._init_cmsis_mfcc ---
SR        = 16000
FRAME_LEN = 640          # window_size_ms=40 -> 640 probek
STRIDE    = 320          # window_stride_ms=20 -> 320 probek
N_FRAMES  = 49           # spectrogram_length dla 1 s
N_DCT     = 10           # dct_coefficient_count
N_MEL     = 40           # NUM_FBANK_BINS
FFT_LEN   = 1 << int(np.ceil(np.log2(FRAME_LEN)))   # 640 -> 1024

DEFAULT_OUT = os.path.join(os.path.dirname(__file__),
                           "..", "firmware", "Core", "Inc", "mfcc_tables.h")


def build_tables():
    window = np.zeros(FFT_LEN, dtype=np.float32)
    window[:FRAME_LEN] = sig.windows.hann(FRAME_LEN, sym=False).astype(np.float32)
    filt_len, filt_pos, mel_coef = mfcc_cfg.melFilterMatrix(
        F32, 20.0, 4000.0, N_MEL, SR, FFT_LEN)
    dct_coef = mfcc_cfg.dctMatrix(F32, N_DCT, N_MEL)
    return (np.asarray(filt_pos, dtype=np.uint32),
            np.asarray(filt_len, dtype=np.uint32),
            np.asarray(mel_coef, dtype=np.float32),
            np.asarray(dct_coef, dtype=np.float32),
            window)


def _cfloat(x):
    # Poprawny literal float w C: zawsze z kropka/wykladnikiem (np. 0 -> 0.0f,
    # bo "0f" to blad - invalid suffix "f" on integer constant).
    s = "%.9g" % float(x)
    if not any(c in s for c in ".eEnN"):   # brak kropki/exp/inf/nan
        s += ".0"
    return s + "f"


def _fmt_f(a):
    return [_cfloat(x) for x in a]


def _fmt_u(a):
    return ["%d" % int(x) for x in a]


def _wrap(tokens, per):
    out = []
    for i in range(0, len(tokens), per):
        out.append("  " + ", ".join(tokens[i:i + per]))
    return ",\n".join(out)


def write_header(path):
    filt_pos, filt_len, mel_coef, dct_coef, window = build_tables()
    assert len(mel_coef) == int(filt_len.sum())
    assert len(dct_coef) == N_DCT * N_MEL
    assert len(window) == FFT_LEN

    H = []
    H.append("// AUTOGENEROWANE przez software/export_mfcc_tables.py (cmsisdsp 1.9.7).")
    H.append("// Te same tablice co w treningu (input_data.py). NIE edytowac recznie.")
    H.append("// Zgodnosc MFCC trening <-> MCU zalezy od tych wartosci.")
    H.append("#ifndef MFCC_TABLES_H")
    H.append("#define MFCC_TABLES_H")
    H.append("")
    H.append('#include "arm_math.h"')
    H.append("")
    H.append("#define MFCC_SR         %d" % SR)
    H.append("#define MFCC_FRAME_LEN  %d   // 40 ms" % FRAME_LEN)
    H.append("#define MFCC_STRIDE     %d   // 20 ms" % STRIDE)
    H.append("#define MFCC_N_FRAMES   %d" % N_FRAMES)
    H.append("#define MFCC_N_DCT      %d   // cech na ramke" % N_DCT)
    H.append("#define MFCC_N_MEL      %d" % N_MEL)
    H.append("#define MFCC_FFT_LEN    %d" % FFT_LEN)
    H.append("#define MFCC_N_COEFS    %d   // = sum(filter_len)" % int(filt_len.sum()))
    H.append("#define MFCC_FEATURES   (MFCC_N_FRAMES * MFCC_N_DCT)  // %d" % (N_FRAMES * N_DCT))
    H.append("")
    H.append("// Suma wiersza 0 macierzy DCT. Firmware (starszy CMSIS-DSP) przywraca")
    H.append("// normalizacje przez max po FFT, a trening (cmsisdsp 1.9.7) NIE. Roznica")
    H.append("// wpada tylko w coef0. Korekta: st_out[0] -= MFCC_DCT0_SUM * logf(maxValue).")
    H.append("#define MFCC_DCT0_SUM   %sf" % ("%.9g" % float(dct_coef[:N_MEL].sum())))
    H.append("")
    H.append("static const uint32_t mfcc_filter_pos[MFCC_N_MEL] = {")
    H.append(_wrap(_fmt_u(filt_pos), 12))
    H.append("};")
    H.append("")
    H.append("static const uint32_t mfcc_filter_len[MFCC_N_MEL] = {")
    H.append(_wrap(_fmt_u(filt_len), 12))
    H.append("};")
    H.append("")
    H.append("static const float32_t mfcc_filter_coefs[MFCC_N_COEFS] = {")
    H.append(_wrap(_fmt_f(mel_coef), 6))
    H.append("};")
    H.append("")
    H.append("static const float32_t mfcc_dct_coefs[MFCC_N_DCT * MFCC_N_MEL] = {")
    H.append(_wrap(_fmt_f(dct_coef), 6))
    H.append("};")
    H.append("")
    H.append("static const float32_t mfcc_window_coefs[MFCC_FFT_LEN] = {")
    H.append(_wrap(_fmt_f(window), 6))
    H.append("};")
    H.append("")
    H.append("#endif // MFCC_TABLES_H")

    with open(path, "w") as f:
        f.write("\n".join(H) + "\n")
    print("FFT_LEN=%d  N_MEL=%d  N_DCT=%d  sum(filter_len)=%d" %
          (FFT_LEN, N_MEL, N_DCT, int(filt_len.sum())))
    print("Zapisano:", os.path.normpath(path))


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_OUT
    write_header(out)
