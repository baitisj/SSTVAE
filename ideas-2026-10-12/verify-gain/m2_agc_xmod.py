"""R5: how much does a strong L signal's envelope modulate the inverse AGC's
gain, i.e. what multiplicative noise does it impose on every OTHER signal in
the band? Report the gain modulation g = 1/sqrt(level) over the data section:
cap = mean(g)^2/var(g) (SINR ceiling it imposes on a neighbour, if untracked),
split into the part above 0.5 Hz (too fast for any gain tracker) and below."""
import sys, math
sys.path.insert(0, "/home/user/sstvae/tests")
import numpy as np
from qrss_fakes import l_keying, l_symbols
from qrss_helpers import unit_rms_latents
from sstvae.qrss import frontend, lframe, lwave
from sstvae.qrss.constants import FE_FS
from scipy import signal

spec = lframe.get("l-short")
sym = l_symbols(spec, unit_rms_latents(spec.n_data, 3))
PRE = 12.0
bb = lwave.frame_baseband(sym, spec, keying=l_keying(spec))
z = lwave.resample(bb, FE_FS, int(-PRE * FE_FS), int(150 * FE_FS))
n = len(z)
f_off = 700.0  # pilot 700 Hz above FE centre (mid band), irrelevant to AGC
tt = np.arange(n) / FE_FS
z = z * np.exp(2j * np.pi * (f_off * tt % 1.0))
a, b = int((PRE + 25) * FE_FS), int((PRE + 120) * FE_FS)   # inside the data section
for snr in (-6.0, 0.0, 10.0, 20.0):
    rng = np.random.default_rng(1)
    var = FE_FS / (2500 * 10 ** (snr / 10))
    sc = 1 / math.sqrt(var)     # noise unit variance, signal scaled
    x = (z * sc + (rng.standard_normal(n) + 1j * rng.standard_normal(n)) / math.sqrt(2)).astype(np.complex64)
    x, keep = frontend.blank(x)
    _, lev = frontend.normalise(x, keep)
    g = 1 / np.sqrt(lev[a:b].astype(float))
    g = g / g.mean()
    tot = float(np.var(g))
    sos = signal.butter(4, 0.5, "highpass", fs=FE_FS, output="sos")
    fast = float(np.var(signal.sosfiltfilt(sos, g)))
    blank_frac = float(np.mean(keep[a:b] < 1))
    print(f"strong L at {snr:+5.1f} dB SNR2500: gain mod rms {math.sqrt(tot)*100:5.2f}% "
          f"-> cap {10*math.log10(1/tot):5.1f} dB; >0.5 Hz part cap {10*math.log10(1/fast):5.1f} dB; "
          f"blanked {blank_frac*100:.2f}%", flush=True)
