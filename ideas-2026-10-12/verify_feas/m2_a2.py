"""O15: share of A2 (preamble_search) spent in gammaincc, CE and L kinds, noise FE."""
import time, cProfile, pstats, io
import numpy as np
from scipy import special
from sstvae.qrss import acquire as A
from sstvae.qrss.constants import FE_FS

rng = np.random.default_rng(1)
n = int(60 * FE_FS)
fe = ((rng.standard_normal(n) + 1j * rng.standard_normal(n)) * np.sqrt(0.5 / np.log(2))).astype(np.complex64)
t0 = 15 * FE_FS
c = A.CarrierCandidate(f_hz=1500.0, drift_hz_per_min=0.0, metric=0.0)
for kind, name in ((A.CE_PRE, "CE"), (A.L_PRE, "L")):
    A.preamble_search(fe, t0, c, FE_FS, kind=kind)       # warm
    t = time.perf_counter()
    for _ in range(10):
        A.preamble_search(fe, t0, c, FE_FS, kind=kind)
    tot = (time.perf_counter() - t) / 10
    # gammaincc alone on this kind's grid
    inp = A._a2_input(fe, t0, c.f_hz, 0.0, FE_FS)
    chan = A._a2_mf(inp, kind)
    lam2, taus, S, var = A._preamble_sums(chan)
    st = A.preamble_scales(S, var)
    t = time.perf_counter()
    for _ in range(10):
        ps = np.stack([special.gammaincc(k, lam) for k, lam in st])
    tg = (time.perf_counter() - t) / 10
    t = time.perf_counter()
    for _ in range(10):
        A.preamble_p(S, var)
    tp = (time.perf_counter() - t) / 10
    # shortcut: per scale, gammaincc at the max only
    t = time.perf_counter()
    for _ in range(10):
        pm = min(special.gammaincc(k, lam.max()) for k, lam in st) * len(st)
    ts = (time.perf_counter() - t) / 10
    p, _, _ = A.preamble_p(S, var)
    print(f"{name}: grid {S.shape}, preamble_search {tot*1e3:.1f} ms, preamble_p {tp*1e3:.1f} ms, "
          f"gammaincc {tg*1e3:.1f} ms, shortcut {ts*1e3:.3f} ms; min p equal: {min(pm,1.0) == p.min()}")
