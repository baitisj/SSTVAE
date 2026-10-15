#!/usr/bin/env python3
"""Append wave 6b's entries to port-insights.md, each under its section."""
import sys

P = "/mnt/project-files/qrss-build/port-insights.md"

ADD = {
    "## 2. Performance (measured)": """
- **Wave 6b, the whole receiver, C++ against Python** (4 shared CPUs, one run each):
  - receive_pass: TINY about 3x, SHORT 3.5-5x, l-short 3.8x; FULL -12 dB with a verified detection CE 4.5 s against 24-27 s (5-6x) and L 8.3-9.0 s against 26-29 s (3x). Gate V alone is 16.4 s (CE) and 18.6 s (L) of Python per FULL pass, about 40% of a pass handed an unverified detection, which is what EM and the live listener hand it.
  - receive_slot: SHORT CE 1.6 s against 4.0 s, SHORT joint 2.8 against 7.3 s, FULL CE 9.1 against 32.4 s (3.5x), FULL joint 16.3 against 51.6 s (3.2x). On FULL CE the split is candidates 1.3 against 3.1 s, gate 1.8 against 5.8 s, passes 5.4 against 22.0 s: the per-pass tracker dominates both, the orchestration does not.
  - `sstvae-qrss receive` against `qrss_receive.py --no-store`, wall and peak RSS: SHORT CE 1.9 s / 86 MB against 6.8 s / 204 MB; SHORT L 2.7 s / 86 MB against 6.0 s / 208 MB; FULL CE with --image 9.8 s / 508 MB against 34.4 s / 667 MB; FULL L with --image 12.5 s / 519 MB against 31.6 s / 702 MB. This is what a release build does.
  - The L path gains least (3x against CE's 5-6x) because ltrack's FFT filters and Kalman passes are already compiled code in Python; the fit_grid_l halving above is the lever.
  - The FULL .npz reader holds the file buffer and a copy of fe at once (~123 MB transient for 57 MB of fe); a move or mmap would halve it.
""",
    "## 1. The Python reference: latent bugs and fragile spots": """
- **Wave 6b latent spots (receiver, receive CLI):**
  - `PassResult.cw.agrees` can be `np.bool_` (from `z > Z_ACCEPT` on an np.float64): equal to a bool, but `json.dumps` rejects it, so any sidecar writer that serialises it raw will fail.
  - `_receive_pass_l` does not check its frame's waveform (only `receive_pass` does), so a direct caller with a CE frame gets a nonsense L pass.
  - `_suspect_neighbour` never fires on any simulated signal, including the weak-neighbour and alias captures built to provoke it; only the stubbed fuzz reaches it. Either it guards a field case the simulator cannot make or it is dead: a recorded capture should decide before anyone relies on it.
  - `qrss_receive.py` with a slot before 1970 (q < 0) acquires and tracks normally, then dies with an uncaught ValueError from `sequences.scrambler` at the first demodulation. (The C++ first did worse: it descrambled with 2^64 + q and printed a pass; it now refuses as Python does, inside `receive_pass`.) Refusing in `parse_slot` would retire both.
  - `qrss_receive.py` checks `--image` only after the whole receive: a missing directory or a name Pillow cannot map raises after up to ~35 s of FULL receive, and the result is lost. `--slot ''` counts as absent and `--start ''` is silently ignored (`if args.slot`).
  - `load_npz` trusts the archive: fs = 0 reaches a ZeroDivisionError, a 2-D fe fails inside sliding_window_view with an error naming neither file nor member, and t0_index NaN prints a RuntimeWarning and finds nothing.
  - scipy.io.wavfile (under `wavio.read_wav`) misreads 8-bit PCM in a wider container (it reads count = n_samples single bytes, so returns the first half of the raw bytes and misaligns the chunk walk); misaligns after any data chunk whose size is not a multiple of the container; and refuses a WAV whose RIFF size is 0 or too small (what a crashed streaming recorder leaves) with an UnboundLocalError about `fs`. The C++ reproduces all three on purpose, since the reference is whatever scipy returns. A recorder that crashed mid-slot therefore cannot be received by either; patching the header size first would rescue it.
""",
    "## 3. Simplifications and design": """
- **receive_pass and _receive_pass_l fold into one function in C++ (wave 6b):** they share prepare, gate V, channel_for, the round A/B sequence, the header and cw decisions and the PassResult assembly; only the tracker and the header decoder differ, and both are a dispatch on the waveform. Python could do the same.
- **One slot orchestration template over two policies (wave 6b):** typed records with the GIL released, and py::object with every stage resolved through `receiver`'s globals per call. Both give bitwise-equal results, there is one copy of the budget, merge and gating logic, and spies and monkeypatches see exactly the reference's call sequence. The typed/Python switch identifies rows by where they are defined, never by the `__qrss_native__` mark, because spies copy that mark.
- **The C++ receive API takes the 2026-10-12 rules per call** (`ReceiveConfig.old_edge_noise`, `old_gating`) where Python reads module switches (`rules.old`): thread-safe, and the listener can run two configurations side by side.
- **simfile walks the zip itself** only because npz.hpp cannot read a 0-d `<U` member; with str support in npz.hpp about 120 lines go away. `pathlib_str` exists twice (CLI and codec glue) because the codec glue is not linked in no-codec builds; it belongs in sstvae_qrss proper. argparse.hpp lacks mutually exclusive groups (receive.cpp emulates `--just-ce | --just-l`).
- **A typed q would retire a whole class:** q is int64 in the records and uint64 in the scrambler; every boundary between them needs a check (the CLI and receive_pass both have one now).
""",
    "## 4. Portability and numerics": """
- **Erased passes amplify tracker rounding about 10x (wave 6b).** z_ref over an erased stretch is extrapolated through the Kalman P, so a 1.8e-8 difference in u becomes 4.9e-5 in P and 1.06e-6 in z_ref; unerased passes stay near 1e-7. Bars on erased z_ref need ~1e-5.
- **z's elementwise bar is the tight one:** 4.1e-7 (L FULL) and 5.7e-7 (SHORT -12) against 1e-6, i.e. 5-10 float32 ulps on single small latents after a long chain, while the scale-relative error is 2-3e-8. If a compiler or FMA change (aarch64) crosses the elementwise bar, relax it rather than chase it; the scale-relative bar and the decoded picture are the contract. timing.cov's 1e-4 bar is four orders looser than anything measured (2.4e-10).
- **Picture-level parity holds where the release bar needs it:** FULL `--image` differs from Python's PNG by one level on 18 (CE) and 22 (L) of 921,600 subpixels, all from z rounding: Python's render_pass on the C++ pass gives the C++ PNG exactly. The uid hash (SHA-256 of float32 z) is a cheap bitwise-z detector.
- **numpy's channel mean over a WAV is a per-row pairwise sum** once there are 8 or more channels; read_wav mirrors numpy's pairwise blocks to stay bitwise. The NaN payload of a frame holding several NaNs follows the compiler's operand order (x86 addsd keeps the first operand's), so it is a property of numpy's build.
- **Narrow paths:** the QRSS core opens files through `fopen(std::string)` (wavread, wav, api, npz, beaconfile), which on MSVC reads a non-ASCII path through the ANSI code page; the bindings encode a str path with surrogatepass where Python's open uses os.fsencode. A project-wide convention (UTF-8 to `_wfopen`/`std::filesystem::path`, `PyUnicode_EncodeFSDefault`) is due before a Windows release.
- **`frame::py_format` mangled field widths** ("%9.3f" gave ".1500.017": every non-alphanumeric byte run became the decimal point) and printed "-nan"; fixed at the merge, with a ctest. **`std::stoul` on a numpy `<U<n>` width** is 32-bit on MSVC and `4 * width` wrapped on 64-bit; read with an overflow-checked loop.
""",
    "## 5. How the port went": """
- **Wave 6b (receiver per-pass half, slot half + receive API, `sstvae-qrss receive`): merged with no conflicts.** An accidental stop killed the first run mid-tests; the worktree's uncommitted work was committed as WIP and a resumed agent finished it, which is the third time "commit early" paid off. The per-pass review found no C++ bug (the swap test pinned every difference to primitives measured elsewhere). The slot review found the one big gap: the WAV reader matched scipy only on well-formed files from one writer, and a grammar fuzz of chunk orders, lying sizes and truncations (3000 files) drove a rewrite modelling scipy's walk. The CLI review found q < 0 and an overflow in the npz str width. The pattern holds: arithmetic is clean, edges and I/O are where the bugs are.
- **A typed run that silently falls back proves nothing.** The slot's scenario tests first checked only the flag the rows hand the binding; a spy on the per-pass bindings now proves typed runs stay typed (0 calls) and Python-mode runs reach them.
- **Constants exported as rows are a hazard** for any harness that wraps every row in a spy (`live._plausible` reads `receiver.GHOST_HZ`).
- **Test-tier cost is now the constraint.** The receiver parity file's fast tier first ran 93-107 s (each SHORT reference receive_pass is 4 s of Python); caching captures and moving pure-reference SHORT passes to slow brought it to 43 s. The slot file's fast tier is ~83 s and the CLI file's ~35 s (run in a 4-way pool). `test_qrss_receiver.py` has only 4 fast tests (55 slow) and the acceptance file none, so `--native-qrss=rx -m 'not slow'` barely reaches the native receiver end to end; the parity files and chosen slow node ids do.
- **A CLI wave needs CMakeLists.txt named as editable:** codec-only CLI sources sit outside the glob's filter, so `--image` was verified with a temporary hook and wired in at the merge. And each finished subcommand breaks `test_dispatch`'s not-built pin; deriving that set from main.cpp's table would stop it.
- **zipfile.writestr stamps local time** (np.savez's `open(name, 'w')` stamps 1980-01-01), so a hand-built archive in a pin generator made the committed pins nondeterministic until switched.
""",
}

text = open(P).read()
for head, entry in ADD.items():
    i = text.index(head)
    j = text.find("\n## ", i + len(head))
    if j < 0:
        j = len(text)
    body = text[:j].rstrip("\n") + "\n" + entry.rstrip("\n") + "\n\n"
    text = body + text[j:].lstrip("\n")
open(P, "w").write(text)
print("ok", len(text))
