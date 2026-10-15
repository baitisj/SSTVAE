# QRSSTVAE: C++ port (sstvae-qrss) vs Python reference (qrss_*.py)

Measured 2026-10-14/15 on branch `waveform_l` at HEAD. Nothing in the repository was edited.

## Machine and method

- Intel Xeon @ 2.80 GHz, 4 vCPU (shared cloud VM), 16 GB RAM, Linux 6.18.
- Load: 1-minute load average 0.67 / 1.92 / 2.94 at the first check (decaying from earlier work). Across 41 readings taken before heavy runs, the 1-minute value had a median of 0.67 and a maximum of 1.56. At the end it was 0.23.
- Commands ran one at a time.
- C++: `native/build-qrss-codec/sstvae-qrss` (codec ON, Release). Python: `.venv/bin/python` 3.x with onnxruntime 1.31.0. The C++ codec uses ORT 1.28.0.
- Every command ran as `mtime OUT timeout 200 cmd` (mtime.c):
  - wall time is from CLOCK_MONOTONIC;
  - CPU time is the child's user+sys from wait4;
  - peak RSS is ru_maxrss: the maximum over the process and the children it reaped, not their sum;
  - "tree" is the peak of the summed RSS of the whole process tree, sampled every 50 ms.
- Runs: median of 3 where a run took under 30 s; otherwise 1 run.
- bench.py drives the runs and appends to results.jsonl; summarize.py builds table.md. Per-run stdout and stderr are in logs/.
- Inputs: one picture for everything, enc/wonder_wheel_640x480.png, encoded to `.qrsp` with fp16 mode A. Picture ID 7b202aad.
  - Transmit: slot 2026-10-09T06:00Z, K1ABC FN42, 1500 Hz, int16 output unless marked float.
  - Receive inputs come from `qrss_simulate.py enc/py.qrsp OUT --slot S --callsign K1ABC --grid FN42 --waveform W --freq 1500 --frame F --snr X --preset quiet --seed N`.
  - The same WAV went to both implementations.
- Listen inputs come from `mkmix.py`: the float transmit WAVs, each normalised to unit power over its keyed samples, summed, plus white noise at the stated SNR in 2500 Hz. They are replayed with `--wav W --slot 2025-10-01T00:00Z`.

## Results (median wall / CPU / peak RSS (tree peak); ratio = Python/C++ wall)

| case | Python | C++ | wall ratio | CPU ratio | N py/cpp | outputs |
|---|---|---|---|---|---|---|
| start-up floor (`python -c import qrss_receive, receiver,...` vs `sstvae-qrss --help`) | 1.00 s / 1.55 s / 101 MB | 0.01 s / 0.01 s / 20 MB | 72x | 111x | 3/3 | n/a |
| encode PNG -> .qrsp, fp16 mode A | 1.39 / 2.68 / 245 MB | 0.32 / 0.83 / 152 MB | 4.4x | 3.2x | 3/3 | .qrsp byte-identical, ID 7b202aad |
| encode JPEG (same picture as .jpg) | 1.37 / 2.68 / 245 MB | 0.33 / 0.86 / 152 MB | 4.2x | 3.1x | 3/3 | IDs differ (7b202aad vs edb7bb4e): stb vs libjpeg decode |
| transmit CE tiny | 1.31 / 1.82 / 139 MB | 0.11 / 0.11 / 23 MB | 12.1x | 17.1x | 3/3 | WAV byte-identical |
| transmit CE short | 1.66 / 2.19 / 177 MB | 0.39 / 0.38 / 23 MB | 4.2x | 5.7x | 3/3 | WAV byte-identical |
| transmit CE medium | 2.50 / 3.03 / 267 MB | 1.02 / 1.00 / 24 MB | 2.4x | 3.0x | 3/3 | WAV byte-identical |
| transmit CE full | 5.32 / 5.52 / 574 MB | 2.80 / 2.75 / 25 MB | 1.9x | 2.0x | 3/3 | WAV byte-identical |
| transmit CE full --float | 3.50 / 4.04 / 299 MB | 1.36 / 1.35 / 24 MB | 2.6x | 3.0x | 3/3 | identical |
| transmit L tiny | 1.14 / 1.67 / 140 MB | 0.19 / 0.19 / 23 MB | 6.0x | 8.8x | 3/3 | WAV byte-identical |
| transmit L short | 1.39 / 1.93 / 178 MB | 0.70 / 0.70 / 30 MB | 2.0x | 2.8x | 3/3 | WAV byte-identical |
| transmit L medium | 2.29 / 2.80 / 273 MB | 1.83 / 1.80 / 43 MB | 1.3x | 1.6x | 3/3 | WAV byte-identical |
| transmit L full | 4.30 / 4.80 / 590 MB | **4.94 / 4.90 / 80 MB** | **0.87x** | 1.0x | 3/3 | WAV byte-identical |
| transmit L full --float | 3.86 / 4.33 / 315 MB | 2.58 / 2.54 / 81 MB | 1.5x | 1.7x | 3/3 | 35 of 14.38M samples differ, max 4.5e-13 (about 1 ULP near zero) |
| receive CE tiny -6 dB | 3.15 / 6.94 / 152 MB | 1.34 / 1.34 / 54 MB | 2.3x | 5.2x | 3/3 | stdout identical |
| receive CE tiny -16 dB | 3.32 / 7.78 / 152 MB | 1.49 / 1.47 / 54 MB | 2.2x | 5.3x | 3/3 | stdout identical |
| receive L tiny -6 dB | 3.38 / 8.99 / 153 MB | 1.69 / 1.68 / 54 MB | 2.0x | 5.4x | 3/3 | stdout identical |
| receive L tiny -16 dB | 2.58 / 5.69 / 152 MB | 1.07 / 1.04 / 54 MB | 2.4x | 5.5x | 3/3 | stdout identical |
| receive CE short -6 dB | 7.24 / 10.89 / 297 MB | 2.46 / 2.42 / 143 MB | 2.9x | 4.5x | 3/3 | stdout identical (header K1ABC FN42 7b202aad, callsign agrees, mean W +8.17 dB) |
| receive CE short -24 dB | 7.86 / 11.49 / 297 MB | 2.49 / 2.47 / 143 MB | 3.2x | 4.7x | 3/3 | identical except the uid hash suffix (preamble detection) |
| receive L short -6 dB | 7.24 / 13.49 / 297 MB | 2.99 / 2.98 / 143 MB | 2.4x | 4.5x | 3/3 | stdout identical |
| receive L short -24 dB | 6.55 / 10.58 / 297 MB | 2.37 / 2.35 / 143 MB | 2.8x | 4.5x | 3/3 | identical except the uid hash; callsign check says DISAGREES in both |
| receive CE short -28 dB | 11.06 / 18.88 / 297 MB | 3.65 / 3.64 / 143 MB | 3.0x | 5.2x | 1/1 | rc 1, "no CE or L signal found" in both |
| receive CE short -32 dB | 10.87 / 16.49 / 297 MB | 3.42 / 3.38 / 143 MB | 3.2x | 4.9x | 3/3 | rc 1, nothing found in both |
| receive L short -32 dB | 3.50 / 6.67 / 297 MB | 1.44 / 1.41 / 143 MB | 2.4x | 4.7x | 3/3 | rc 1, nothing found in both |
| receive CE full -6 dB + --image | 45.04 / 50.47 / 1393 MB | 12.06 / 12.96 / 796 MB | 3.7x | 3.9x | 1/3 | text same but the uid hash; PNG 10 subpixels differ by 1 (PSNR 97.8 dB); both 22.16 dB vs the original |
| receive L full -6 dB + --image | 43.01 / 52.49 / 1393 MB | 14.99 / 15.77 / 796 MB | 2.9x | 3.3x | 1/3 | uid hash only; PNG 10 subpixels by 1 (97.8 dB); both 22.58 dB vs the original |
| receive CE full -32 dB, blind (TBD) | 124.42 / 136.0 / 1393 MB | 21.55 / 21.28 / 796 MB | 5.8x | 6.4x | 1/3 | uid hash only; found by "tbd", SNR2500 -35.0 dB, header not decoded, in both |
| receive L full -30 dB | 63.59 / 74.28 / 1393 MB | 12.40 / 12.31 / 796 MB | 5.1x | 6.0x | 1/3 | rc 1, nothing found, stdout identical |
| receive of live/full.wav (the FULL listen input) | 40.03 / 47.16 / 1393 MB | 11.62 / 12.52 / 795 MB | 3.4x | 3.8x | 1/3 | reference for the listen PNG |
| listen replay TINY (CE 1200 + L 2000 Hz, -10 dB) | 8.04 / 10.59 / 175 MB (tree 329) | 4.29 / 6.19 / 94 MB (88) | 1.9x | 1.7x | 3/3 | cmp_listen AGREE |
| listen replay SHORT (CE K1ABC 1200 + L W1AW 2000 Hz, -6 dB) | 24.15 / 28.53 / 421 MB (tree 511) | 9.15 / 10.66 / 209 MB (210) | 2.6x | 2.7x | 3/3 | cmp_listen AGREE |
| listen replay FULL, --refresh 600 | over 200 s (timeout; 71 s CPU, 1290 MB, tree 1529 MB) | 68.79 / 77.28 / 1263 MB | over 2.9x | n/a | 1/1 | C++ only; its tile PNG vs Python receive of the same WAV: max diff 1, PSNR 93.6 dB, both 23.11 dB vs the original |
| decode (store, key K1ABC:7b202aad) | 1.62 / 3.17 / 454 MB | 0.57 / 1.62 / 359 MB | 2.9x | 2.0x | 3/3 | stdout identical, PNG pixel-identical |
| decode --em 2 | 33.34 / 34.65 / 468 MB | 5.78 / 6.68 / 361 MB | 5.8x | 5.2x | 1/3 | stdout identical (mean W +9.24 -> +9.33 -> +9.33 dB); PNG pixel-identical; refined z max diff 1.9e-9 |
| decode --retro --frame short | 7.14 / 8.63 / 460 MB | 2.60 / 3.55 / 355 MB | 2.8x | 2.4x | 3/3 | both find 1 pass, q=1990587, Z 26.3, 1500.052 Hz, attached by header (4 passes); only the new uid hash differs; PNG pixel-identical |

### Listen details

The tile fields were compared by cmp_listen.py.

TINY:
- Worst differences: f_hz 7.8e-7, snr_db 8e-6, mean_w_db 5.7e-5, z_ref relative 1.4e-4.
- Two tiles; TINY and SHORT tiles carry no picture (FULL only).
- Store trees and normalised logs are equal.
- Passband: 1112 kB on disk for Python vs 1096 kB for C++, 112.5 MB apparent (sparse raw hours). No FLAC was packed.

SHORT:
- Two tiles, both complete, with callsigns and picture 7b202aad.
- Worst differences: f 3e-7, snr 3e-7, mean_w 2e-6, z_ref 5.9e-5.
- The packed hour fe_488687.flac (14.4M samples, 12 s of them signal) has equal samples. Python's file (soundfile/libFLAC) is 166,471 bytes and the C++ encoder's is 159,219; raw would be 57.6 MB.
- Passband: 4248 vs 4236 kB on disk, 56.4 MB apparent. Whole store: 10436 vs 10424 kB.

FULL: Python's round-B receive of the previous slot it half heard (23:45) alone took 131 s, which is why it went past 200 s.

### EM and retro fixture

Python `qrss_receive.py --store em/base` ran on four CE SHORT passes of 7b202aad:
- -6 dB at 06:00, -15 dB at 06:15 and -20 dB at 06:30: headers decoded, attached by header.
- -30 dB at 06:45: not found, kept in the passband.

Each timed run started from a fresh copy of that store.

## Sizes

- **sstvae-qrss binary:**
  - Codec build: 90,441,960 bytes as built, 4,796,672 stripped (4.74 MB text).
  - No-codec build: 83,874,080 bytes as built, 4,579,432 stripped.
- **Shared libraries:**
  - The no-codec build NEEDS only libstdc++, libm, libgcc_s and libc.
  - The codec build also NEEDS libonnxruntime.so.1 (24.3 MB, ORT 1.28.0), libQt6Network and libQt6Core (Qt is there for the model fetcher). ldd resolves 57 libraries totalling 92.5 MiB, of which ICU data is 30.8 MB.
  - The codec build's RUNPATH points into native/build/_deps.
- **libpython:** none in either build (ldd | grep -c python gives 0).
- **Python environment replaced:**
  - The project .venv is 462 MB; it includes dev extras such as pyarrow at 155 MB.
  - Minimal closures by envsize.py: base (numpy, scipy, pillow) 179.2 MiB; `cli` extras 263.2 MiB in 21 dists; cli + soundfile 267.6 MiB in 24 dists.
  - The largest in that last closure are scipy 106.7, onnxruntime 61.5, numpy 53.8, pillow 18.8 and hf-xet 11.0 MiB.
  - On top of that comes the interpreter (6.5 MB) and the stdlib (about 40-53 MB).

## Lines of code (loc.py; physical / code lines, code excludes blank, comment and docstring lines)

| group | files | lines | code |
|---|---|---|---|
| Python reference sstvae/qrss/*.py | 31 | 16,092 | 9,865 |
| qrss_*.py scripts | 7 | 1,085 | 725 |
| **reference total** | 38 | 17,177 | 10,590 |
| C++ native/core/qrss, hand-written | 129 | 45,787 | 33,805 |
| core/qrss/codec | 11 | 932 | 589 |
| core/math, added by the port | 19 | 4,285 | 3,180 |
| core/util, added by the port | 4 | 654 | 475 |
| apps/sstvae_qrss | 19 | 3,388 | 2,448 |
| **shipped port total** | 182 | 55,046 | 40,497 (3.8x the reference) |
| core/qrss, generated (config, rx_config, pyunicode tables, tables) | 5 | 8,166 | 8,021 |
| bindings (pybind11 parity module) | 68 | 20,759 | 16,197 |
| ctests native/tests/qrss | 96 | 33,575 | 29,031 |
| vendored dr_flac.h | 1 | 12,760 | 8,877 |
| parity tests test_qrss_native_*.py | 61 | 35,117 | 25,500 |
| parity helpers tests/qrss_native | 52 | 3,432 | 1,915 |
| generators tools/gen_qrss_*.py | 8 | 2,212 | 1,577 |

The reference count includes code that was not ported: channel.py (950 lines, the channel simulator), si5351.py (127 lines) and qrss_simulate.py, which has no C++ subcommand. The port spans 175 commits from 3843897 (2026-10-12).

## Tests

- ctest -N: 98 in the codec tree (78 of them qrss), 97 in the no-codec tree (77 qrss).
- Parity files: 61 files, 4,398 tests collected (4,272 fast, 126 slow).
- Reference QRSS suite: 29 non-native files, 841 tests (692 fast, 149 slow).

## Surprises

1. **L FULL int16 transmit is slower in C++** (4.94 s vs 4.30 s), and L MEDIUM is only 1.3x faster. Callgrind on L SHORT accounts for it:
   - 43% is in `lwave::resample_into`, a scalar loop that copies scipy's upfirdn.
   - About 28% is glibc scalar sincos inside `ce::to_audio`, where numpy uses vectorised sin/cos.
   - int16 output renders the pass twice, once to find the peak and once to write (tx.cpp 433-463). That is what keeps memory at 80 MB against 590 MB. With --float, where only one render is needed, the C++ is 1.5x faster.
2. **L --float output is not bit-exact**: 16-35 samples per pass differ by about 1 ULP near zero. The int16 output is byte-identical, and so is every CE output.
3. **JPEG input gives a different picture ID** (stb vs libjpeg decoding). PNG input is byte-identical.
4. **FULL listen memory is not lower in C++**: 1263 MB, against Python's 1290 MB main process (1529 MB for the whole tree, because Python receives in a child process). Receive memory, by contrast, is 57% of Python's at FULL.
5. **Python's FULL listen replay does not finish in 200 s**; the C++ takes 69 s. Round B on the half-heard previous slot alone took 131 s in Python.
6. **`sstvae-qrss receive` refuses --store/--passband**, and its help still says the store is "not ported yet (plan stage 3)". listen and decode already have the store.
7. **The codec build links Qt6Network/Qt6Core** for the model fetcher, so it drags in 57 runtime libraries (92.5 MiB, ICU included). Its RUNPATH points into another build tree (native/build/_deps).
8. **ORT versions differ** between the venv (1.31.0) and the C++ (1.28.0); uv.lock pins 1.28.0 or 1.24.3 depending on platform. Encode outputs are still byte-identical, and the decoded PNGs pixel-identical, wherever the inputs matched.
9. **Python receive uses more CPU than wall time** (BLAS threads), while the C++ is single-threaded. On a busy machine the CPU ratio (4.5-6.4x) is the better guide than the wall ratio (2-6x).
10. **C++ receive peak RSS is set by the frame alone**: 54 MB TINY, 143 MB SHORT and 796 MB FULL, whatever the SNR.
11. **Two coincidences**:
    - The FULL receive PNGs differ in exactly 10 subpixels by 1 in both the CE and L cases.
    - The C++ passband is 12-16 kB smaller on disk (sparse allocation, then FLAC 4% smaller).
12. **There is no separate "blind" CLI mode.** Weak SHORT passes (-24 dB) were still found by the preamble. The TBD path (method "tbd", read with probe_method.py) only appeared on the FULL -32 dB case, which is also where the C++ is fastest relative to Python (5.8x).

## Files

All under /tmp/claude-0/-home-user-sstvae/92006b74-04c2-5210-829f-64b8a831fdbf/scratchpad/measure/:
- Measurement scripts: mtime.c, mtime, bench.py and summarize.py.
- Comparison scripts: psnr.py, cmp_listen.py, cmp_store.py, mkmix.py, probe_method.py, envsize.py and loc.py.
- Data: results.jsonl (every run) and table.md.
- Logs: logs/ holds every run's stdout and stderr.
