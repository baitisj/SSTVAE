# Rank of the L pilot's A1 line among A1's picks (10 Hz apart) on noise, threshold off. Scratch only.
import sys, math, time, numpy as np
sys.argv = ["x"]
src = "/tmp/claude-0/-home-user-sstvae/92006b74-04c2-5210-829f-64b8a831fdbf/scratchpad/a2_l.py"
exec(open(src).read().split("f0 = 1512.3")[0])
from sstvae.qrss import acquire as A
f0 = 1512.3
t = time.time()
for snr in (-24.0, -26.0, -28.0, -31.0):
    ranks = []; above = 0; ce_ranks = []
    for s in range(6):
        x, t0 = capture(z_l, f0, snr, 300 + s)
        cs = A.carrier_line_stats(x, t0)
        thr = A.a1_threshold(cs.M)
        lines = A.carrier_lines(x, t0, stats=cs, pfa=0.5, max_cands=400)
        r = next((i + 1 for i, c in enumerate(lines) if abs(c.f_hz - f0) < 1.0), None)
        ranks.append(r)
        if r is not None and lines[r - 1].metric > thr: above += 1
        if s < 3:
            xc, t0c = capture(z_c, f0, snr, 400 + s)
            csc = A.carrier_line_stats(xc, t0c)
            lc = A.carrier_lines(xc, t0c, stats=csc, pfa=0.5, max_cands=400)
            ce_ranks.append(next((i + 1 for i, c in enumerate(lc) if abs(c.f_hz - f0) < 1.0), None))
    print(f"{snr}: L pilot rank {ranks} (above 1e-x thr {above}/6), n lines {len(lines)}; CE carrier rank {ce_ranks}; {time.time()-t:.0f}s", flush=True)
