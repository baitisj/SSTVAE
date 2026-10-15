#!/usr/bin/env python3
"""Append wave 7's entries to port-insights.md, each under its section."""

P = "/mnt/project-files/qrss-build/port-insights.md"

ADD = {
    "## 1. The Python reference: latent bugs and fragile spots": """
- **Wave 7 reference fixes (all mirrored in C++, pushed c0c830f):**
  - Pre-1970 slots now refuse in `parse_slot` ("slot <iso> is before 1970-01-01T00:00Z"), so `qrss_receive.py` no longer acquires and tracks for ~35 s before dying in `sequences.scrambler`. `qrss_listen.py` turns a bad `--slot`/`--start` into a SystemExit naming the option.
  - `qrss_receive.py --image` is checked before receiving: a missing directory, and an extension Pillow can read but not write (.psd, .xpm, .gbr), used to fail after the whole receive.
  - `load_npz` refuses fs <= 0, a q before 1970 and a wrongly shaped fe up front. The first version of that check refused '>c8' (complex64 in the other byte order) with a message that contradicted itself; the review caught it and fe is now normalised to native order.
  - `_receive_pass_l` given a CE frame now gives `receive_pass`'s refusal instead of "AttributeError: 'NoneType' object has no attribute 'fs'".
  - Non-finite frequency hints are dropped in acquire rather than propagated.
  - `cw.agrees` is wrapped in `bool()`. The premise (an np.bool_ breaking json.dumps) was false, since `cwid.match` has returned a Python float since wave 4; the review found the agent's test could not fail and rewrote it to stub `match` with np.float64.
- **Still in the reference, pinned rather than changed:**
  - verify and verify_l track `best + tries[1:]`, assuming `tries[0]` is the best timing. On L noise it is not (a carrier estimate's runner-up outscores every first peak after segment refinement), so the best is tracked twice and the strongest runner-up never. Worth a look in the reference.
  - `make_classes` broadcasts a length-1, scalar or (1, n) header on -v1 frames instead of refusing it, so `known_symbols` accepts wrong-length headers in both implementations.
  - `TBD_GUMBEL_MU[0] = 9.845` does not round the reference's own measurement (9.8443 from 2,400 Z, Python and C++ alike). Harmless at 0.0007.
  - `receiver.capture_from_wav` and `receive_wav` call `str(path)`, so a bytes path becomes the literal file name "b'...'"; the C++ matches on purpose.
  - The reference codec cannot load a model from a directory whose name is not UTF-8 (onnxruntime's InferenceSession takes a strict str); the C++ can.
  - `qrss_transmit.py` writes its WAV and then dies with UnicodeEncodeError printing "wrote <name>" for a non-UTF-8 name under en_US.UTF-8 (stdout is strict there; only UTF-8 mode or the C/POSIX locales make it surrogateescape).
- **A-7 recheck on the C++ statistic (no constant moves):** the C++ A3 statistic is bit-identical to Python's, and TBD_GUMBEL reproduces within 0.0007 (mu) and 0.0003 (beta). Three calibration facts worth knowing:
  - A3's Gumbel-by-moments fit is conservative in the far tail: exceedance at the committed 1% point is 0.08-0.50 of nominal over 200 captures, and 0 of 3,694 noise channels reached p < 1e-3 over 100 slots. A3's real false-alarm rate is perhaps 3-10x below nominal, which costs sensitivity, not safety.
  - A1's frozen threshold is mildly anti-conservative at 1e-4 (1.44x nominal; 1.20x at 1e-3), inside the reference test's factor of 2. A2 is conservative (0.66-0.96 of nominal per scale).
  - False A2 hits do happen with joint CE+L listening: 1 in 100 noise slots (p 4.94e-8 against A2_PFA 1e-7), identical on both sides, refused by gate V. The end-to-end false-pass rate rests on gate V and the header, not on A2's PFA.
""",
    "## 2. Performance (measured)": """
- **Wave 7, gate V's coarse timing grid computed once** (verify/verify_l ran `fit_timing` with n_best=1 and again with n_best=V on the same grid). Noise candidate, one thread:
  - Python gate V: CE FULL 73.3 -> 59.3 s, L FULL 62.3 -> 51.4 s, CE SHORT 4.6 -> 4.1 s, L SHORT 2.6 -> 2.3 s. This is what Hamlet's Python listener gains once pulled: about 11-14 s per FULL candidate that reaches gate V.
  - C++: saves 1.1 s (CE FULL, 12%) and 1.3 s (L FULL, 16%); gate V on noise is now 8.3 s (CE FULL) and 7.0-7.2 s (L FULL), 7x faster than Python.
  - Trade-off: a blind candidate that passes on its best timing now pays for the runners-up's segment refinement up front (C++ CE SHORT 0.07 -> 0.10 s). A lazy runner-up extraction (keep each carrier estimate's Z, Zl and x and pull k >= 1 peaks on demand, in carrier-estimate order) would make it free. Not done.
  - Caching `TbdHit.f_hz` saves under 1 ms per acquire (spline fits 13 -> 8); the "thousands of reads" premise came from the test-only all_hits mode.
- **Whole-slot receive_slot, C++ against Python on the 13 acceptance scenarios** (budget_s=None, conversions included): 2.0-3.0x. Per scenario, Python / C++: ce-tiny-6 2.0 / 0.86 s, l-tiny-12 3.1 / 1.13, ce-short-12 4.6 / 1.54, l-short-12-joint 4.1 / 2.03, ce-l-neighbours 9.4 / 3.11. Least on the joint L slot, most on CE SHORT and the neighbour slot.
- **One noise slot, SHORT + L_SHORT joint:** receive_slot 1.45 s C++ against 3.51 s Python; acquire is 1.24 of the 1.45 s and costs the same on a TINY slot as a SHORT one, so acquire's length-independent part is where the next C++ speedup is. Inside the calibration captures CFAR is now the dominant C++ stage (1.1x over numpy: a per-frame median over 2000 bins), with Viterbi 9.4x and the spectrogram 1.3x.
- **CLI receive, C++ against the script:** TINY .npz 1.4 s vs 2.9 s; TINY WAV 3.5 s vs 8.4 s; a 4 s noise WAV 0.025 s vs 1.1 s (interpreter start-up).
""",
    "## 3. Simplifications and design": """
- **Mutually exclusive groups in argparse.hpp (wave 7)** replaced about 120 lines of receive.cpp emulation (usage rewriting, abbreviation matching, error order) with one `exclusive()` call, raising the conflict where CPython's take_action does.
- **verify_track(fit=False, timing=best) equals verify_track(fit=True) on the blind line try**, so one timing grid serves both; the C++ factoring (timing_peaks -> timings_of / tries_of) leaves fit_timing and fit_timing_tries as two-line wrappers.
- **One path convention, three call-site rules (wave 7).** `util/fileio.hpp` is the only file opener (UTF-8 to `_wfopen` on Windows, through its own WTF-8 converter because MultiByteToWideChar maps a lone surrogate to U+FFFD) and `pypath` holds the scripts' spellings. On POSIX one UTF-8 convention cannot serve both directions: bindings need surrogatepass (a str's lone surrogate becomes 3 bytes) while argv and file names use surrogateescape (those 3 bytes are 3 escapes). So: `fs_decode` for a message's repr, `fs_str` for a parser handed argv, raw bytes for opening and verbatim printing. `stderr_text` and `fs_decode` share one decoder so they cannot disagree.
- **A receive_slot binding that returned its Acquisition** would halve the calibration noise-slot tests, which currently re-run acquisition in Python to count A2/A3 hits.
- **SSTVAE's own narrow-path sites remain** (images.cpp ifstream and stbi_write_png, codec.cpp:87 and grad_session.cpp:31 byte-wise wstring, checkpoint.cpp narrow fs::path and getenv). They are outside the QRSS port's files (design rule 3), so they are left for SSTVAE's owner; a non-ASCII model or picture path on Windows goes through the ANSI code page there.
""",
    "## 4. Portability and numerics": """
- **The receive chain agrees to rounding end to end (wave 7 acceptance, 13 scenarios):** f_hz within 2.5e-12 Hz, tau0 within 6.6e-12 CH samples, latent SNR within 2.4e-10 dB, mean W bit for bit, Z_ref within 2.1e-8 relative on CE passes (1e-13 on L, suggesting a CE-side reduction order). Every acceptance bar is at least 1e7 times wider than this machine needs; they are sized for another platform or a later C++ change.
- **A calibration check by moments has teeth without a bitwise oracle:** a 1% scale error in the CFAR normalisation moves mean Z by 6-16 standard errors in a 470-capture Python-free sample; a 0.1% error (1.6 standard errors) would need the bitwise comparison.
- **Python's sys.stderr is always backslashreplace**, whatever the locale or PYTHONIOENCODING, while stdout's handler depends on the locale (surrogateescape in UTF-8 mode and under C, POSIX or C.UTF-8; strict under en_US.UTF-8). This sandbox runs POSIX, so a local measurement can overstate what a desktop does. The CLI filters its stderr to match.
- **`datetime.fromisoformat` takes any single code point as the date/time separator**, so a C++ that decoded argv's ED A0 80 as one surrogate accepted a --slot the script refuses. Any parser handed argv must see the str sys.argv holds.
- **A Windows-only decoder branch can be exercised on Linux** by compiling a copy with the `#ifdef _WIN32` forced on and comparing against Python-generated pins; that was the only execution of the surrogatepass arithmetic before a Windows CI job.
- **Pin hashes must normalise CRLF** (.gitattributes has no eol rule for *.py, so Git for Windows checks them out CRLF), and repo paths compared as strings must be built '/'-separated; the acceptance file would otherwise have failed only on Windows, where native-build CI runs it.
""",
    "## 5. How the port went": """
- **Wave 7 (reference fixes, statistical acceptance, A-7 recheck, paths and CLI clean-ups): merged with no conflicts; stage 2 is done.** The receiver's exit criteria all hold: `--native-qrss=rx` fast tier green on every QRSS suite, the reverse chain (Python transmits, C++ receives) tested, A-7 rechecked with no constant moving, and statistical acceptance against 13 committed Python reference summaries with hash pins on 30+ Python sources.
- **Reviews kept finding tests that could not fail** (the cw.agrees JSON test, a runners-up test that counted candidates but not which timings, a CRLF test that re-implemented the normalisation it claimed to cover). Mutation-checking each new test is the habit that caught all three.
- **A hand-written pin list goes stale silently.** A static import closure from receiver.py, channel.py and render.py found four modules the hand list missed, and the review then found it could not see `importlib.import_module`. `test_pins_cover_the_imports` now fails when a new import is neither pinned nor excused. Pinning live.py means a listener-only edit (stage 3) trips "regenerate references" even when they would not change; regeneration is about a minute and byte-identical when nothing moved.
- **A refusal moved earlier in a script breaks every parity test that reached a later refusal through that script** (test_qrss_native_api.py's q = -1 cases). Grep for every test reaching the old refusal before calling it done.
- **Timings taken while other agents share the CPUs were off by up to 4x**; only pinned, repeated runs (OMP/BLAS threads 1, first run discarded) gave usable before/after numbers. A slow test run by node id without `-m slow` is silently deselected (exit 0).
- **Test-tier cost:** receiver_slot's fast tier went 104 -> 33 s by keeping one typed CE, L and joint scenario plus the whole-caller test. The parity files together now run about 12 minutes of fast tier on 4 CPUs, so they are run in groups.
- **The shared project folder went read-failing (rclone I/O errors) during the merge**, which failed `test_morse_table_matches_the_spec_sim` (it reads the spec's sims) in both runs; nothing else in the suite depends on it.
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
