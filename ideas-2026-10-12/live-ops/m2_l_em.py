"""M2 (live-ops): what would leave-one-out EM (data-aided re-tracking) buy an L pass?

l-short on the moderate path. The pass is received blind (round A + B), then
re-tracked from its own 250 Hz capture with a soft-data prior built from an
independent reference of the same latents at per-latent SNR Wr (what the
leave-one-out accumulator would hold), header known:
  plain   - the pass as received
  prior   - re-tracked with the whole prior in the templates (no split)
  split   - CE EM's block split: each precoder block read from a track that
            withholds that block's parity of blocks
Reported: the pass's own latent SNR (unscaled) and mean W, and the accumulated
SNR of reference + pass with the pass's own weights.
"""
import sys, time
sys.path.insert(0, "/home/user/sstvae"); sys.path.insert(0, "/home/user/sstvae/tests")
import numpy as np
from sstvae.qrss import em, frontend, header, ldemod, lframe, ltrack, receiver as RX
from sstvae.qrss.morse import keying_units
from sstvae.qrss.precoder import block_index
from sstvae.qrss.types import EmPrior
from qrss_fakes import l_scenario, L_CALL
from qrss_helpers import Q_TEST

spec = lframe.get("l-short")


def snr(z, a):
    return 10 * np.log10(1.0 / np.mean((np.asarray(z, float) - a) ** 2))


def wdb(w):
    return 10 * np.log10(np.mean(np.asarray(w, float)))


def acc_snr(z, w, a, mref, Wr):
    m = (Wr * mref + np.asarray(w, float) * np.asarray(z, float)) / (Wr + np.asarray(w, float))
    return snr(m, a)


def split_rereceive(p, prior, hdr):
    prep = RX.capture_from_pass(p)
    det = RX.detection_from_pass(p)
    chan = RX.channel_for(prep, spec, det, f_mix_hz=p.f_mix_hz)
    bits = header.encode(hdr)
    cwk = (keying_units(hdr.callsign.rstrip(" ")), False)
    first = ltrack.track_l(chan, spec, det, ltrack.make_classes_l(spec, bits, prior),
                           cw_known=cwk, timing=p.timing)
    par = block_index(spec.n_data) % 2
    z = np.zeros(spec.n_data); w = np.zeros(spec.n_data)
    for k in (0, 1):
        own = par == k
        pk = EmPrior(x_hat=np.where(own, 0.0, prior.x_hat), v=np.where(own, 1.0, prior.v))
        trk = ltrack.track_l(chan, spec, det, ltrack.make_classes_l(spec, bits, pk),
                             cw_known=cwk, timing=first.timing, freq=first.freq, outer=1)
        trk.z_ref = p.report.z_ref
        zk, wk, _, _ = ldemod.extract_l(None, trk, spec, p.q, "joint")
        z[own], w[own] = zk[own], wk[own]
    return z, w


for snr_db in (-12.0, -20.0):
    for seed in (41, 42):
        t0 = time.time()
        a, sim = l_scenario("l-short", snr=snr_db, seed=seed, cfg=(("preset", "moderate"),))
        cap = frontend.Capture(np.asarray(sim.fe, np.complex64), Q_TEST, sim.t0_index)
        ps = RX.receive_slot(cap, spec)
        if not ps:
            print(f"{snr_db} seed {seed}: no pass"); continue
        p = max(ps, key=lambda p: p.report.z_ref)
        hdr = p.header
        line = (f"{snr_db:+.0f} dB seed {seed}: plain {snr(p.z, a):+.2f} (W {wdb(p.w):+.2f})"
                f" hdr {'ok' if hdr else 'none'}")
        rng = np.random.default_rng(seed + 1000)
        for wr_db in (0.0, 6.0):
            Wr = 10 ** (wr_db / 10)
            mref = a + rng.standard_normal(a.size) / np.sqrt(Wr)
            prior = em.prior_from(Wr * mref, np.full(a.size, Wr), p.q)
            pp = RX.receive_pass(RX.capture_from_pass(p), spec, RX.detection_from_pass(p),
                                 prior=prior, f_mix_hz=p.f_mix_hz)
            line += (f" | Wr {wr_db:+.0f}: acc plain {acc_snr(p.z, p.w, a, mref, Wr):+.2f}"
                     f" prior {snr(pp.z, a):+.2f}/W{wdb(pp.w):+.2f}/acc "
                     f"{acc_snr(pp.z, pp.w, a, mref, Wr):+.2f}")
            if hdr is not None:
                zs, ws = split_rereceive(p, prior, hdr)
                line += (f" split {snr(zs, a):+.2f}/W{wdb(ws):+.2f}/acc "
                         f"{acc_snr(zs, ws, a, mref, Wr):+.2f}")
        print(line + f" ({time.time() - t0:.0f} s)", flush=True)
