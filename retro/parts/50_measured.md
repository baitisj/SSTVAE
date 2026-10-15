## Measured: the C++ against the Python

The C++ (`sstvae-qrss`) is 1.9 to 5.8 times faster than the Python in wall time on every receive, listen and decode case (receive was timed with --no-store on both sides), and 3.3 to 6.4 times faster in CPU time on receive (measured), most on a blind FULL receive and on two EM rounds (5.8x each). There are two exceptions: an L FULL pass written as an int16 WAV takes 4.94 s against the Python's 4.30 s, and a FULL listen session uses about as much memory as the Python's. The C++ is one binary that links no libpython, 4.8 MB stripped (90 MB unstripped, as the Linux packaging copies it today); with the onnxruntime and Qt the app already ships, it replaces about 330 MB of Python (Size). Every output agreed within its bar (Agreement).

### Speed

How it was measured:
- **Machine and builds:** a shared cloud VM (4 vCPU of a 2.80 GHz Intel Xeon, 16 GB of RAM, Linux 6.18) running one command at a time, at a median 1-minute load of 0.67. The C++ is native/build-qrss-codec/sstvae-qrss built RelWithDebInfo (-O2 -g, -ffp-contract=off, no -march), as the release CI builds it (.github/workflows/native-build.yml:273); the Python is the project .venv. onnxruntime is 1.31.0 in the Python and 1.28.0 in the C++.
- **Timing:** a small C wrapper around `timeout 200`: wall time from CLOCK_MONOTONIC; CPU (user plus sys) and peak RSS (ru_maxrss, the largest single process) from wait4; whole-tree RSS sampled every 50 ms. Memory is in MB of 2^20 bytes, as ru_maxrss reports it, and file sizes are decimal. Runs under 30 s are the median of 3, longer ones a single run.
- **Inputs:** one 640 by 480 picture encoded fp16, mode A (picture ID 7b202aad), sent in the 2026-10-09T06:00Z slot as K1ABC FN42 on 1500 Hz; receive WAVs from qrss_simulate.py's quiet preset; listen inputs from transmit WAVs plus white noise at the stated SNR in 2500 Hz. Both sides read the same files.

Ratio is the Python's wall time divided by the C++'s.

| Case | Python wall | C++ wall | Ratio | Outputs agree? |
|---|---|---|---|---|
| Start-up (import receive modules; `--help`) | 1.00 s | 0.014 s | 72x | n/a |
| Encode PNG to .qrsp | 1.39 s | 0.32 s | 4.4x | byte-identical |
| Transmit CE TINY | 1.31 s | 0.11 s | 12.1x | WAV byte-identical |
| Transmit CE SHORT | 1.66 s | 0.39 s | 4.2x | WAV byte-identical |
| Transmit CE MEDIUM | 2.50 s | 1.02 s | 2.4x | WAV byte-identical |
| Transmit CE FULL | 5.32 s | 2.80 s | 1.9x | WAV byte-identical |
| Transmit CE FULL, float WAV | 3.50 s | 1.36 s | 2.6x | identical |
| Transmit L TINY | 1.14 s | 0.19 s | 6.0x | WAV byte-identical |
| Transmit L SHORT | 1.39 s | 0.70 s | 2.0x | WAV byte-identical |
| Transmit L MEDIUM | 2.29 s | 1.83 s | 1.3x | WAV byte-identical |
| **Transmit L FULL** | **4.30 s** | **4.94 s** | **0.87x** | WAV byte-identical |
| Transmit L FULL, float WAV | 3.86 s | 2.58 s | 1.5x | 35 samples differ by about 1 ulp |
| Receive TINY, CE and L at -6 and -16 dB (4 cases) | 2.58 to 3.38 s | 1.07 to 1.69 s | 2.0 to 2.4x | stdout identical |
| Receive SHORT, CE and L at -6 and -24 dB (4 cases) | 6.55 to 7.86 s | 2.37 to 2.99 s | 2.4 to 3.2x | identical, or uid hash only at -24 dB |
| Receive SHORT, nothing found (CE -28 and -32 dB, L -32 dB) | 3.50 to 11.06 s | 1.44 to 3.65 s | 2.4 to 3.2x | rc 1 in both |
| Receive CE FULL -6 dB with --image | 45.04 s | 12.06 s | 3.7x | uid hash only; PNG within 1 level |
| Receive L FULL -6 dB with --image | 43.01 s | 14.99 s | 2.9x | uid hash only; PNG within 1 level |
| Receive CE FULL -32 dB, found blind by A3 | 124.42 s | 21.55 s | 5.8x | uid hash only |
| Receive L FULL -30 dB, nothing found | 63.59 s | 12.40 s | 5.1x | stdout identical |
| Listen replay TINY (CE and L, -10 dB) | 8.04 s | 4.29 s | 1.9x | tiles agree |
| Listen replay SHORT (CE and L, -6 dB) | 24.15 s | 9.15 s | 2.6x | tiles agree; FLAC samples equal |
| Listen replay FULL | over 200 s (killed) | 68.79 s | over 2.9x | C++ picture within 1 level of a Python receive |
| Decode from the store | 1.62 s | 0.57 s | 2.9x | PNG pixel-identical |
| Decode --em 2 | 33.34 s | 5.78 s | 5.8x | stdout identical; PNG pixel-identical |
| Decode --retro --frame short | 7.14 s | 2.60 s | 2.8x | same find; PNG pixel-identical |

What the table says:
- **Receive gains grow with frame length and search effort:** 2.0 to 2.4x on TINY, 2.4 to 3.2x on SHORT, 2.9 to 3.7x on a FULL pass that decodes and 5.1 to 5.8x on a FULL slot searched blind or found empty (measured). The blind and empty FULL slots repeat tracking and timing fits per candidate, where the C++ gains most (inferred from the per-module timings below).
- **Listen gains least on TINY (1.9x)**, where acquisition, a fixed cost per slot that barely gained, dominates (inferred). On FULL the Python's round B over the previous, half-heard slot took 131 s alone, against 68.79 s for the whole C++ replay.
- **Run-to-run spread** was at most 13% for C++ runs over 0.2 s (43% on shorter ones) and up to 50% for the Python. The L FULL loss is not noise: two of three Python runs (4.27 s and 4.30 s) beat all three C++ runs (4.75 to 4.98 s).

CPU and memory for the cases that matter most:

| Case | Python CPU, peak RSS | C++ CPU, peak RSS | CPU ratio |
|---|---|---|---|
| Transmit CE FULL | 5.52 s, 574 MB | 2.75 s, 25 MB | 2.0x |
| Transmit L FULL | 4.80 s, 590 MB | 4.90 s, 80 MB | 1.0x |
| Receive SHORT (the 4 decoding cases) | 10.58 to 13.49 s, 297 MB | 2.35 to 2.98 s, 143 MB | 4.5 to 4.7x |
| Receive CE FULL -6 dB | 50.47 s, 1393 MB | 12.96 s, 796 MB | 3.9x |
| Receive CE FULL -32 dB, blind | 136.0 s, 1393 MB | 21.28 s, 796 MB | 6.4x |
| Listen SHORT | 28.53 s, 421 MB (tree 511 MB) | 10.66 s, 209 MB | 2.7x |
| Listen FULL | at least 71 s (killed), 1290 MB (tree 1529 MB) | 77.28 s, 1263 MB | not comparable |
| Decode --em 2 | 34.65 s, 468 MB | 6.68 s, 361 MB | 5.2x |

- **The C++ receive is single-threaded; the Python's is not.** The Python keeps 1.1 to 2.7 CPUs busy (8.99 s of CPU in 3.38 s on L TINY at -6 dB; measured), probably through numpy's threaded BLAS (inferred), so on a loaded machine the CPU ratio of 3.3 to 6.4x is the better guide. The C++ uses more CPU than wall time only in its own threads: the listener (6.19 s in 4.29 s on TINY) and onnxruntime (1.62 s in 0.57 s for decode).
- **The C++ receive's peak memory depends only on the frame:** 54, 143 and 796 MB on TINY, SHORT and FULL at every SNR tested, 57% of the Python's 1393 MB at FULL (measured).
- **The C++ transmitter stays at 23 to 81 MB** against the Python's 139 to 590 MB: it renders in blocks of 65,536 samples (native/core/qrss/tx.cpp:434), where the Python holds the whole pass, 14.4 million samples, 110 MB as float64 at FULL.

### Where the C++ is not faster

- **An L FULL pass written as int16 is 15% slower, 4.94 s against 4.30 s, and L MEDIUM gains only 1.3x (measured).** Only the CLI's int16 WAV is affected: the app renders each pass once, to float32, through api::pass_audio (native/core/qrss/api.cpp:178-183, called at native/gui/qrss_tx.cpp:318), 1.5x faster than the Python. A callgrind profile of an L SHORT transmit puts 43% in the resampler, which sums in scipy's order to stay byte-identical, and about 28% in glibc's sin and cos. The int16 path also renders the pass twice for the peak it scales by, about 2.4 s at FULL (computed from the float WAV's 2.58 s). S3 has the fix.
- **A FULL listen session saves little or no memory: 1263 MB against at least 1290 MB for the Python's main process, killed at 200 s before the replay ended (measured).** Why is not yet known: the listener never runs the batch front end that makes up most of a WAV receive's 796 MB; S1 lists the suspects and the one-hour profile that would settle it. The Python receives at the end of a slot in a child process (sstvae/qrss/live.py:373-400), and its process tree reached at least 1529 MB. TINY and SHORT sessions use about half the Python's memory: 94 MB against 175 MB, and 209 MB against 421 MB.
- **Start-up runs the other way and inflates the small-frame ratios.** The C++ starts in 0.014 s and 20 MB, against 1.00 s and 101 MB for the Python's receive imports (72x, measured). Every Python figure carries about that second (inferred beyond the receive imports), so the 12.1x on a CE TINY transmit is mostly start-up: about 0.3 s against 0.11 s without it (inferred). The app transmits in process and keeps one `sstvae-qrss listen` running, so it pays start-up once per session.

### Size

- **The binary** is 90,441,960 bytes with the codec and 83,874,080 without (RelWithDebInfo, with debug information), and 4,796,672 and 4,579,432 bytes stripped. Nothing in tools/package_app.sh or .github/workflows/native-build.yml strips, so a Linux package would carry the 90 MB file (inferred from the scripts; no package was built). MSVC and Apple's toolchain keep debug information outside the executable, so Windows and macOS should escape this (inferred). The fix is a strip in the packaging (S5).
- **Its libraries.** The codec-off build needs only libstdc++, libm, libgcc_s and libc. The codec build adds libonnxruntime.so.1 (24.3 MB, version 1.28.0) and, for the model fetcher, libQt6Network and libQt6Core: ldd lists 57 libraries, 97.0 MB, of which ICU data is 30.8 MB. The desktop app ships onnxruntime and Qt already, so a package adds only the binary (inferred). The RUNPATH points into native/build/_deps, a development path, so the package relies on the launcher's LD_LIBRARY_PATH (tools/package_app.sh:189-191).
- **The Python it replaces.** The smallest set that runs the scripts, the `cli` extra plus soundfile, is 280.6 MB in 24 distributions (scipy 111.9 MB, onnxruntime 64.5 MB, numpy 56.4 MB, pillow 19.7 MB); with the interpreter (6.5 MB) and the standard library (40 to 53 MB), that is about 330 to 340 MB (computed). The project .venv, with development extras, is about 468 MB.
- **Lines of code** at 4e92d36, counted by loc.py as physical lines, with code lines (no blank, comment or docstring lines) in parentheses:
  - Reference (sstvae/qrss/*.py and the qrss_*.py scripts): 17,177 (10,590) in 38 files.
  - Shipped C++ (core/qrss with its codec glue, the core/math and core/util the port added, apps/sstvae_qrss): 55,046 (40,497) in 182 files, 45,787 lines of it in core/qrss. That is 3.8 times the reference's code lines, though the reference count includes channel.py (950 lines), si5351.py (127 lines) and qrss_simulate.py, none of them ported.
  - Port support: 95,095 (74,220), 1.7 times the shipped C++ and 5.5 times the reference in physical lines, 1.8 and 7.0 times in code lines (computed). In code lines: pybind11 bindings 16,197 in 68 files, ctests 29,031 in 96, parity files 25,500 in 61, their helpers 1,915 in 52, generators 1,577 in 8.
  - Not counted: the generated config, rx_config and tables (8,021 code lines in 5 files) and the vendored dr_flac.h (8,877 lines).
  - The port is 185 commits from 3843897 to 4e92d36 (181 without merges, 175 titled 'QRSS C++ port').
- **Tests.** The codec-off tree registers 75 QRSS ctests and the codec tree 76 (97 and 98 with the rest of native/). The extra is qrss_core_codec; qrss_core_em_chain, the no-Python chain test, is registered in both and skips where the binary has no encoder. The parity files collect 4,398 tests (4,272 fast, 126 slow), 5.2 times the reference QRSS suite's 841 (692 fast, 149 slow, in 29 files).

### Agreement

Every case agreed within the parity bar: every int16 WAV and all CE output byte-identical, L's float output within one float32 ulp (below), and the receiver's header and picture the same, exactly after decode, EM and retro and within one grey level after a FULL receive.
- **Transmit and encode:** every int16 WAV (CE and L, all four frames) and the CE FULL float WAV are byte-identical, and a PNG gives a byte-identical `.qrsp` (picture ID 7b202aad) despite onnxruntime 1.31.0 against 1.28.0.
- **Receive:** stdout is identical in 10 of 15 cases, including the 4 that found nothing. In the other 5 (SHORT at -24 dB and FULL at -6 dB for CE and L, and the blind CE FULL at -32 dB) only the pass uid's hash suffix differs, since it hashes the latents' float bytes; headers, callsign verdicts, frequencies and SNRs print the same.
- **Pictures:** after a FULL receive, 10 subpixels differ by one grey level (97.8 dB PSNR between the two) for CE and for L, and both score the same against the original (22.16 dB CE, 22.58 dB L). After decode, EM and retro the PNGs are pixel-identical: EM's refined latents differ by at most 1.9e-9 with the same mean W every round (+9.24, +9.33, +9.33 dB), and retro finds the same pass (q=1990587, Z 26.3, 1500.052 Hz) and attaches it to the same 4-pass accumulator.
- **Listen:** TINY and SHORT tiles agree field by field (worst 7.8e-7 Hz in frequency, 8e-6 dB in SNR, 5.7e-5 dB in mean W, 1.4e-4 relative in z_ref), as do the store trees and normalised logs. With no Python FULL tile, the C++ one was compared with a Python receive of the same WAV: within one grey level (93.6 dB PSNR, about 26 subpixels, computed), both 23.11 dB against the original.
- **FLAC:** the hour packed during the SHORT listen (14.4 million samples, 12 s of them signal) decodes to equal samples, and the C++ file is 159,219 bytes against 166,471 from libFLAC through soundfile, 4.4% smaller (computed; raw is 57.6 MB).

Two outputs are knowingly not identical:
- **L float WAVs:** 16 to 35 samples per pass differ by about one float32 ulp near zero (35 of 14.38 million at FULL, at most 4.5e-13). L audio passes through the clipper's FFTs, which round differently in the C++ and in pocketfft (about 1e-15 measured, against the 1e-9 the tests allow, tests/test_qrss_native_tx_audio.py:7-11), and that flips the float32 rounding of a few samples. The app sends this float path (native/gui/qrss_tx.cpp:318), but 4.5e-13 of full scale is far below any soundcard's resolution.
- **JPEG input:** the C++ decodes JPEG with stb_image (native/core/images/images.cpp:576) and the Python with Pillow's libjpeg, so a .jpg gives a different picture ID (edb7bb4e against 7b202aad); a PNG is byte-identical. SSTVAE already accepts this difference.

### Earlier per-module timings

These are the waves' own scratch measurements on the same shared 4-CPU machine while other work ran, mostly through the bindings and from a few runs each. Porter and reviewer sometimes disagreed several-fold (the front end's audio_to_fe in the Python: 0.68 s in one run, 3.05 s in the other), so read them as orders of magnitude. Pairs give the Python's time first unless they say otherwise. A figure another part already gives is only pointed to here.
- **Interpreter-bound pieces gained most:** template_z 236 µs against 1.1 µs (about 210x) and _gauss_z 72 µs against 0.1 µs (about 700x, wave 10), and A3's Viterbi over 896 frames 133 ms against 14 ms (wave 7). cwid.read_ml's 90x is in Lessons (Performance surprises).
- **Long-frame timing fits are why FULL gains more than SHORT:** a blind fit_timing on FULL 13.98 s against 1.40 s (about 10x, wave 5; Lessons says why), fit_timing_l on L FULL 13.25 s against 1.53 s (8.7x, wave 6a) and track() over a FULL pass 19.6 s against 3.67 s (5.3x, wave 5).
- **Acquisition barely gained:** the A2 preamble search was 1.2x faster per CE candidate and 1.5x per L one (wave 5), because numpy's BLAS and scipy already ran its sums and its p-value in compiled code. S4 and O15 give the parts and what fixing them is worth.
- **Where both sides run pocketfft or the same scipy C, the gain is 1 to 3x:** retro's template search 2.2 to 3.3x after batching (wave 10); the spectrogram 1.63x, the special functions and median filter at scipy's speed (wave 4); CFAR 1.1x (60 ms against 53 ms at 896 frames, wave 7).
- **Slower in the C++, besides the L transmit** (the C++'s time first): a plain 8 kHz int16 WAV read, 0.074 s against 0.023 s (wave 6b), and the front end's normalise, 0.47 to 0.49 s against 0.26 to 0.49 s, where numpy's AVX2 sort beats a sliding-window median (wave 4). known_keying (S8) and packing a passband hour to FLAC (Lessons) are slower too.
- **Listener latency, which the end-to-end runs do not show:** feeding a 0.25 s chunk during a background receive had a p95 of 2,340 µs and a maximum of 4,893 µs in the Python, whose worker shares the GIL, against 340 µs and 568 µs in the C++ (wave 9a).

### What was not measured

- Python's FULL listen replay to the end (killed at 200 s), and `sstvae-qrss receive` with a store: it refused one at the time of measuring (citing plan stage 3), and 5e1ae13 added the store and passband afterwards, untimed. Listen and decode exercised the store instead.
- Anything but simulated audio on the one x86-64 test machine: no off-air recording, one picture, the quiet preset only; no Windows, macOS (including the x86_64 cross build), ARM or Android run; no -O3 or -march build; no multi-hour listen session or full 48 h passband store; no timing of the in-process app APIs (api.hpp, listen_api, em_api) as the app calls them.
