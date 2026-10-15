"""cmp_listen.py PYDIR CPPDIR: compare two listen replays (state/, store/),
the way tests/test_qrss_native_listen.py's _same_run does, and report sizes."""
import json
import math
import os
import re
import subprocess
import sys
from pathlib import Path

import numpy as np

py, cpp = Path(sys.argv[1]), Path(sys.argv[2])
TOL = {"f_hz": 0.1, "snr_db": 0.3, "mean_w_db": 0.05}
problems = []


def state(d):
    return json.loads((d / "state" / "state.json").read_text())


def norm(line):
    m = re.match(r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ (listening: .*)$", line)
    if m:
        line = "<utc> " + m.group(1)
    return re.sub(r" \((\d+) s\)$", " (_ s)", line)


def racy(line):
    return " signal(s) at 100% (" in line


def tree(d):
    if not d.exists():
        return []
    return sorted(p.relative_to(d).as_posix() + ("/" if p.is_dir() else "") for p in d.rglob("*")
                  if not p.name.startswith(".") or p.name == ".lock")


sp, sn = state(py), state(cpp)
worst = {k: 0.0 for k in (*TOL, "z_ref_rel", "confidence")}
tp, tn = sp["tiles"], sn["tiles"]
if len(tp) != len(tn):
    problems.append(f"tile count {len(tp)} vs {len(tn)}")
for x, y in zip(tp, tn):
    for k in sorted(set(x) | set(y)):
        if k not in x or k not in y:
            problems.append(f"tile {x.get('id')}: key {k} only on one side")
            continue
        if k in TOL:
            d = abs(x[k] - y[k])
            worst[k] = max(worst[k], d)
            if d >= TOL[k]:
                problems.append(f"tile {x['id']} {k}: {x[k]} vs {y[k]}")
        elif k == "z_ref":
            d = abs(x[k] - y[k]) / abs(x[k])
            worst["z_ref_rel"] = max(worst["z_ref_rel"], d)
        elif k == "confidence":
            for u, v in zip(x[k], y[k]):
                if (u is None) != (v is None):
                    problems.append(f"tile {x['id']} confidence None mismatch")
                elif u is not None:
                    worst[k] = max(worst[k], abs(u - v))
        elif k == "uid":
            if x[k].split("_")[:2] != y[k].split("_")[:2]:
                problems.append(f"tile {x['id']} uid {x[k]} vs {y[k]}")
        elif x[k] != y[k]:
            problems.append(f"tile {x['id']} {k}: {x[k]!r} vs {y[k]!r}")
for t in tp:
    print(f"tile {t['id']}: {t.get('waveform')} {t.get('frame')} status {t.get('status')} callsign {t.get('callsign')} "
          f"picture {t.get('picture_id')} f_hz {t.get('f_hz')} snr_db {t.get('snr_db')} mean_w_db {t.get('mean_w_db')} "
          f"image {t.get('image')}")
print("worst:", ", ".join(f"{k} {v:.3g}" for k, v in worst.items()))
for k in sorted(set(sp) | set(sn)):
    if k in ("updated", "tiles"):
        continue
    if k == "log":
        a = [norm(x) for x in sp[k] if not racy(x)]
        b = [norm(x) for x in sn[k] if not racy(x)]
        if a != b:
            problems.append("state.json log differs")
    elif sp.get(k) != sn.get(k):
        problems.append(f"state.json {k}: {sp.get(k)!r} vs {sn.get(k)!r}")
lp = [norm(x) for x in (py / "state" / "listener.log").read_text().splitlines() if not racy(x)]
ln = [norm(x) for x in (cpp / "state" / "listener.log").read_text().splitlines() if not racy(x)]
if lp != ln:
    problems.append("listener.log differs:\n  " + "\n  ".join(
        f"{a!r} | {b!r}" for a, b in zip(lp, ln) if a != b))
# store tree with uids mapped
def store_tree(d, st):
    names = tree(d / "store")
    for k, t in enumerate(st["tiles"]):
        if t.get("uid"):
            names = [n.replace(t["uid"], f"UID{k}") for n in names]
    return sorted(names)
if store_tree(py, sp) != store_tree(cpp, sn):
    problems.append(f"store trees differ: {store_tree(py, sp)} vs {store_tree(cpp, sn)}")
pbp, pbn = py / "store" / "passband", cpp / "store" / "passband"
if tree(pbp) != tree(pbn):
    problems.append(f"passband trees differ: {tree(pbp)} vs {tree(pbn)}")
for f in tree(pbp):
    if f.endswith("/") or f == ".lock" or not (pbn / f).exists():
        continue
    if f.endswith(".flac"):
        import soundfile
        a, fa = soundfile.read(str(pbp / f), dtype="int16", always_2d=True)
        b, fb = soundfile.read(str(pbn / f), dtype="int16", always_2d=True)
        same = fa == fb and a.shape == b.shape and np.array_equal(a, b)
        print(f"passband {f}: {a.shape[0]} samples, equal {same}; {(pbp / f).stat().st_size} bytes (soundfile/libFLAC) "
              f"vs {(pbn / f).stat().st_size} bytes (C++ encoder); raw would be {a.size * 2} bytes")
        if not same:
            problems.append(f"{f} samples differ")
    elif (pbp / f).read_bytes() != (pbn / f).read_bytes():
        problems.append(f"passband {f} bytes differ")


def du(p, apparent=False):
    a = ["du", "-sk"] + (["--apparent-size"] if apparent else []) + [str(p)]
    return int(subprocess.run(a, capture_output=True, text=True).stdout.split()[0])


for name, d in (("py", py), ("cpp", cpp)):
    print(f"{name}: passband {du(d / 'store' / 'passband')} kB on disk ({du(d / 'store' / 'passband', True)} kB apparent); "
          f"store total {du(d / 'store')} kB; state {du(d / 'state')} kB; files: "
          + ", ".join(f"{p.name} {p.stat().st_size}" for p in sorted((d / 'store' / 'passband').iterdir())))
from PIL import Image
for t in tp:
    if t.get("image"):
        a = np.asarray(Image.open(py / "state" / t["image"]), dtype=np.float64)
        b = np.asarray(Image.open(cpp / "state" / t["image"]), dtype=np.float64)
        mse = float(np.mean((a - b) ** 2))
        ps = math.inf if mse == 0 else 10 * math.log10(255.0 ** 2 / mse)
        print(f"tile {t['id']} picture: PSNR {ps:.1f} dB C++ vs Python, {int((a != b).sum())} subpixels differ")
        if ps <= 30:
            problems.append(f"tile {t['id']} picture PSNR {ps:.1f}")
print("AGREE" if not problems else "PROBLEMS:\n" + "\n".join(problems))
