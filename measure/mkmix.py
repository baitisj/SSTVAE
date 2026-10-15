"""mkmix.py OUT SNR_DB SEED TX.wav [TX.wav ...]: the float transmit WAVs
(qrss_transmit --float, same slot, all starting 12 s before t0) summed, each
scaled to unit power over its keyed samples, plus white noise putting each at
SNR_DB in 2500 Hz (noise power in 2500 Hz = sigma^2 * 2500/4000 at 8 kHz),
written as peak-normalised int16 (sstvae.wavio.write_wav)."""
import sys
import numpy as np
sys.path.insert(0, "/home/user/sstvae")
from scipy.io import wavfile
from sstvae import wavio

out, snr, seed, *ins = sys.argv[1:]
snr, seed = float(snr), int(seed)
xs = []
for p in ins:
    fs, x = wavfile.read(p)
    assert fs == 8000 and x.dtype == np.float32, (p, fs, x.dtype)
    x = x.astype(np.float64)
    on = np.abs(x) > 1e-6
    pw = float(np.mean(x[on] ** 2))
    xs.append(x / np.sqrt(pw))
    print(f"{p}: {len(x)} samples, keyed {on.mean():.3f}, power {pw:.4g}")
n = max(len(x) for x in xs)
y = np.zeros(n)
for x in xs:
    y[:len(x)] += x
sigma = np.sqrt(1.0 / (10 ** (snr / 10) * 2500 / 4000))
y += sigma * np.random.default_rng(seed).standard_normal(n)
wavio.write_wav(out, y)
print(f"{out}: {n} samples ({n / 8000:.1f} s), noise sigma {sigma:.3f}")
