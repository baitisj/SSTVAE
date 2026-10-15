"""probe_method.py WAV SLOT FRAME: the detection method of each pass the
reference receiver returns (receive_pass is wrapped to record its Detection)."""
import sys
from pathlib import Path

sys.path.insert(0, "/home/user/sstvae")
sys.argv = [sys.argv[0], *sys.argv[1:]]
import qrss_receive as Q  # noqa: E402
from sstvae.qrss import receiver  # noqa: E402

wav, slot, fr = sys.argv[1:4]
seen = []
orig = receiver.receive_pass


def spy(cap, spec, det, *a, **k):
    p = orig(cap, spec, det, *a, **k)
    seen.append((spec.name, det.method, round(det.f_hz, 3), None if p is None else p.uid))
    return p


receiver.receive_pass = spy
cap, npz_frame = Q.load_capture(Path(wav), Q.parse_slot(slot), None)
args = Q.parse_args([wav, "--slot", slot, "--frame", fr, "--no-store"])
spec = Q.select_frames(args, npz_frame)
passes = receiver.receive_slot(cap, spec)
print("passes:", [p.uid for p in passes])
for s in seen:
    print("receive_pass:", s)
