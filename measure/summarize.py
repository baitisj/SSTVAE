import json, statistics
from collections import defaultdict
rows = [json.loads(l) for l in open("results.jsonl")]
g = defaultdict(list)
for r in rows:
    g[(r["name"], r["impl"])].append(r)
names = []
for r in rows:
    if r["name"] not in names:
        names.append(r["name"])
print(f"| case | Python wall / CPU / peak RSS (tree) | C++ wall / CPU / peak RSS (tree) | wall ratio | CPU ratio | N py/cpp |")
print("|---|---|---|---|---|---|")
for n in names:
    m = {}
    for impl in ("py", "cpp"):
        rs = g.get((n, impl))
        if not rs:
            continue
        m[impl] = {k: statistics.median(r[k] for r in rs) for k in ("wall", "cpu", "maxrss_mb", "tree_peak_mb")}
        m[impl]["n"] = len(rs)
        m[impl]["rc"] = sorted({r["rc"] for r in rs})
    f = lambda d: f"{d['wall']:.2f} s / {d['cpu']:.2f} s / {d['maxrss_mb']:.0f} MB ({d['tree_peak_mb']:.0f})" + ("" if d["rc"] in ([0],) else f" rc {d['rc']}")
    py, cpp = m.get("py"), m.get("cpp")
    ratio = f"{py['wall'] / cpp['wall']:.1f}x" if py and cpp else "-"
    cr = f"{py['cpu'] / cpp['cpu']:.1f}x" if py and cpp else "-"
    print(f"| {n} | {f(py) if py else '-'} | {f(cpp) if cpp else '-'} | {ratio} | {cr} | {py['n'] if py else 0}/{cpp['n'] if cpp else 0} |")
