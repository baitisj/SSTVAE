"""M1: where is KC2G's carrier really? Long-FFT peak per 60 s of the stored 250 Hz channel."""
import json, sys
from fractions import Fraction
import numpy as np
SNAP = "/tmp/claude-0/-home-user-sstvae/92006b74-04c2-5210-829f-64b8a831fdbf/scratchpad/snap/store/passes/"
for f in ["1990769_1206082_48f31a83", "1990767_1205472_11834407"]:
    d = np.load(SNAP + f + ".npz", allow_pickle=True)
    m = json.loads(d["meta"].tobytes().decode())
    ch = d["ch"].astype(np.complex128); fs = m["ch_fs"]; t0i = m["ch_t0_index"]
    fmix = 1500.0 + float(Fraction(m["f_mix_hz"]))
    print(f, "mix", round(fmix, 4), "report offset", round(m["report"]["offset_hz"], 3),
          "drift", round(m["report"]["drift_hz_per_min"], 3), "Hz/min, len", len(ch) / fs, "s")
    W = 60 * fs
    nfft = 1 << 18
    fr = np.fft.fftfreq(nfft, 1 / fs)
    sel = np.abs(fr) < 3
    out = []
    for a in range(0, len(ch) - W + 1, W):
        x = ch[a:a + W] * np.hanning(W)
        X = np.abs(np.fft.fft(x, nfft)) ** 2
        Xs = X[sel]; frs = fr[sel]
        k = int(np.argmax(Xs))
        pk = frs[k]
        floor = np.median(X[(np.abs(fr) > 4) & (np.abs(fr) < 40)])
        tmid = (a + W / 2 - t0i) / fs
        out.append((tmid, fmix + pk, 10 * np.log10(Xs[k] / floor)))
    for t, fz, s in out:
        print(f"  t {t:7.1f} s  carrier {fz:9.3f} Hz  peak/floor {s:5.1f} dB")
    arr = np.array(out)
    good = arr[:, 2] > 15
    p = np.polyfit(arr[good, 0], arr[good, 1], 1)
    print("  line fit: f(t0) = %.3f Hz, drift %.3f Hz/min, rms resid %.3f Hz" % (
        p[1], p[0] * 60, np.std(arr[good, 1] - np.polyval(p, arr[good, 0]))))
