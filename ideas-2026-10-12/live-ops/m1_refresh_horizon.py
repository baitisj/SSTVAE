"""M1 (live-ops): does a live refresh's cost depend on how much of the pass has been heard?

One CE FULL pass at -12 dB in a ring, read as the live listener reads it at
`progress` of the frame (the rest noise-filled and erased), then the live
refresh's two receiver calls: detect (gate V on the preamble hit) and
receive_slot(round_b=False, dets). CPU seconds per call at 5% and 95%.
Usage: m1_refresh_horizon.py [ce|l]"""
import resource, sys, time
sys.path.insert(0, "/home/user/sstvae/scripts"); sys.path.insert(0, "/home/user/sstvae")
sys.path.insert(0, "/home/user/sstvae/tests")
import numpy as np
from sstvae.qrss import frame, lframe, live, receiver
from sstvae.qrss.constants import FE_FS
from sstvae.qrss.frontend import slot_t0

wf = sys.argv[1] if len(sys.argv) > 1 else "ce"
if wf == "ce":
    import qrss_spread_header as S
    spec = frame.FULL
    _, sim, fe = S.simulate(spec, -12.0, 5)
    q = S.Q
    sel = spec
else:
    from qrss_fakes import l_scenario
    from qrss_helpers import Q_TEST
    spec = lframe.get("l-full")
    _, sim = l_scenario("l-full", snr=-12.0, seed=5)
    fe = np.asarray(sim.fe, np.complex64)
    q = Q_TEST
    sel = (frame.FULL, spec)


def cpu():
    r = resource.getrusage(resource.RUSAGE_SELF)
    return r.ru_utime + r.ru_stime


for prog in (0.05, 0.95):
    ring = live.FeRing(live.frame_seconds(spec) + 400.0)
    n0 = int(slot_t0(q) * FE_FS) - int(sim.t0_index)
    k = int(sim.t0_index + prog * live.frame_seconds(spec) * FE_FS)
    ring.write(n0, fe[:k])
    c0 = cpu()
    prep, erase, heard = live.slot_capture(ring, q, sel)
    c1 = cpu()
    dets = receiver.detect(prep, sel, live_only=True, erase_s=erase)
    c2 = cpu()
    passes = receiver.receive_slot(prep, sel, estimator="joint", erase_s=erase, round_b=False,
                                   dets=list(dets))
    c3 = cpu()
    print(f"{wf} progress {prog:.2f}: capture {c1 - c0:.1f} s, detect {c2 - c1:.1f} s "
          f"({len(dets)} det, z {[round(float(d.z_ref), 1) for d in dets]}), "
          f"receive {c3 - c2:.1f} s, passes {len(passes)} "
          f"mean W {[round(receiver.mean_w_db(p), 2) for p in passes]}", flush=True)
