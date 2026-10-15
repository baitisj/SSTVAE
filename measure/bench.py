#!/usr/bin/env python3
"""bench.py NAME IMPL [--runs N] [--cwd DIR] [--setup SHELL] [--max-runs-if-slow 1] -- cmd...

Runs cmd under ./mtime (wall, user, sys, ru_maxrss, sampled tree peak) and
`timeout 200`, N times (median of 3 by default; a first run over 30 s stops
at one), sequentially, with SHELL run (untimed) before each run. Appends one
JSON line per run to results.jsonl and keeps each run's stdout/stderr under
logs/NAME.IMPL.<k>.{out,err}. Prints a one-line summary.
"""
import json
import os
import shlex
import statistics
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main():
    argv = sys.argv[1:]
    i = argv.index("--")
    opts, cmd = argv[:i], argv[i + 1:]
    name, impl = opts[0], opts[1]
    runs, cwd, setup, ok_rc = 3, None, None, 0
    k = 2
    while k < len(opts):
        if opts[k] == "--runs":
            runs = int(opts[k + 1]); k += 2
        elif opts[k] == "--cwd":
            cwd = opts[k + 1]; k += 2
        elif opts[k] == "--ok-rc":
            ok_rc = int(opts[k + 1]); k += 2
        elif opts[k] == "--setup":
            setup = opts[k + 1]; k += 2
        else:
            raise SystemExit(f"unknown option {opts[k]}")
    logs = HERE / "logs"
    logs.mkdir(exist_ok=True)
    rows = []
    for r in range(runs):
        if setup:
            subprocess.run(setup, shell=True, check=True)
        ru = HERE / f".ru.{os.getpid()}"
        out = logs / f"{name}.{impl}.{r}.out"
        err = logs / f"{name}.{impl}.{r}.err"
        with open(out, "wb") as o, open(err, "wb") as e:
            p = subprocess.run([str(HERE / "mtime"), str(ru), "timeout", "200", *cmd], cwd=cwd, stdout=o, stderr=e,
                               stdin=subprocess.DEVNULL)
        wall, user, sys_, maxrss, tree, rc = ru.read_text().split()
        ru.unlink()
        row = dict(name=name, impl=impl, run=r, wall=float(wall), user=float(user), sys=float(sys_),
                   cpu=float(user) + float(sys_), maxrss_mb=int(maxrss) / 1024, tree_peak_mb=int(tree) / 1024,
                   rc=int(rc), cmd=" ".join(shlex.quote(c) for c in cmd), cwd=cwd)
        rows.append(row)
        with open(HERE / "results.jsonl", "a") as f:
            f.write(json.dumps(row) + "\n")
        print(f"  run {r}: {row['wall']:.2f} s wall, {row['cpu']:.2f} s CPU, {row['maxrss_mb']:.0f} MB maxrss, "
              f"{row['tree_peak_mb']:.0f} MB tree peak, rc {row['rc']}", flush=True)
        if row["wall"] > 30 or row["rc"] != ok_rc:
            break
    med = {k: statistics.median(r[k] for r in rows) for k in ("wall", "cpu", "maxrss_mb", "tree_peak_mb")}
    print(f"{name} {impl}: median of {len(rows)}: {med['wall']:.2f} s wall, {med['cpu']:.2f} s CPU, "
          f"{med['maxrss_mb']:.0f} MB peak RSS ({med['tree_peak_mb']:.0f} MB tree), rc {[r['rc'] for r in rows]}")


if __name__ == "__main__":
    main()
