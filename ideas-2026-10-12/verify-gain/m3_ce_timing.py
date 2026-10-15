"""C2: does CE's refine_timing walk the timing off at low SNR the way L's did
before TRK's fix (l-short -27 dB: 0.105 T rms, 0.33 dB mean loss)?
CE SHORT, steady, start the round-A track AT the true timing; compare
refine=True (what the receiver does) with refine=False (true timing kept).
Report timing error (T units, rms over frame positions) and the latent-SNR
difference (refined minus true-timing), z rescaled by its slope."""
import sys, time, math
sys.path.insert(0, "/home/user/sstvae/tests")
import numpy as np
from sstvae.qrss import frame, frontend, header, channel, morse, demod, track
from sstvae.qrss import receiver as RX
from sstvae.qrss.channel import ChannelConfig
from sstvae.qrss.constants import CH_FS, T_SYM, WAVEFORM_CE
from sstvae.qrss.precoder import precode
from sstvae.qrss.types import Detection, FreqPath, Timing
from qrss_fakes import L_HDR, L_CALL
from qrss_helpers import Q_TEST, unit_rms_latents

spec = frame.SHORT
snr = float(sys.argv[1]); seeds = range(int(sys.argv[2]), int(sys.argv[3])); OFFS = [float(x) for x in sys.argv[4].split(',')]
sps = spec.t_sym * CH_FS
p = np.arange(spec.n_pos, dtype=np.float64)

def tau(tm):
    return tm.tau0 + p * sps * (1 + tm.ppm * 1e-6) + tm.gamma * (p / spec.n_pos) ** 2

def lsnr(z, w, a):
    z = np.asarray(z, float); keep = np.asarray(w) > 0
    z, a = z[keep], a[keep]
    k = float(z @ a / (a @ a))
    return 10 * math.log10(np.mean(a ** 2) / np.mean((z / k - a) ** 2)), k

errs, dls = [], []
for seed in seeds:
    t = time.time()
    a = unit_rms_latents(spec.n_data, seed)
    sym = frame.assemble(spec, header.encode(L_HDR), precode(a, Q_TEST))
    sim = channel.simulate(sym, spec, Q_TEST, ChannelConfig(snr_db=snr, seed=seed),
                           carrier_hz=1500.0, keying=morse.keying_units(L_CALL))
    prep = RX.prepare(frontend.Capture(sim.fe, Q_TEST, sim.t0_index))
    tt = np.asarray(sim.truth.t_pos_s, float)
    slope, icpt = np.polyfit(p, tt * CH_FS, 1)
    true = Timing(tau0=float(icpt), ppm=float((slope / sps - 1) * 1e6), gamma=0.0, z=0.0,
                  cov=np.zeros((3, 3)))
    path = FreqPath(t_s=np.array([-30.0, 0.0, 2000.0]), f_hz=np.full(3, 1500.0), weight=np.ones(3))
    det = Detection(f_hz=1500.0, path=path, timing=true, z_ref=10.0, method="preamble",
                    lead_in_s=0.0, waveform=WAVEFORM_CE)
    chan = RX.channel_for(prep, spec, det, (), None)
    tr = track.track(chan, spec, det, track.make_classes(spec), timing=true, refine=False)
    z, w, _, _ = demod.extract(None, tr, spec, Q_TEST, "joint")
    ref_snr = lsnr(z, w, a)[0]
    for off in OFFS:
        st = Timing(tau0=true.tau0 + off * sps, ppm=true.ppm, gamma=0.0, z=0.0, cov=np.zeros((3, 3)))
        tr = track.track(chan, spec, det, track.make_classes(spec), timing=st, refine=True)
        z, w, _, _ = demod.extract(None, tr, spec, Q_TEST, "joint")
        sn, k = lsnr(z, w, a)
        tm = tr.timing
        e = float(np.sqrt(np.mean((tau(tm) - tau(true)) ** 2)) / sps)
        e0 = (tm.tau0 - true.tau0) / sps
        dl = sn - ref_snr
        errs.append(e); dls.append(dl)
        print(f"CE short {snr} s{seed} start {off:+.2f} T: final err rms {e:.3f} T (tau0 {e0:+.3f}) "
              f"latent {sn:.2f} vs true-timing {ref_snr:.2f}: {dl:+.2f} dB ({time.time()-t:.1f}s)", flush=True)
print(f"== {snr} dB: timing rms over seeds {math.sqrt(np.mean(np.square(errs))):.3f} T, "
      f"mean loss {-np.mean(dls):+.2f} dB, max loss {-min(dls):+.2f} dB")
