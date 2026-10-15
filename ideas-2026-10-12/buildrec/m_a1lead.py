"""A1 on an L pass with and without an 8 s lead-in, default A1 window vs one
extended back over the lead-in. Found = an A1 line within 0.5 Hz of the pilot."""
import sys, time
sys.path.insert(0, "/home/user/sstvae"); sys.path.insert(0, "/home/user/sstvae/tests")
import numpy as np
from sstvae.qrss import acquire as A, frontend
from sstvae.qrss import receiver as RX
from qrss_fakes import l_scenario
from qrss_helpers import Q_TEST

SPANS = {"default": A.A1_SPAN_S, "ext": (-10.5, 22.5)}
t_all = time.time()
for lead in (0.0, 8.0):
    for snr in (-26.0, -28.0, -30.0, -32.0):
        found = {k: 0 for k in SPANS}; ratio = {k: [] for k in SPANS}
        n = 8
        for s in range(n):
            a, sim = l_scenario("l-short", snr, 300 + s, lead_in=lead)
            prep = RX.prepare(frontend.Capture(sim.fe, Q_TEST, sim.t0_index))
            for k, span in SPANS.items():
                cs = A.carrier_line_stats(prep.fe, prep.t0_index, prep.fs, span_s=span)
                thr = A.a1_threshold(cs.M)
                near = np.flatnonzero(np.abs(cs.f_hz - 1500.0) < 0.5)
                ratio[k].append(float(cs.stat[:, near].max()) / thr)
                lines = A.carrier_lines(prep.fe, prep.t0_index, prep.fs, stats=cs)
                found[k] += any(abs(c.f_hz - 1500.0) < 0.5 for c in lines)
        print(f"lead {lead:4.1f} s  SNR {snr:5.1f}: " + "  ".join(
            f"{k}: {found[k]}/{n} found, pilot stat/thr median {np.median(ratio[k]):.2f}"
            for k in SPANS), flush=True)
# noise only: does the extended window raise false lines? count lines on noise
for s in range(4):
    a, sim = l_scenario("l-short", None, 900 + s)
    rng = np.random.default_rng(s)
    fe = (rng.standard_normal(len(sim.fe)) + 1j * rng.standard_normal(len(sim.fe))).astype(np.complex64)
    prep = RX.prepare(frontend.Capture(fe, Q_TEST, sim.t0_index))
    out = []
    for k, span in SPANS.items():
        out.append(f"{k}: {len(A.carrier_lines(prep.fe, prep.t0_index, prep.fs, stats=A.carrier_line_stats(prep.fe, prep.t0_index, prep.fs, span_s=span)))} lines")
    print("noise", s, "  ".join(out))
print(f"total {time.time()-t_all:.1f} s")
