"""R7: header FER at list 8 vs list 32, BPSK AWGN LLRs, steady channel."""
import sys, time
import numpy as np
from sstvae.qrss import header
from sstvae.qrss.header import HeaderFields

rng = np.random.default_rng(7)
def hdr(i):
    return HeaderFields(callsign="AG7EW", grid="DM43", picture_id=int(rng.integers(0, 2**32)),
                        mode=1, segment=0, codec_id=0xd1d8, version=2, reserved=0)
t = time.time(); h = hdr(0); c = header.encode(h); print("encode", time.time() - t)
s = 1.0 - 2.0 * c.astype(np.float64)
t = time.time(); assert header.decode(4 * s) == h; t8 = time.time() - t
t = time.time(); header.decode(4 * s, list_size=32); t32 = time.time() - t
print(f"decode time list8 {t8*1000:.0f} ms, list32 {t32*1000:.0f} ms")
n = int(sys.argv[1]) if len(sys.argv) > 1 else 60
for esn0 in [float(x) for x in sys.argv[2:]] or [-12.5, -12.0, -11.5]:
    sig2 = 10 ** (-esn0 / 10)
    ok8 = ok32 = 0
    for k in range(n):
        h = hdr(k); s = 1.0 - 2.0 * header.encode(h).astype(np.float64)
        y = s + rng.standard_normal(s.size) * np.sqrt(sig2)
        llr = 2 * y / sig2
        ok8 += header.decode(llr) == h
        ok32 += header.decode(llr, list_size=32) == h
    print(f"Es/N0 {esn0:+.1f} dB: list8 {ok8}/{n}  list32 {ok32}/{n}", flush=True)
