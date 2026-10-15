"""L_FULL pass, Gaussian latents (2 seeds): envelope peaks at 250 Hz and in the
8 kHz audio, what the app's peak normalisation (`tx::condition_for_output`) then
leaves as average power, per-section power, and a fourth clip pass."""
import functools, sys, time
import numpy as np
from sstvae.qrss import lframe, lwave, ce
from sstvae.qrss import constants as C
from sstvae.qrss.morse import keying_units

spec = lframe.L_FULL
key = keying_units("AG7EW")

def frame(seed):
    rng = np.random.default_rng(seed)
    bits = rng.integers(0, 2, C.N_HDR_BITS).astype(np.uint8)
    x = rng.normal(size=spec.n_data)
    return lframe.assemble(spec, bits, x)

def stats(sym, lead, tag):
    t = time.time()
    bb = lwave.frame_baseband(sym, spec, keying=key, lead_in_s=lead)
    tau = lwave.tau_grid(bb.fs, bb.n0, len(bb.z))
    secs = lwave._pilot_sections(spec, tau, lead)
    p = np.abs(bb.z) ** 2
    dm = secs["data"]
    pd = p[dm].mean()
    q = lambda arr, qq: 10 * np.log10(np.quantile(arr, qq) / pd)
    out = {k: 10 * np.log10(p[m].mean() / pd) for k, m in secs.items()}
    # 8 kHz envelope in chunks, and the real audio's peak
    n_pre = 12 * 8000
    n = int((lwave.keyed_span_pos(spec, lead)[1] * spec.t_sym + 3 + 12) * 8000)
    env_max = 0.0; aud_max = 0.0; ss = 0.0; cnt = 0; hist = []
    step = 120 * 8000
    num, den = ce.carrier_fraction(1500)
    for i0 in range(0, n, step):
        m = min(step, n - i0)
        z = lwave.resample(bb, 8000, i0 - n_pre, m)
        a = ce.to_audio(z, 8000, num, den, 1.0, n0=i0)
        env_max = max(env_max, float(np.abs(z).max())); aud_max = max(aud_max, float(np.abs(a).max()))
        tt = (np.arange(i0, i0 + m) - n_pre) / 8000 / spec.t_sym
        sel = np.zeros(m, bool)
        lay = lframe.layout(spec)
        first = lay.pos[spec.n_pre + spec.n_hdr]
        sel = (tt > first + 8) & (tt < lay.pos[-1] - 8)
        for aa, bb_ in lay.win_pos:
            sel &= ~((tt > aa - 9) & (tt < bb_ + 9))
        hist.append(np.abs(z[sel]) ** 2)
        ss += float(np.sum(a ** 2)); cnt += m
    h = np.concatenate(hist)
    pd8 = h.mean()
    q8 = {qq: round(10 * np.log10(np.quantile(h, qq) / pd8), 2) for qq in (0.999, 0.9999, 0.99999)}
    max8 = 10 * np.log10(env_max ** 2 / pd8)
    # app: scale so audio peak = level; avg data power relative to a steady tone at the same peak
    rel = 10 * np.log10(pd8 * 0.5 / aud_max ** 2)   # data's audio power / (peak^2/2)
    n_over = {d: int(np.sum(h > pd8 * 10 ** (d / 10))) for d in (3.5, 4.0, 4.5)}
    print(f"{tag}: section power vs data (dB) " + ", ".join(f"{k} {v:+.2f}" for k, v in out.items()))
    print(f"   250 Hz data PAPR q99.99 {q(p[dm], 0.9999):.2f}, max {10*np.log10(p[dm].max()/pd):.2f} dB;"
          f" max over whole pass {10*np.log10(p.max()/pd):.2f} dB")
    print(f"   8 kHz data envelope PAPR {q8}, max(whole pass) {max8:.2f} dB, audio peak/sqrt2 vs data rms: {-rel:.2f} dB;"
          f" samples over 3.5/4/4.5 dB: {n_over} of {len(h)}  ({time.time()-t:.0f} s)")
    return bb

for seed in (1, 2):
    sym = frame(seed)
    stats(sym, 10.0, f"seed {seed}, 3 passes")
orig = lwave.clip
lwave.clip = functools.partial(orig, overshoot=(1.0, 1.5, 2.0, 2.0))
sym = frame(1)
stats(sym, 10.0, "seed 1, 4 passes (1,1.5,2,2)")
st = lwave.loopback_stats(sym, spec, keying=key, lead_in_s=10.0)
print("   4-pass loopback: SINR %.2f dB, data/ref %.3f, pilot %.3f, papr %.2f, avg_power %.3f" % (
    st["sinr_db"], st["data_vs_ref"], st["pilot_frac"], st["papr_db"], st["avg_power"]))
lwave.clip = orig
st = lwave.loopback_stats(sym, spec, keying=key, lead_in_s=10.0)
print("   3-pass loopback: SINR %.2f dB, data/ref %.3f, pilot %.3f, papr %.2f, avg_power %.3f, sinr_pre %.2f, sinr_hdr %.2f, sinr_ref %.2f" % (
    st["sinr_db"], st["data_vs_ref"], st["pilot_frac"], st["papr_db"], st["avg_power"], st["sinr_pre_db"], st["sinr_hdr_db"], st["sinr_ref_db"]))
print("   gains: data %.3f ref %.3f hdr %.3f pre %.3f" % tuple(abs(st[k]) for k in ("gain_data", "gain_ref", "gain_hdr", "gain_pre")))
print("   pilot gains:", {k: round(abs(v), 3) for k, v in st["gain_pilot"].items()})
