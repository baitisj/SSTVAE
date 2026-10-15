"""Installed size of a requirement closure in this environment (dist RECORD files)."""
import os, re, sys
from importlib import metadata
from packaging.requirements import Requirement

def closure(roots):
    seen, todo = {}, list(roots)
    while todo:
        name = todo.pop()
        key = re.sub(r"[-_.]+", "-", name).lower()
        if key in seen:
            continue
        try:
            d = metadata.distribution(name)
        except metadata.PackageNotFoundError:
            seen[key] = None
            continue
        seen[key] = d
        for r in d.requires or []:
            req = Requirement(r)
            if req.marker and not req.marker.evaluate({"extra": ""}):
                continue
            todo.append(req.name)
    return seen

def size(d):
    tot = 0
    for f in d.files or []:
        p = d.locate_file(f)
        try:
            tot += os.path.getsize(p)
        except OSError:
            pass
    return tot

for label, roots in (("base (numpy scipy pillow)", ["numpy", "scipy", "pillow"]),
                     ("cli extras (+ onnxruntime huggingface_hub)", ["numpy", "scipy", "pillow", "onnxruntime", "huggingface_hub"]),
                     ("cli + soundfile (QRSS passband FLAC)", ["numpy", "scipy", "pillow", "onnxruntime", "huggingface_hub", "soundfile"])):
    c = closure(roots)
    tot = sum(size(d) for d in c.values() if d)
    missing = [k for k, d in c.items() if d is None]
    print(f"{label}: {len(c)} dists, {tot/2**20:.1f} MiB" + (f" (not installed: {missing})" if missing else ""))
    if label.startswith("cli + soundfile"):
        for k, d in sorted(c.items(), key=lambda kv: -(size(kv[1]) if kv[1] else 0)):
            if d:
                print(f"   {k} {d.version}: {size(d)/2**20:.1f} MiB")
