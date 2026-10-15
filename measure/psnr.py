import sys, math
import numpy as np
from PIL import Image
a = np.asarray(Image.open(sys.argv[1]).convert("RGB"), dtype=np.float64)
b = np.asarray(Image.open(sys.argv[2]).convert("RGB"), dtype=np.float64)
assert a.shape == b.shape, (a.shape, b.shape)
mse = float(np.mean((a - b) ** 2))
ps = math.inf if mse == 0 else 10 * math.log10(255.0 ** 2 / mse)
print(f"{sys.argv[1]} vs {sys.argv[2]}: shape {a.shape}, identical pixels {np.array_equal(a, b)}, max |d| {np.abs(a-b).max():.0f}, PSNR {ps:.2f} dB")
if len(sys.argv) > 3:
    o = np.asarray(Image.open(sys.argv[3]).convert("RGB"), dtype=np.float64)
    for n, x in (("py", a), ("cpp", b)):
        m = float(np.mean((x - o) ** 2)); print(f"  {n} vs original: PSNR {10*math.log10(255**2/m):.2f} dB")
