"""A1 on an L pilot: today's span (-2.5, 22.5) s vs one reaching back over a 10 s
lead-in (-10.5, 22.5) s, with and without the lead-in, AWGN, l-tiny frames (same
preamble as L_FULL). Detected = A1 returns a line within 0.5 Hz of the pilot."""
import math, sys, time
import numpy as np
from sstvae.qrss import acquire as A, frontend, lframe, lwave
from sstvae.qrss import channel as ch
from sstvae.qrss.constants import FE_FS
from sstvae.qrss.morse import keying_units
PRE_S = 12.0
spec = lframe.L_TINY
def clean(lead, seed):
    rng = np.random.default_rng(100 + seed)
    sym = lframe.assemble(spec, None, rng.standard_normal(spec.n_data))
    bb = lwave.frame_baseband(sym, spec, keying=None, lead_in_s=lead)
    return lwave.resample(bb, FE_FS, int(-PRE_S * FE_FS), int(40.0 * FE_FS))
def capture(z, f, snr, seed):
    n = len(z)
    turns = ch.nominal_turns(f - 1500.0, np.arange(n), FE_FS, PRE_S * FE_FS)
    rng = np.random.default_rng(seed)
    var = FE_FS / (2500 * 10 ** (snr / 10))
    x = z * np.exp(2j * np.pi * (turns - np.floor(turns))) + math.sqrt(var / 2) * (
        rng.standard_normal(n) + 1j * rng.standard_normal(n))
    x, keep = frontend.blank(x.astype(np.complex64))
    x, _ = frontend.normalise(x, keep)
    return x, PRE_S * FE_FS
F = 1611.3
N_SEED = 12
t = time.time()
for snr in (-24, -26, -28, -30, -32):
    row = []
    for lead in (0.0, 10.0):
        for span in ((-2.5, 22.5), (-10.5, 22.5)):
            if lead == 0.0 and span[0] < -3: continue
            hit = band = 0; margin = []
            for s in range(N_SEED):
                fe, t0 = capture(clean(lead, s), F, snr, 1000 * s + 7)
                cs = A.carrier_line_stats(fe, t0, span_s=span)
                lines = A.carrier_lines(fe, t0, stats=cs)
                hit += any(abs(c.f_hz - F) < 0.5 for c in lines)
                band += any(F - 22.1 <= c.f_hz <= F + 0.5 for c in lines)
                thr = A.a1_threshold(cs.M)
                b = int(np.argmin(np.abs(cs.f_hz - F)))
                v = float(cs.stat[:, b - 1:b + 2].max())
                k, th = A.a1_gamma(cs.M)
                margin.append((v - thr) / math.sqrt(k * th * th))
            row.append(f"lead {lead:>4.0f} span {span[0]:>5}: pilot {hit:2d}/{N_SEED} band {band:2d}/{N_SEED} "
                       f"(stat-thr)/sd median {np.median(margin):+5.2f}")
    print(f"{snr} dB | " + " | ".join(row), flush=True)
print(f"{time.time()-t:.0f} s")
