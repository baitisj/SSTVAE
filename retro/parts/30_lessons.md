## What the port taught us

The hard parts were not where the plan put them: the DSP came across nearly bit for bit, while the work and the bugs sat in numpy's evaluation order, Python's argument edges, and the listener's threads and teardown. The paragraph below defines two test terms, and the first lesson defines how the port was run.

Two test terms recur here and elsewhere. A whole-caller A/B runs one of the reference's callers once plain and once with every row applied, and compares the two. A swap test assigns C++ pieces into the reference and requires the reference's result bit for bit.

### How the waves ran

- **One porter and one adversarial reviewer per lane, each lane in its own worktree, was the unit that worked, and the integration step was where lanes met for the first time.**
  - A wave was a batch of modules ported in parallel. Each module was a lane: a porter wrote the C++ in its own git worktree, and an adversarial reviewer read it against the Python line by line, probed its edges, mutation-tested the new tests (broke one line on purpose to see whether a test fails) and fixed what it found. An integration step then merged the lanes, rebuilt both build trees (with and without the codec) and re-ran everything.
  - About 98 agents ran (a mapping step, waves 1 to 10 and two reruns), and reviews fixed about 211 findings: 46 in wave 1, 25 in wave 4, 19 in wave 7 and 21 in wave 10, but 4 in wave 5 and 3 in wave 6a, where they mostly closed test gaps.
  - Registering sources and tests by glob kept lanes off each other's files (waves 6b and 7 merged with no conflicts); shared changes went into a follow-up commit per merge.
  - Cross-lane faults surfaced only at integration: wave 8's passband and live rows clashed until the live ones accepted passband.py's native slot_t0 (136c208), a wave 9a fix fell between two lanes until integration made it (a12c3b8), and tools/gen_qrss_rx_config.py's module list conflicted whenever two lanes added a module.
  - A reference fix in one lane broke another lane's tests that pinned the old behaviour, such as a listener test asserting the reference's failure on a bad state.json (wave 9a).

- **The plan got the order of the stages right, the shipped code's size wrong by about two, and the tests' size wrong by more than ten.**
  - The plan (section 0) sized the port at 23,000 lines of production C++ (25,000 with stage 4) plus 7,000 of tests and bindings. Counted by loc.py (Measured), the port shipped 55,046 lines of C++ (40,497 code lines), about 2.2 times the plan's 25,000, and needed 95,095 lines of bindings, ctests, parity files, helpers and generators, about 13.6 times its 7,000 (ratios computed).
  - Elapsed, by author times: 1.3 h for stage 0, about 9 h for stage 1 with a restart, 21.4 h for stage 2 (waves 4 to 7), 13.5 h for stage 3 (waves 8 to 9b) and 5.7 h for stage 4 (wave 10), about 51 h in all.
  - Three plan decisions changed on contact: stage 4 became required after the 2026-10-13 05:00Z request for no Python in release builds; the passband store gained an encoder of its own (see the FLAC lesson); and a port of scipy's cephes igamc and igamci replaced a planned closed form, keeping A2's p-value argmin the reference's (native/core/math/special.hpp:6-20, wave 4).
  - The plan's tolerances were far too loose: about 1e-9 on L's clipped audio and 1e-12 on CE's, where the test machine reached 7.9e-16 to 1.33e-15 on L and bit-exact CE (wave 2).

- **Interruptions and the shared machine set the pace of the second half, and committing early made the interruptions cheap.**
  - A container restart cut wave 3 off at 02:55Z on 2026-10-13, leaving two work-in-progress commits (584110a, 60b0c73) from which a rerun finished stage 1 at 06:16Z. Wave 6b's first run, stopped by accident, was finished the same way (345e006, cb7a31d).
  - The shared project folder went unreadable at about 03:00Z on 2026-10-14 (P1, T5), and builds were the slow step: about 7.5 min per tree after touching a widely included header at -j2 on 4 shared CPUs (measured; P3).

### What the adversarial reviews found

- **The reviews found almost no arithmetic bugs; the bugs sat where a Python caller meets a C++ edge.**
  - The 144 review-bug records count records, not fixes: several bundle a whole lane's findings, and the fixes number about 211. Roughly, 34 are tests that could not fail or coverage gaps, 29 argument handling and refusal texts, 19 platform and locale, 16 concurrency and teardown, 14 integer limits, 7 orchestration slips, 6 each of NaN, summation order and file formats, 2 quadratic slowdowns, 4 stale comments and 1 that fits none of these.
  - One was plain arithmetic: npsum's sum_as chunked same-dtype sums at 8,192 elements and was wrong from 8,193 on (wave 1). The demod review found its numeric core bit-identical on every probe and all eight real bugs in the bindings (wave 4).
  - One example of each edge: a float n0 of 1.5 was truncated where the reference raises IndexError (wave 4); INT64_MIN // -1 raised SIGFPE (wave 1); a NaN fs matched every cached std::map key (wave 4); and refusal classes are interface, so wav::FormatError carries Python's error class (native/core/qrss/wavread.hpp:92).
  - Fuzzing beat hand-picked cases: the WAV reader passed its author's parity test while departing from scipy on many file shapes, and since its fix passes 3,000 of 3,000 fuzzed files (tests/test_qrss_native_receiver_slot.py:1559, wave 6b); the npz reader raised UnicodeDecodeError on about 17% of fuzz inputs before its fix (wave 1).

- **Platform and locale bugs were the third-largest class, and most were found on Linux by probing for Windows.**
  - QCoreApplication's setlocale(LC_ALL, "") made the CLI read 1500.3 as 1500 under de_DE and print '287,9 Hz' in a refusal; main.cpp now resets LC_NUMERIC (native/apps/sstvae_qrss/main.cpp:180-183) and refusals go through frame::py_format (native/core/qrss/frame.cpp:237).
  - A narrow std::string path reaches MSVC in the ANSI code page and breaks under a non-ASCII user name, so wave 7 routed file sites through one UTF-8 helper (util::path_text) and made the acceptance pins survive a Windows checkout's backslash paths and CRLF sources.
  - Windows headers trapped three ways: <windows.h> without NOMINMAX breaks std::min( (MinGW predefines NOMINMAX, so the probe was a wrapper header; wave 9a), it defines far as a macro (native/core/qrss/flac.cpp:619-620), and a seam named Sleep shadowed ::Sleep (native/core/qrss/sources.cpp:410).
  - A Windows-only byte decoder was checked on Linux by compiling a copy with #ifdef _WIN32 forced on and comparing it against Python-generated pins, with 0 mismatches (wave 7).

- **Most concurrency and teardown bugs were found by review or by failing tests rather than by ThreadSanitizer.**
  - ThreadSanitizer found no data race in QRSS production code; it flagged a race on a test's latch, and four checks built on fixed 30 ms pauses failed once instrumentation slowed a pack about 6 times (wave 9b).
  - Reading found the rest. The listener waited on the store's lock while holding its own, blocking feed for 7.68 s, under 1 ms after the fix (measured; native/tests/qrss/test_listener.cpp:922, wave 9a). Per-object store mutexes let only 3 of 6 concurrent attaches to one root survive, fixed by a per-root registry (native/core/qrss/store.cpp root_mutex, wave 8).
  - Teardown with an interpreter in the process was its own class: a static py::object destroyed after finalization (waves 1 and 2) and PyHooks dropping references without the GIL (wave 10).
  - Abandoning a receive left a stale state.json and a late listening=true write until LiveListener::abandon() and PassbandStore::stop_packing() closed them (2f36a93 and its follow-up, wave 9b).

- **Mutation testing is what showed which tests could not fail, and many could not.**
  - Every wave 10 lane's first ctest pins passed by a wide margin, the EM pins' worst error 1.48e-8 against a 1e-5 bar (measured), so only a deliberate mutation shows such a pin can fail; a surviving mutant marks a test that cannot.
  - Survivors came in every stage 3 and 4 wave: the store's expiry '<' as '<=' and min_heard's '>=' as '>' (wave 8); the resync boundary, tested 55,900 samples past the point, and ages assigned by i % 4 (wave 9a); a stale FLAC Rice plan through 311,486 plus 18,441 checks (wave 9b); a flipped drift sign through all 10 template_search cases, and a header re-label invisible to fixtures that all used version 2 headers (wave 10). Two path mutants survive by construction, marking code no test can reach (wave 7).
  - The ctest pins killed em.cpp mutants the parity file missed (known_keying's tie-break, template_z's chunk sum, gamma_logsf's median split), and the parity file an LLR mutant the pins missed. A stale mutant binary once gave a flat EM history of [6.198, 6.198, 6.198] that matched the reference to 1 ulp after a rebuild (wave 10).

### The parity harness

- **Running the reference's own callers with the C++ substituted found what function-by-function parity missed, and became mandatory after its first run.**
  - The first whole-caller A/B (tests/test_qrss_native_acquire_stats.py:431, wave 4) failed at cell_lam, 337.535874989212 against 337.5358749892118, on a Fortran-ordered matrix reaching preamble_scales; the lane's 35 parity tests, all on C-contiguous inputs, had passed.
  - Wave 5 required one in every lane, by which time the table had grown from 15 rows early in stage 1 to 278 rows and 63 import sites.
  - Its reach is what the callers send: no A/B fed a NaN timing or took the store=False path that crashed. With every row installed the C++ calls its own callees, so wave 6a also ran configurations keeping each caller in Python, and 4 mutants were caught only there.

- **An A/B proves nothing until it proves the C++ ran.**
  - Under --native-qrss, wave 1's first parity file compared the C++ against itself; under that flag parity files skip by design, and "8 skipped" reads like success (wave 6a).
  - Without the call counters and "target is the binding" assertions standard since wave 5, the A1/A2 A/Bs would have passed on pure Python.
  - Each harness detail kept the reference side the reference: spies installed before the rows are built, the reference answer computed before the rows apply (wave 6a), and a check for __wrapped__, since functools.wraps copies the native mark onto a spy (tests/qrss_native/em_refine.py:39-41, wave 10).

- **The strongest checks were exact on exact inputs.**
  - The swap test requires the reference's track, with the C++'s _smooth and templates assigned in, to equal the C++'s bit for bit (tests/test_qrss_native_track.py:846, wave 5); gate V and receive_pass are pinned the same way (tests/test_qrss_native_track_timing.py:674, tests/test_qrss_native_receiver.py:637).
  - Deterministic stubs made orchestration exact and cheap: 240 seeds in about 2.3 s, against 1 to 5 s for one real acquisition (measured; tests/test_qrss_native_acquire.py:889, wave 6a), and both listeners matched on 190 of 190 random scenarios (wave 9a).
  - A capture can be built rather than committed: the store's own template planted in seeded noise gives a C++ and reference agreeing to about 2e-16, against pins at 1e-9 (wave 10).

- **Pins had to be generated from the reference: both hand-typed pins in wave 4 were wrong.**
  - np.polyfit through one point at degree 1 gives the minimum-norm [0.5, 0.5], not [1, 1], and a TINY frame has 1,753 symbols, not 1,024 (wave 4). Every generated pin passed first time.
  - Generators have traps too: zipfile.writestr stamps the local time where np.savez stamps 1980-01-01, so committed pins changed between runs (wave 6b).
  - rx_pins.json's source list comes from a static import closure, which found four modules a hand list had missed (tests/test_qrss_native_acceptance.py:102, wave 7). The cost is false alarms: a regeneration forced by a fix to frontend.py and live.py changed 4 lines, every summary byte-identical (0be27ca, wave 9a).

- **Wrong fixtures and test oracles were a recurring source of false results, mostly tests that passed while testing nothing.**
  - An erasure from 5 s to 15 s on TINY fell entirely in the preamble and callsign region, so every output equalled the unerased pass; a test now checks each fixture has the property its label names (tests/test_qrss_native_demod.py:66, wave 4).
  - Fixtures hid whole branches: none blanked a symbol, so the erase paths were never compared (wave 6a); TINY has no callsign windows, so a cwid A/B on it calls nothing (wave 5); and with no header SNR, make_pass_result gives all-zero LLRs, letting a known_header mutant survive (wave 10).
  - Scenarios were wrong too: a 0 dB TINY signal vanished under the headerless cut at -6 dB, so a benchmark measured nothing (wave 9a), and a session-sharing test's clock sat 400 s past the hour it meant to test (native/tests/qrss/test_em_api.cpp:646, wave 10).

### Numerics traps

- **Bit-exactness was reachable far more often than planned, at the price of copying numpy's evaluation order.**
  - Measured bit-identical: polar decoding on 1,642 vectors (wave 1); A1, 0 of 2,496,260 cells differing, and A3 over 9,600 Z values (wave 7); template_z on 20,000 random points (wave 10); and pocketfft at every c2c length from 1 to 300 and 17 more up to 65,536 (tests/test_qrss_native_npfft.py:37).
  - The price was numpy's own operation order and formulas: its FMA complex multiply (native/core/math/npcomplex.hpp:40), Smith's division, sums seeded at +0.0 and einsum's two-lane SSE2 sum (native/core/qrss/track.cpp:177-201), with -ffp-contract=off so an FMA cannot flip a pinned digest (native/CMakeLists.txt:229).
  - Where it stops, it stops small: z is 5 to 10 float32 ulps off on single small latents, and a FULL --image differs from the reference's PNG by one grey level on 10 to 23 of 921,600 subpixels, depending on the input (wave 6b; Measured).

- **The bits depend on the CPU, the BLAS build and memory layout, so "bit-exact" is a claim about one machine.**
  - numpy 2.5 picks complex arithmetic by CPU dispatch: the test machine runs the X86_V4 loops, and forcing X86_V2 changed 44% of complex products (measured, wave 4).
  - np.convolve, np.dot and np.vdot sum in OpenBLAS's kernel order (waves 4 and 5), and numpy reductions follow memory layout, which changed up to 42% of the cells of the real preamble sums (wave 4).
  - The interpreter counts too: -2j*np.pi has a +0.0 real part under CPython 3.13 and -0.0 under 3.14, which flips the sign of exp()'s imaginary zero at t = 0 (native/core/qrss/ltrack_timing.cpp:102-103, wave 6a).

- **numpy and CPython carry their own elementary functions, and each differed from libm somewhere; scipy's cephes calls libm and ported bit for bit.**
  - numpy's float32 log, which the port copies (native/core/math/nplog.hpp:36, wave 4), differs from a correctly rounded one on about 5% of arguments, its SIMD exp from glibc's on about 5%, np.angle on 7% and log10 on 5% to 9%; so mean_w_db is the one field the C++ store writes differently (wave 8).
  - CPython's math.lgamma is its own Lanczos approximation, off libm's in the last place for about half of arguments (native/core/math/special.cpp:1347, wave 10).
  - Squaring a numpy scalar calls pow, which differs from x*x on about 1 in 1,200 values, and GCC folds pow(x, 2.0) into x*x, so the port keeps a volatile exponent (native/core/math/cubic_spline.cpp:153-156, wave 4).

- **Signed zeros and NaN payloads were part of the output, and the std:: functions got them wrong.**
  - np.maximum(-0.0, 0.0) is +0.0 and keeps NaN, so neither std::max nor std::fmax would do (native/core/qrss/render.cpp:26, wave 4).
  - OpenBLAS's solve returns [nan, 1] for a NaN pivot where a portable LU returns [nan, nan], so no NaN pattern from numpy's solve or inv is pinned (wave 4).
  - Some payloads cannot be matched: with several NaNs in one WAV frame, the channel mean's payload follows the compiler's operand order (0xfffe340000000000 against 0xfffa280000000000, wave 6b).

- **Where the reference is ill-conditioned, its own conditioning set the floor, and no checked decision moved.**
  - The Kalman/RTS pass keeps about 7 digits: one ulp in one BLAS dot moves P by up to 2.0e-5 relative (measured, wave 5). refine_timing's Newton step at h = 0.3 samples turns last-ulp differences into about 3e-5 relative in the drift's ppm (wave 10).
  - numpy is sometimes the less accurate side: its slope on a weighted two-point fit is 3.9e-8 off exact rationals, the port's Jacobi SVD 6e-12 (wave 4).
  - None of it moved a decision: both implementations pick the same winning cell on all 82 noise captures, and decode --em and --retro print the same text and identical pictures in all 10 cases (wave 10).

### Porting numpy and scipy idioms

- **About a fifth of the production C++ replaces libraries the reference imports, and porting the same C again was the safest replacement.**
  - That is about 10,700 lines (computed): 4,300 of math, 4,200 of file formats and 2,200 emulating Python's runtime (JSON, numbers, text, paths, argparse), plus a 1,636-line generated Unicode table.
  - Ported scipy C (cephes igamc and igamci, ndtri, i0e) and vendored pocketfft agree bit for bit; a Jacobi SVD and an LU in place of LAPACK agree to rounding, and even dgelsd's eps had to be LAPACK's 2^-53 (native/core/math/linalg.cpp:260, wave 4). The live noise generator is the one deliberate departure (K1).

- **Reproducing Python's I/O meant reproducing its walks, its errors and its text handling.**
  - scipy's WAV reader walks while tell() is under the RIFF size plus 8, keeps the last data and fmt chunks, and raises a different error class per malformed shape; the port does the same, misreads included (wave 6b).
  - The npz writer is byte-identical because np.savez stamps 1980-01-01 (wave 1). numpy's memmap 'r+' grows a short file, and until the C++ did too, a 0-byte raw hour left by a crash failed every C++ write (wave 8).
  - Numbers: json.loads holds any integer, which the C++ re-reads as a float literal when out of range (native/core/qrss/live.cpp:207); float(INT64_MAX) rounds to 2^63; and std::stod accepted '0x10', made '1e-500' inf and read the locale's decimal point (wave 8).
  - Streams and names: stderr is always backslashreplace while stdout follows the locale, argv and file names need surrogateescape (native/core/qrss/pytext.hpp:9-14), and Windows names need WTF-8 so a lone surrogate survives (native/core/util/fileio.hpp:95, wave 7).

- **The home-made FLAC encoder beat libFLAC on size and lost on time.**
  - Fixed predictors gave files 1.21 to 1.31 times soundfile's size (wave 8), LPC up to order 12 gave 0.898 to 0.968 times on 8 simulated hours, and libFLAC level 5's order cap of 8 would cost about 9.6% (measured, wave 9b); a SHORT listen's packed hour came out 4.4% smaller than libFLAC's (Measured).
  - Encoding a fully heard passband hour takes 1.29 to 1.50 s against soundfile's 0.36 to 0.76 s, an hour heard 12% 0.42 to 0.48 s, and an hour of white noise 1.86 s (measured, wave 9b), all inside the 2 s budget.
  - Checking it needed a strict decoder: libsndfile refuses valid 0-frame streams and some rates above 65,535 Hz (wave 8), and dr_flac a partition order above 8 (native/third_party/dr_libs/dr_flac.h:4959).

### Performance surprises

- **The C++ won big only where the reference ran interpreted loops; where numpy or scipy already ran tuned C, it tied or lost.**
  - Interpreted loops gained 90 to 700 times, from cwid.read_ml's 24.2 ms against 0.27 ms (wave 5) up to template_z and _gauss_z (wave 10; per-module figures in Measured). Blind fit_timing's 10 times on FULL is an algorithm change, a direct sparse gather in place of FFT correlation (measured, wave 5).
  - Where numpy or scipy already ran compiled code the gain was 1 to 3 times or a loss: A2's preamble search gained 1.2 times, and normalise and a plain WAV read are slower (measured; whole commands in Measured; S4, O15).

- **The parity rules and the compiler's defaults cost speed until the hot loops were written for them.**
  - Without -mfma, std::fma is a library call, two per numpy-style complex multiply; AVX2 and FMA clones picked at run time keep the bits and took _fe_power from about 3 times slower than numpy to 1.7 ms against 5.4 ms (measured; native/core/math/convolve.cpp:136, wave 4).
  - Inlining numpy's negative-index rule with a [[noreturn]] cold thrower took extract from 24.9 ms to 13.2 ms (measured; native/core/qrss/demod.hpp:119-124, wave 4).

- **Buffer shapes and copies made the C++ slower than the reference more often than arithmetic did.**
  - oaconvolve with whole rows-by-n buffers, about 130 MB each, took 0.85 s against scipy's 0.36 to 0.39 s; about 54 blocks at a time gave the same bits in 0.50 s (measured, wave 4).
  - Two quadratic paths hid behind small test inputs: npz member lookup took 8 s for 65,536 members, 0.35 s after the fix (wave 1), and a 900 s replay pushed whole into StreamFE took 2.85 s against the reference's 0.90 s, 0.45 s after the fix (wave 4).

### What the port says about the Python reference

- **On bad input the reference fails with library exceptions or silent NaN, and the C++ reproduces both on purpose.**
  - template_z raises "math domain error" for n_chunks of 0 or less instead of a refusal, and em_refine's stop test is False for NaN, so on an empty accumulator every round runs (sstvae/qrss/em.py:399; B11, B14).
  - Most of these are reachable only from damaged or hand-edited files, and the C++ copies the texts so the two CLIs print the same thing (wave 10).
  - Value-dependent types made bugs of their own: corr_accept returns np.bool_ where corr_stat returns 0.0, so one log line wrote 1.0 or 0.0 in one case and true or false in the other (wave 8).

- **A profile taken through a test can point at work a real run never does.**
  - Caching TbdHit.f_hz looked like the big win, with thousands of polyfit calls in the profile, but those came from a test-only mode; in a real acquire it removes 1 to 6 fits of about 0.16 ms each, under 1 ms in all (measured, wave 7).

### Tooling and the environment

- **Several tools reported success when nothing had run.**
  - A slow test run by node id without -m slow prints "1 deselected" and, through a pipe, exits 0 (wave 7).
  - The ninja target sstvae_qrss_cli is the CLI library and does not link sstvae-qrss, so the decode tests skipped almost everything until the right target was built (wave 10).
  - The flac CLI was not installed, so test_flac_cli always skipped; libFLAC through ctypes, with a self-check that can fail, now stands beside it (tests/test_qrss_native_flac.py:377, wave 9b).

- **Measurements on a shared machine needed the same suspicion as the code.**
  - Timings taken while other work shared the 4 CPUs were off by up to 4 times (3.15 s against 0.84 to 1.01 s for one acquire) until runs were pinned to one thread with the first discarded (wave 7).
  - On Linux, exec carries the parent's RSS high-water mark into the child, so a C++ run under pytest read 355 MB against a real 73 to 78 MB (wave 9b).
  - ThreadSanitizer slows the DSP far more than the file and thread code: receiver_slot took about 255 s against 20.1 s, about 13 times slower (computed), which needed a 1,500 s ctest TIMEOUT (wave 9b).
