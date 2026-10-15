import sys, math, time, numpy as np
sys.argv = ["x"]
src = "/tmp/claude-0/-home-user-sstvae/92006b74-04c2-5210-829f-64b8a831fdbf/scratchpad/a2_l.py"
exec(open(src).read().split("f0 = 1512.3")[0])
from sstvae.qrss import acquire as A
f0 = 1512.3
for snr in (-24.0, -25.0, -26.0, -27.0, -28.0, -29.0):
    n16 = n64 = nthr = 0; N = 20
    for s in range(N):
        x, t0 = capture(z_l, f0, snr, 1000 + s)
        cs = A.carrier_line_stats(x, t0)
        thr = A.a1_threshold(cs.M)
        lines = A.carrier_lines(x, t0, stats=cs, pfa=0.5, max_cands=400)
        r = next((i + 1 for i, c in enumerate(lines) if abs(c.f_hz - f0) < 1.0), None)
        if r is not None:
            n16 += r <= 16; n64 += r <= 64; nthr += (r <= 16 and lines[r-1].metric > thr)
    print(f"{snr}: rank<=16 {n16}/{N}, rank<=64 {n64}/{N}, as today (<=16 and over thr) {nthr}/{N}", flush=True)
