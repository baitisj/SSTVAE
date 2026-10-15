"""Spec 6.3/6.4 tracking model (sims/l_spread.py) with and without the data-aided (EM)
reference, for several L pilot shares. Read-only use of the shared sims."""
import sys
sys.path.insert(0, "/mnt/project-files/qrss-spec/sims")
import numpy as np
import tracking as T
import l_spread as LS  # prints its tables on import; fine

LOSS = LS.LOSS
RHO = 0.054
def model(F_R, F_D, em, clip_db=12.2, rho=RHO, copy_ref=True):
    FD = F_D / (1 + rho)
    FR = F_R + (F_D * rho / (1 + rho) if copy_ref else 0.0)
    G = FD * 2500 / (T.RS * 15 / 16)
    S_CLIP = 10 ** ((clip_db - 10 * np.log10(1 + rho)) / 10)
    def s_eff(S, B, r):
        c = 2500 * S * (FR + FD * r)
        e = T.mmse_gauss(c, B)
        s1 = G * S
        st = s1 * (1 - e) / (1 + s1 * e)
        return 1 / (1 / st + 1 / S_CLIP)
    def acc(S, N, B, iters=300):
        if not em:
            return N * s_eff(S, B, 0.0)
        s_loo = 0.0
        for _ in range(iters):
            new = (N - 1) * s_eff(S, B, s_loo / (1 + s_loo))
            if abs(new - s_loo) < 1e-10 * max(1, s_loo):
                s_loo = new; break
            s_loo = new
        return N * s_eff(S, B, s_loo / (1 + s_loo))
    def req(t, N, B):
        lo, hi = -60.0, 20.0
        for _ in range(60):
            mid = (lo + hi) / 2
            if 10 * np.log10(max(acc(10 ** (mid / 10), N, B), 1e-30)) >= t: hi = mid
            else: lo = mid
        return hi
    return req
# power splits after clipping from sims/tracking.py (clipstudy, gaussian, 0 dB headroom)
configs = {"tone 8%": (0.074 + 0.067, 0.796), "tone 15% (spec)": (0.138 + 0.063, 0.739),
           "tone 25%": (0.234 + 0.055, 0.650), "tone 35%": (0.331 + 0.048, 0.563)}
Ns = [1, 4, 16, 64]
print("\n\n##### L thresholds (dB SNR2500 at average power), cold start, copy as reference")
for tname, tdb in [("good +2.2", 2.2), ("usable -3.6", -3.6), ("recog -9.0", -9.0)]:
    for pname, B in [("quiet", 0.1), ("moderate", 0.5), ("disturbed", 1.0)]:
        line = f"{tname:11s} {pname:9s}"
        for em in (True, False):
            for cname, (fr, fd) in configs.items():
                req = model(fr, fd, em)
                v = [req(tdb + LOSS[tdb][n], n, B) for n in Ns]
                line += f" | {'EM' if em else 'noEM'} {cname[5:8]}:" + ",".join(f"{x:6.1f}" for x in v)
        print(line)
