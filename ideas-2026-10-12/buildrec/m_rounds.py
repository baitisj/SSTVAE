"""Round A vs round B: scale (slope of z on the true latents), raw and rescaled
latent SNR, W honesty, and the reported frequency f(0) vs the frame-mean path.
Usage: m_rounds.py L|CE"""
import sys, time
sys.path.insert(0, "/home/user/sstvae"); sys.path.insert(0, "/home/user/sstvae/tests")
import numpy as np
from sstvae.qrss import frame, frontend, header, ldemod, lframe, channel, morse, demod
from sstvae.qrss import receiver as RX
from sstvae.qrss.channel import ChannelConfig
from sstvae.qrss.constants import WAVEFORM_L, WAVEFORM_CE
from sstvae.qrss.precoder import precode
from qrss_fakes import l_scenario, L_HDR, L_CALL
from qrss_helpers import Q_TEST, unit_rms_latents

WF = sys.argv[1]

def stats(z, w, a):
    z = np.asarray(z, float); w = np.asarray(w, float)
    keep = w > 0
    z, w, a = z[keep], w[keep], a[keep]
    k = float(z @ a / (a @ a))
    raw = 10*np.log10(np.mean(a**2) / np.mean((z - a)**2))
    res = 10*np.log10(np.mean(a**2) / np.mean((z / k - a)**2))
    honest = float(np.mean((z - a)**2 * w))
    W = 10*np.log10(np.mean(w))
    return k, raw, res, honest, W

def ce_sim(snr, seed, cfg):
    spec = frame.SHORT
    a = unit_rms_latents(spec.n_data, seed)
    sym = frame.assemble(spec, header.encode(L_HDR), precode(a, Q_TEST))
    sim = channel.simulate(sym, spec, Q_TEST, ChannelConfig(snr_db=snr, seed=seed, **dict(cfg)),
                           carrier_hz=1500.0, keying=morse.keying_units(L_CALL))
    return a, sim

def one(snr, seed, preset=None):
    cfg = (("preset", preset),) if preset else ()
    t = time.time()
    if WF == "L":
        spec = lframe.L_SHORT
        a, sim = l_scenario("l-short", snr, seed, cfg=cfg)
        sel = (spec,); hints = ((1500.0, WAVEFORM_L),)
    else:
        spec = frame.SHORT
        a, sim = ce_sim(snr, seed, cfg)
        sel = spec; hints = (1500.0,)
    cap = frontend.Capture(sim.fe, Q_TEST, sim.t0_index)
    prep = RX.prepare(cap)
    dets = RX.detect(prep, sel, hints=hints, max_candidates=2)
    dets = [d for d in dets if abs(d.f_hz - 1500.0) < 3 and d.waveform == spec.waveform]
    if not dets:
        print(f"{WF} {preset or 'steady'} {snr} s{seed}: no detection ({time.time()-t:.1f} s)"); return
    out = RX.receive_pass(prep, spec, dets[0], return_tracks=True)
    if out is None:
        print(f"{WF} {snr} s{seed}: receive failed"); return
    pr, trs = out
    if WF == "L":
        zA, wA, _, dA = ldemod.extract_l(None, trs[0], spec, Q_TEST, "joint")
    else:
        zA, wA, _, dA = demod.extract(None, trs[0], spec, Q_TEST, "joint")
    kA = stats(zA, wA, a); kB = stats(pr.z, pr.w, a)
    tr = trs[-1]
    dur = spec.keyed_end_pos * spec.t_sym
    ts = np.linspace(0.0, dur, 400)
    fp = np.array([float(tr.freq(x)) for x in ts])
    tag = "B" if len(trs) > 1 else "A only"
    print(f"{WF} {preset or 'steady':8s} {snr:6.1f} s{seed}: hdr {pr.header is not None} "
          f"| A k {kA[0]:.3f} raw {kA[1]:6.2f} resc {kA[2]:6.2f} honest {kA[3]:.3f} W {kA[4]:6.2f} kappa {dA['kappa']:.3f} "
          f"| {tag} k {kB[0]:.3f} raw {kB[1]:6.2f} resc {kB[2]:6.2f} honest {kB[3]:.3f} W {kB[4]:6.2f} "
          f"| f0-1500 {fp[0]-1500:+.3f} mean {fp.mean()-1500:+.3f} median {np.median(fp)-1500:+.3f} "
          f"({time.time()-t:.1f} s)", flush=True)

if WF == "L":
    cases = [(-12, 1), (-12, 2), (-24, 1), (-24, 2), (-24, 3), (-28, 1), (-28, 2), (-28, 3), (-28, 4)]
    fades = [(-12, 1), (-12, 2), (-12, 3)]
else:
    cases = [(-12, 1), (-12, 2), (-20, 1), (-20, 2), (-24, 1), (-24, 2)]
    fades = [(-12, 1), (-12, 2)]
for snr, s in cases:
    one(snr, s)
for snr, s in fades:
    one(snr, s, "moderate")
