"""loc.py: lines of code of the QRSS reference and its C++ port, by group.

Physical lines (wc -l) and code lines: blank lines and comment-only lines
dropped; Python docstrings count as comments (as cloc counts them by default),
C++ // and /* */ comments are comments. Run from the repository root."""
import io
import subprocess
import sys
import tokenize
from pathlib import Path

ROOT = Path("/home/user/sstvae")
FIRST = "3843897"   # "QRSS C++ port, stage 0"


def py_counts(path: Path):
    src = path.read_text(encoding="utf-8")
    lines = src.splitlines()
    comment_or_doc = set()
    code = set()
    try:
        toks = list(tokenize.generate_tokens(io.StringIO(src).readline))
    except (tokenize.TokenError, SyntaxError):
        toks = []
    prev = None
    for t in toks:
        if t.type == tokenize.COMMENT:
            comment_or_doc.add(t.start[0])
        elif t.type == tokenize.STRING and (prev is None or prev.type in (tokenize.INDENT, tokenize.DEDENT,
                                                                            tokenize.NEWLINE, tokenize.NL)):
            # a bare string statement: a docstring
            for ln in range(t.start[0], t.end[0] + 1):
                comment_or_doc.add(ln)
        elif t.type not in (tokenize.NL, tokenize.NEWLINE, tokenize.INDENT, tokenize.DEDENT,
                            tokenize.ENDMARKER, tokenize.ENCODING):
            for ln in range(t.start[0], t.end[0] + 1):
                code.add(ln)
        if t.type not in (tokenize.COMMENT, tokenize.NL):
            prev = t
    n = len(lines)
    return n, len(code)


def cpp_counts(path: Path):
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    in_block = False
    code = 0
    for line in lines:
        s = line
        has_code = False
        i = 0
        in_str = None
        while i < len(s):
            c = s[i]
            if in_block:
                if s.startswith("*/", i):
                    in_block = False
                    i += 2
                    continue
                i += 1
                continue
            if in_str:
                if c == "\\":
                    i += 2
                    continue
                if c == in_str:
                    in_str = None
                i += 1
                continue
            if s.startswith("//", i):
                break
            if s.startswith("/*", i):
                in_block = True
                i += 2
                continue
            if c in "\"'":
                in_str = c
            if not c.isspace():
                has_code = True
            i += 1
        code += has_code
    return len(lines), code


def count(files):
    n = c = 0
    for f in files:
        p = ROOT / f
        if p.suffix == ".py":
            a, b = py_counts(p)
        else:
            a, b = cpp_counts(p)
        n += a
        c += b
    return len(files), n, c


def git_added(*paths):
    out = subprocess.run(["git", "diff", "--name-only", "--diff-filter=A", f"{FIRST}~1", "HEAD", "--", *paths],
                         cwd=ROOT, capture_output=True, text=True, check=True).stdout.split()
    return [f for f in out if (ROOT / f).is_file()]


def glob(pattern):
    return sorted(str(p.relative_to(ROOT)) for p in ROOT.glob(pattern) if p.is_file())


GENERATED = {"native/core/qrss/config.hpp", "native/core/qrss/rx_config.hpp", "native/core/qrss/pyunicode_tables.hpp",
             "native/core/qrss/tables.cpp", "native/core/qrss/tables.hpp"}
src = lambda fs: [f for f in fs if f.endswith((".cpp", ".hpp", ".h"))]
core_qrss = src(glob("native/core/qrss/*.*"))
groups = [
    ("Python reference: sstvae/qrss/*.py", glob("sstvae/qrss/*.py")),
    ("Python reference: qrss_*.py scripts", glob("qrss_*.py")),
    ("C++ native/core/qrss (hand-written)", [f for f in core_qrss if f not in GENERATED]),
    ("C++ native/core/qrss (generated tables/config)", [f for f in core_qrss if f in GENERATED]),
    ("C++ native/core/qrss/codec", src(glob("native/core/qrss/codec/*.*"))),
    ("C++ native/core/math (added by the port)", src(git_added("native/core/math"))),
    ("C++ native/core/util (added by the port)", src(git_added("native/core/util"))),
    ("C++ native/apps/sstvae_qrss (+codec)", src(glob("native/apps/sstvae_qrss/**/*.*"))),
    ("C++ bindings native/bindings/module/qrss (+codec)", src(glob("native/bindings/module/qrss/**/*.*"))),
    ("C++ ctests native/tests/qrss (+codec, cli)", src(glob("native/tests/qrss/**/*.*"))),
    ("vendored third_party/dr_libs/dr_flac.h", ["native/third_party/dr_libs/dr_flac.h"]),
    ("Python parity tests tests/test_qrss_native_*.py", glob("tests/test_qrss_native_*.py")),
    ("Python parity rows/helpers tests/qrss_native/*.py", glob("tests/qrss_native/*.py")),
    ("Python generators tools/gen_qrss_*.py", glob("tools/gen_qrss_*.py")),
]
print(f"{'group':58s} {'files':>5s} {'lines':>8s} {'code':>8s}")
tot = {}
for name, files in groups:
    k, n, c = count(files)
    tot[name] = (k, n, c)
    print(f"{name:58s} {k:5d} {n:8d} {c:8d}")
ref = [g for g in tot if g.startswith("Python reference")]
port = [g for g in tot if g.startswith("C++ native/core") or g.startswith("C++ native/apps")]
for label, keys in (("reference total (sstvae/qrss + scripts)", ref),
                    ("port total, shipped C++ (core/qrss hand-written + codec + math + util + app)",
                     [g for g in port if "generated" not in g])):
    k = sum(tot[g][0] for g in keys)
    n = sum(tot[g][1] for g in keys)
    c = sum(tot[g][2] for g in keys)
    print(f"{label}: {k} files, {n} lines, {c} code lines")
