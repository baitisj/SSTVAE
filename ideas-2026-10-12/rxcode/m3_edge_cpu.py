"""M3: (a) the front end's response and the noise floor near the band edges;
(b) where the joint receiver's extra CPU goes on a CE SHORT pass, and what an L pass spends."""
import cProfile, io, pstats, sys, time
sys.path.insert(0, "/home/user/sstvae"); sys.path.insert(0, "/home/user/sstvae/tests")
import numpy as np
from scipy import signal
from sstvae.qrss import frontend, track, receiver as RX, frame, lframe
from sstvae.qrss.constants import FS, FE_FS, CH_FS

# (a) FE filter magnitude at audio f (the 1250 Hz low-pass about 1500 Hz)
w, h = signal.freqz(frontend._FE_TAPS, worN=np.array([abs(f - 1500.0) for f in
                    (200, 230, 250, 270, 300, 326, 340, 360, 2640, 2660, 2700, 2730, 2750)]), fs=FS)
print("FE response dB at audio 200..2750:", dict(zip((200, 230, 250, 270, 300, 326, 340, 360, 2640, 2660, 2700, 2730, 2750),
      np.round(20 * np.log10(np.abs(h) + 1e-12), 2))))
rng = np.random.default_rng(5)
x = rng.standard_normal(FS * 120)
fe = frontend.audio_to_fe(x)
fe, lev = frontend.normalise(fe)
for f in (305.0, 326.0, 360.0, 400.0, 1500.0, 2600.0, 2660.0, 2695.0):
    ch = track.make_chan(fe, None, 0.0, FE_FS, f, 5.0, 115.0)
    psd = float(np.median(frontend.noise_psd(ch.ch, CH_FS)))
    # the truth at the signal: periodogram power near 0 Hz of the channel (+-20 Hz)
    seg = ch.ch[: (len(ch.ch) // 2500) * 2500].reshape(-1, 2500)
    P = np.mean(np.abs(np.fft.fft(seg * np.hanning(2500), axis=1)) ** 2, axis=0) / (CH_FS * np.sum(np.hanning(2500) ** 2))
    fr = np.fft.fftfreq(2500, 1 / CH_FS)
    near = float(np.mean(P[np.abs(fr) < 20]))
    lband = float(np.mean(P[(fr > -22) & (fr < -3)]))
    print(f"  chan at {f:6.1f} Hz: noise_psd {10*np.log10(psd):6.2f} dB, true at +-20 Hz {10*np.log10(near):6.2f} dB,"
          f" L data band {10*np.log10(lband):6.2f} dB -> SNR reads {10*np.log10(near/psd):+.2f} dB high")

# (b) CPU
from test_qrss_receiver import scenario
from qrss_fakes import l_scenario
from qrss_helpers import Q_TEST
SHORT, L_SHORT = frame.SHORT, lframe.L_SHORT
a, sim = scenario("short", -12.0, 1)
prep = RX.prepare(frontend.Capture(sim.fe, Q_TEST, sim.t0_index))
RX.receive_slot(prep, frame.TINY) if False else None
counts = {}
def counted(mod, name):
    f = getattr(mod, name)
    def g(*a, **k):
        counts[name] = counts.get(name, 0) + 1
        return f(*a, **k)
    setattr(mod, name, g)
from sstvae.qrss import ltrack, acquire
for mod, nm in ((track, "verify"), (ltrack, "verify_l"), (track, "verify_track"), (ltrack, "verify_track_l"),
                (track, "track"), (ltrack, "track_l"), (acquire, "track_before_detect"), (acquire, "preamble_search")):
    counted(mod, nm)
for label, sel in (("CE only", SHORT), ("joint", (SHORT, L_SHORT))):
    counts.clear()
    t = time.process_time()
    pr = cProfile.Profile(); pr.enable()
    out = RX.receive_slot(prep, sel)
    pr.disable()
    dt = time.process_time() - t
    print(f"\n== {label}: {dt:.1f} CPU s (profiled), passes {[(p.waveform, round(p.f_hz, 2)) for p in out]}, calls {counts}")
    s = io.StringIO(); pstats.Stats(pr, stream=s).sort_stats("tottime").print_stats(14); print(s.getvalue()[s.getvalue().find('ncalls'):][:2600])
    if label == "joint":
        s = io.StringIO(); pstats.Stats(pr, stream=s).sort_stats("cumulative").print_stats(r"receiver|acquire|ltrack|track\.py"); print(s.getvalue()[s.getvalue().find('ncalls'):][:3500])
a, lsim = l_scenario("l-short", -12.0, 1)
lprep = RX.prepare(frontend.Capture(lsim.fe, Q_TEST, lsim.t0_index))
counts.clear()
t = time.process_time()
pr = cProfile.Profile(); pr.enable()
out = RX.receive_slot(lprep, (SHORT, L_SHORT))
pr.disable()
print(f"\n== L short joint: {time.process_time() - t:.1f} CPU s (profiled), passes {[(p.waveform, round(p.f_hz, 2)) for p in out]}, calls {counts}")
s = io.StringIO(); pstats.Stats(pr, stream=s).sort_stats("tottime").print_stats(14); print(s.getvalue()[s.getvalue().find('ncalls'):][:2600])
