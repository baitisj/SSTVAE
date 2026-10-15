"""A3 (whole-slot track-before-detect) blind detection of L_FULL, steady and quiet.
Question: where is A3's L 50% point? (decides how much F1+L1/L6/L7/R3's A1 gain
adds to the *stored* pass, which comes from the finish whose A3 already runs).
found = an A3 hit (p<TBD_PFA) whose detection lands within 0.5 Hz of the pilot,
gated as L or ambiguous."""
import sys, time, math
sys.path.insert(0, "/home/user/sstvae/tests")
import numpy as np
from qrss_fakes import l_keying, l_symbols
from qrss_helpers import Q_TEST, unit_rms_latents
from sstvae.qrss import acquire as A, frame, frontend, lframe
from sstvae.qrss import channel as ch
from sstvae.qrss.channel import ChannelConfig
from sstvae.qrss.constants import WAVEFORM_L

lspec = lframe.L_FULL
specs = (frame.FULL, lspec)
cells = [(p, float(s)) for p, s in
         [(a.split(":")[0], a.split(":")[1]) for a in sys.argv[1].split(",")]]
nseed = int(sys.argv[2])
sym_cache = {}
for preset, snr in cells:
    res = []
    for k in range(nseed):
        seed = 500 + k
        f = 900.0 + 131.7 * k + 0.37
        t = time.time()
        if seed not in sym_cache:
            sym_cache[seed] = l_symbols(lspec, unit_rms_latents(lspec.n_data, seed))
        sim = ch.simulate(sym_cache[seed], lspec, Q_TEST,
                          ChannelConfig(snr_db=snr, seed=seed, preset=preset),
                          carrier_hz=f, keying=l_keying(lspec))
        x, keep = frontend.blank(sim.fe)
        x, _ = frontend.normalise(x, keep)
        hits = A.track_before_detect(x, sim.t0_index, lspec, shape_specs=specs)
        near = [h for h in hits if abs(h.f_hz - f) < 25]
        ok = False; info = ""
        for h in near:
            d = h.to_detection(WAVEFORM_L)
            if abs(d.f_hz - f) < 0.5:
                ok = True
            info += f" hit {h.f_hz-f:+.2f} z={h.z:.1f} p={h.p_value:.1e} wf={h.waveform} dL={d.f_hz-f:+.2f}"
        allz = [h.z for h in hits]
        res.append(ok)
        print(f"{preset} {snr} seed {seed}: found={ok} nhits={len(hits)}{info} ({time.time()-t:.1f}s)", flush=True)
    print(f"== {preset} {snr}: {sum(res)}/{len(res)}", flush=True)
