"""cmp_store.py A B: two store trees, file by file: the same names, .json equal,
.npz arrays equal or their worst difference."""
import json, sys
from pathlib import Path
import numpy as np
a, b = Path(sys.argv[1]), Path(sys.argv[2])
ta = sorted(p.relative_to(a).as_posix() for p in a.rglob("*") if p.is_file())
tb = sorted(p.relative_to(b).as_posix() for p in b.rglob("*") if p.is_file())
print("same file names:", ta == tb, "" if ta == tb else (sorted(set(ta) ^ set(tb))))
for f in sorted(set(ta) & set(tb)):
    pa, pb = a / f, b / f
    if pa.read_bytes() == pb.read_bytes():
        continue
    if f.endswith(".npz"):
        za, zb = np.load(pa, allow_pickle=False), np.load(pb, allow_pickle=False)
        diffs = []
        for k in sorted(set(za.files) | set(zb.files)):
            if k not in za.files or k not in zb.files:
                diffs.append(f"{k}: one side only"); continue
            x, y = za[k], zb[k]
            if x.shape != y.shape or x.dtype != y.dtype:
                diffs.append(f"{k}: {x.shape}/{x.dtype} vs {y.shape}/{y.dtype}"); continue
            if np.array_equal(x, y):
                continue
            if np.issubdtype(x.dtype, np.number):
                d = np.abs(x.astype(np.complex128) - y.astype(np.complex128))
                diffs.append(f"{k}: max |d| {d.max():.3g} (scale {np.abs(x).max():.3g})")
            else:
                diffs.append(f"{k}: differs ({x.dtype})")
        print(f"{f}: bytes differ; arrays: " + ("; ".join(diffs) if diffs else "all equal (container bytes differ)"))
    elif f.endswith(".json"):
        ja, jb = json.loads(pa.read_text()), json.loads(pb.read_text())
        print(f"{f}: json {'equal as data' if ja == jb else 'DIFFERS'}")
        if ja != jb:
            for k in sorted(set(ja) | set(jb)) if isinstance(ja, dict) else []:
                if ja.get(k) != jb.get(k):
                    print(f"    {k}: {str(ja.get(k))[:120]} | {str(jb.get(k))[:120]}")
    else:
        print(f"{f}: bytes differ")
