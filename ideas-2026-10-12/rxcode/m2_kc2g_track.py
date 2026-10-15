"""M2: the tracker's frequency path on KC2G's stored 04:15Z pass against the carrier itself.

Re-receives the pass from its stored 250 Hz channel (as EM does), from a
detection at A2's frequency (1205.56) and at the reported one (1206.08),
and prints tr.freq(t) beside a 30 s long-FFT carrier estimate."""
import json, time
from dataclasses import replace
from fractions import Fraction
import numpy as np
from sstvae.qrss import receiver, track, frame
from sstvae.qrss.store import Store
SNAP = "/tmp/claude-0/-home-user-sstvae/92006b74-04c2-5210-829f-64b8a831fdbf/scratchpad/snap/store"
st = Store(SNAP)
p = st.load_pass("1990769_1206082_48f31a83")
ch = np.asarray(p.ch, np.complex128); fs = p.ch_fs; t0i = p.ch_t0_index
fmix = 1500.0 + float(Fraction(p.f_mix_hz))
# carrier truth: 30 s Hann long-FFT peaks
W = 30 * fs; nfft = 1 << 18; fr = np.fft.fftfreq(nfft, 1 / fs); sel = np.abs(fr) < 3
truth = []
for a in range(0, len(ch) - W + 1, W):
    X = np.abs(np.fft.fft(ch[a:a + W] * np.hanning(W), nfft)) ** 2
    truth.append(((a + W / 2 - t0i) / fs, fmix + fr[sel][int(np.argmax(X[sel]))]))
truth = np.array(truth)
prep = receiver.capture_from_pass(p)
spec = frame.get(p.frame)
for f0 in (1205.56, float(p.report.offset_hz)):
    det = receiver.detection_from_pass(p)
    t = np.array([-10.0, 0.0, 1800.0])
    det = replace(det, f_hz=f0, path=replace(det.path, f_hz=np.full(3, f0)))
    t1 = time.time()
    chan = receiver.channel_for(prep, spec, det, (), p.f_mix_hz)
    tr = track.track(chan, spec, det, track.make_classes(spec), timing=p.timing)
    rep = track.report(tr)
    tt = truth[:, 0]
    fp = tr.freq(tt)
    print(f"start {f0:.2f}: offset {rep.offset_hz:.3f} drift {rep.drift_hz_per_min:.3f} "
          f"wander {rep.wander_hz_rms:.3f} df_max {tr.df_max:.3f} ({time.time() - t1:.0f} s)")
    for k in list(range(0, 6)) + list(range(len(tt) - 3, len(tt))):
        print(f"   t {tt[k]:7.1f}  carrier {truth[k, 1]:9.3f}  path {fp[k]:9.3f}  diff {fp[k] - truth[k, 1]:+.3f}")
    good = slice(None)
    print(f"   mean(path - carrier) {np.mean(fp - truth[:, 1]):+.3f} Hz, rms {np.std(fp - truth[:, 1]):.3f}")
    # the u_hat rotation the path leaves: per 30 s window
    tpos = tr.t_pos_s(); u = tr.u
    rot = []
    for a0 in tt:
        s = (tpos > a0 - 15) & (tpos < a0 + 15)
        s1 = np.nonzero(s)[0]
        if len(s1) < 10: rot.append(np.nan); continue
        uu = u[s1]
        rot.append(np.angle(np.sum(uu[1:] * np.conj(uu[:-1]))) / (2 * np.pi * spec.t_sym))
    rot = np.array(rot)
    print(f"   path + u_hat rotation - carrier: mean {np.nanmean(fp + rot - truth[:, 1]):+.3f}, "
          f"first window {fp[0] + rot[0] - truth[0, 1]:+.3f}")
