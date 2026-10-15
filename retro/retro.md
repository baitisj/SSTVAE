# QRSSTVAE: what the C++ port taught us, and what to do next

2026-10-15

This answers Jeff's 2026-10-13 request to "express your insights about how things could be better and how we can make optimizations that might be useful", in the 2026-10-12 ideas document's format.

**Short answer.** The port is finished: since the 2026-10-15 receive fix (5e1ae13, reviewed in 3787d20) every QRSSTVAE station command is one C++ binary with no Python, and every output meets the parity bar below. Most of what would make things better is small:

- **Have the app send L's lead-in (F1).** One line in the app plus a setting (inferred). A steady lead-in measured about +2 dB of live A1 depth for L (A1 is the first acquisition stage), and 5 to 6 dB once the receiver also looks at it (L1, not built).
- **Keep a long listening session tidy and its store whole (B1, B5).** Neither listener deletes the picture of an expired tile (a detected signal the app shows), up to 96 PNGs a day per signal (computed), and two processes merging into one store of received passes can lose a pass (read from the code). About 15 and 60 to 100 lines per language (inferred).
- **Find where a FULL listener's memory goes (S1, S2).** A FULL (30-minute) session peaks at 1263 MB, about what the Python used (measured). The leading suspect, onnxruntime's memory pool, holds about 340 MB (inferred), and streaming a WAV through the front end would cut a FULL receive from 796 MB to roughly 360 to 520 MB (inferred).
- **Run the port's checks in CI (T1, T2, T10).** None of the port's commits has run there, and the no-Python chain test passes as a skip when no models are set (checked 2026-10-15).
- **Spend less CPU without changing a bit (O15, S4, S3).** Up to about 1.1 s off a 1.3 to 1.7 s FULL acquisition of both waveforms (computed and inferred), and the CLI's int16 L FULL transmit, the one command slower than the Python (4.94 s against 4.30 s, measured), down to about 2.6 s by rendering the pass once (computed).

What the port taught us (Lessons, below): the C++ is 1.9 to 5.8 times faster in wall time on every receive, listen and decode case timed (measured; receive ran with --no-store on both sides, so storing is untimed), and the speed came where the Python looped: interpreted loops gained 90 to 700 times, code numpy or scipy already ran in C 1 to 3 times (measured). Almost none of about 211 review fixes were arithmetic (counted). A1 is bit-exact in all 2,496,260 cells tested, but only on the port's one test machine (a shared 4-CPU cloud VM), where forcing numpy's CPU dispatch lower changed 44% of complex products (measured). The plan sized the shipped C++ within a factor of 2.2 (55,046 lines against 25,000) but its tests and bindings 13.6 times too small (95,095 lines against 7,000; counted). The port also found 127 bugs in the Python, 89 still open.

## What QRSSTVAE is

QRSSTVAE is an ultra-slow, very narrow HF picture mode built on SSTVAE. It sends the SSTVAE v5 codec's **latents**, 132x30x40 numbers per picture, as analog values on one narrow signal, in passes of about 30 minutes. Repeat passes of one picture combine for sensitivity, and several stations fit in 2.4 kHz. **CE** (constant envelope, for Si5351 rigs such as the uBitX, 99% of its power in 51.5 Hz) has been on the air in format v2 since 2026-10-10. **L** (a pilot tone plus QPSK for linear SSB rigs, 21.5 Hz wide, PAPR 3.15 dB) has been on the air since 2026-10-12, in mode A on 14.066 MHz.

SSTVAE sends the same latents on 24 OFDM carriers in about 1.2 kHz, a picture in 32 to 95 s; QRSSTVAE takes about 56 times the airtime in a signal 23 to 56 times narrower, and works roughly 10 to 30 dB further below the noise (2026-10-12 document). The port followed the method of SSTVAE's own C++ port: Python stays the reference, the port goes module by module, and pytest --native-qrss=tx|rx runs the Python suites against the C++.

## Top picks

1. **F1: have the app send L's lead-in by default.**
    - The app never sets lead_in_s (native/gui/qrss_tx.cpp:251-265), so L passes have none. A steady lead-in measured about +2 dB of live A1 depth for L, 5 to 6 dB once L1 (an A1 window over the lead-in) looks at it.
    - One line in the app, the three 0.0 s defaults, and the new setting with its settings-fixture and dialog round-trip entries (inferred); the transmitter parity tests keep the three defaults in step.
    - On air: up to 10 s more key-down per L pass, 0.56% of a FULL frame (computed); format unchanged.
2. **B1: delete a tile's picture when the tile leaves state.json.**
    - Neither listener unlinks an expired tile's PNG (sstvae/qrss/live.py:1642-1645, native/core/qrss/listener.cpp:1635): up to 96 a day per signal heard every slot (computed). Three smaller slips ride along, all four copied by the C++; about 15 lines per language (inferred).
    - On air: no change.
3. **S1 and S2: find and release what a FULL listen session holds.**
    - A FULL receive needs 57% of the Python's memory; a FULL session needs 1263 MB against at least 1290 MB (measured). The leading suspect, onnxruntime's arena (the memory pool it keeps between decodes), holds about 340 MB (inferred); an hour of heap profiling settles it, and the fix is likely about 5 lines (inferred).
    - S2 streams a WAV through the front end bit-identically, cutting a FULL receive from 796 MB to roughly 360 to 520 MB (inferred), in about 100 lines.
    - On air: no change.
4. **B5, O12 and R10: lock the store across processes, then search back and re-receive from the app.**
    - The app's listener is a separate process (native/gui/qrss_window.cpp:526-535), and merges are guarded only within one process, so one of two concurrent merges can be lost (read from the code). A lock is 60 to 100 lines per language (inferred).
    - Then em_api::retro can recover a missed CE group-0 segment (the coarse picture later segments refine) to about -35 to -40 dB (inferred), in about 20 min over a 48 h FULL passband store (computed); re-receive gave +0.8 dB of latent SNR for CE at -12 dB (measured). No app or listener path calls either today; only sstvae-qrss decode --retro and --em do.
    - On air: no change; stored CE results move, so both need a flag until Jeff agrees.
5. **T1, T10 and T2: run the port's checks in CI, and check the chain test's picture.**
    - No CI job passes --native-qrss, none of the port's commits has run in CI, and the no-Python chain test (native/tests/qrss/test_em_chain.cpp) passes as a skip without models.
    - About 25 and 50 lines (T1, T2) run the substituted suites (the reference's suites against the C++) on four platforms on pinned versions, and about 120 (T10) check the no-Python release's decoded picture on all five (inferred).
    - On air: no change.
6. **O15 and S4: make live acquisition cheaper without changing a bit.**
    - Computing A2's p-value once per scale instead of once per cell (A2 is the preamble search) saves up to about 0.8 s of the 1.3 to 1.7 s a FULL acquisition of both waveforms takes (computed upper bound); AVX2 preamble sums save 0.15 to 0.3 s more (inferred). No committed reference values (pins) move.
    - A few lines each and about 40 of C++; the CPU matters on a small board or a battery.
    - On air: no change.
7. **B3: check the frequency path's spline fit, not only that it is finite.**
    - scipy's make_lsq_spline returned finite but wrong coefficients in 1,324 of 2,000 targeted trials (measured), so the receiver follows a wrong frequency path, and the C++ copies it bit-exactly (native/core/qrss/trackbase.cpp:183).
    - A residual check against a straight line, about 15 lines per language (inferred); how often real passes need it is unmeasured.
    - On air: no change.

**Build first: T1, then F1 and B1.**

- T1 with T2's constraints file, about 25 lines, so every later change runs on four platforms; it needs one CI run of waveform_l (P2), Jeff's to trigger.
- F1 and B1, which a station notices.
- S1's profile, which decides whether a FULL listener fits a 1 GB board; O15 and S4 fit anywhere.
- B5 before O12 and R10 reach the app; B3 last, after a decode A/B.

Land reference changes one at a time, regenerating rx_pins.json after each merge. K5, T5 and S5's strip can ride along. Rebuild the app on the station computer after F1, B1 and S1. B2 and B8 need a false-alarm recalibration first.

## What the port delivered

Jeff asked for the C++ on 2026-10-12. Next morning he relayed the maintainer's (Andrew, KC2G) wish for no Python in release builds, bringing stage 4 into scope; Andrew asked to match decoded pictures rather than intermediates, and Jeff left the details to the port. The resulting **parity bar**: the transmitter bit-exact (every int16 WAV and all CE output byte-identical; L's float output, through the clipper's FFTs, held to 1e-9 and in practice within one float32 ulp on up to 35 samples a pass); a release matching the reference's decoded picture and header; intermediate parity in the test harness only; and no promise about the store's file layout.

The commits, on waveform_l in baitisj/SSTVAE: stage 0, build wiring and the switch, 3843897; stage 1, the transmitter and qrss/api.hpp, 7268bc6, the app's sender since 2b6d29b; stage 2, the per-pass receiver, c0c830f; stage 3, the store, the passband store with its own FLAC encoder, and the listener, 89653b9, the app's listener since 39fa50c; packaging, 8cd20f5; and stage 4, EM refine, retro search and em_api.hpp, b84c1f8 plus 4e92d36. That is about 51 hours (Lessons). On 2026-10-15, 5e1ae13 (reviewed in 3787d20) made receive store passes and keep the passband, as qrss_receive.py does, and 17e68da cleared the warnings GCC 16 and Qt 6.12 raised in the station computer's build. The app needs no Python, though SSTVAE_QRSS_BACKEND=python still selects the Python transmitter and listener.

## Terms used throughout

- **reference**: the Python (sstvae/qrss/*.py, qrss_*.py), against which the C++ is checked.
- **pass, slot, q**: one transmission of a picture's segment (about 30 min at FULL), starting on quarter-hour slot q, q x 900 s after the Unix epoch.
- **TINY, SHORT, MEDIUM, FULL**: frame lengths; only FULL carries a picture.
- **group, segment**: modes A, B and C carry 1, 2 and 3 latent groups, one per pass segment; group 0 is the coarse picture.
- **multi-pass store**: the directory ($QRSSTVAE_HOME by default) of received passes and **accumulators**, each combining every pass of one callsign's picture.
- **passband store**: 48 h of the front end's 4 kHz I/Q, packed to FLAC, for searching back.
- **tile**: a detected signal's entry in the listener's state.json (a PNG at FULL), which the app shows.
- **A1, A2, A3**: acquisition. A1 finds carrier or L pilot lines, A2 the preamble at each (most of live acquisition's time), and A3, a whole-slot track-before-detect search, what they missed.
- **joint**: listening for CE and L at once, the app's default; --just-ce listens for CE alone.
- **hint**: a frequency where the store expects a picture's next pass, searched by A2 whether or not A1 saw a line there.
- **gate V**: the acceptance test: a candidate tracked with its known symbols withheld passes when Z_ref, unit normal on noise, exceeds 6.
- **round A, round B, the finish**: round A receives live while a slot is heard; at its end the finish runs round B, verifying at most 12 candidates in 180 s.
- **EM**: expectation-maximisation: each stored pass re-tracked with the others' combined estimate as a prior (decode --em N); CE only.
- **retro search**: a known-waveform search back through the passband store for missed segments (decode --retro); CE only.
- **mean W**: a pass's mean latent weight in dB.
- **ulp**: unit in the last place, the gap between adjacent floats.
- **pins**: expected values generated from the reference and committed; rx_pins.json also hashes the reference sources the C++ receiver mirrors.
- **parity file**: one of the 61 tests/test_qrss_native_*.py files, which run the reference and the C++ side by side.
- **rows**: the table behind --native-qrss that swaps reference functions for their C++ bindings; the reference's suites run with every row applied are the substituted suites.
- **measured, computed, inferred**: run and timed or counted; arithmetic on code or measured numbers; reasoned, not run.
- **On air**: each entry's last clause, what the band would notice; "no change" leaves format and audio untouched.

Wave, lane, porter, reviewer and whole-caller A/B are defined in Lessons.

## How to read the entries

Short names: Lessons (What the port taught us), Measured, and the bugs list (The Python bugs the port found), which sits just before Correctness because its table uses Correctness's entries. Then Correctness (B1 to B15), Speed and memory (S1 to S8), the earlier ideas under their old IDs (F, R, O, L, D, C), Code health (K1 to K9), Tests (T1 to T10) and Running the next port (P1 to P9). IDs are never renumbered, figures carry labels, a pair of figures written "X against Y" gives the C++ (or the actual) first, and every idea entry ends "On air:". Unless an entry says otherwise, a receiver change lands in the reference and the C++ together and regenerates rx_pins.json and the module's pins (about a minute of Python); entries mention pins only when more moves. Everything is experimental, so no entry weighs older listeners.


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


## The Python bugs the port found

Porting the reference line by line, and then reviewing each module against it, found 127 distinct bugs in the Python. The count leaves out stale comments, dead code, test-only defects and notes on how the detection thresholds are calibrated; the threshold edge cases that are counted are in B8. 38 are fixed: 29 in both implementations and 9 in the Python only. 89 are still open. The C++ reproduces 52 of them, most because a parity test or pin holds it to the reference; it already gets 29 right; and for 8 argument checks in internal functions it has no counterpart or its behaviour was not established. Below, "C++ too" means the C++ reproduces a bug and "C++ right" means it does not. Python files are in sstvae/qrss/; the qrss_*.py scripts are at the top level.

### Fixed

- **known_symbols took a short header**: one bit short gave 3,574 positions against 3,573 values. track.py:454-469; ce73aff, both.
- **Gate V fitted its coarse timing grid twice**, the second time for the runners-up; sharing it cut a Python FULL verify of a false candidate in noise from 74.5 s to 63.1 s for CE and from 63.1 s to 50.6 s for L (measured), bit-identically. track.py:504, ltrack.py:984; ce73aff, both.
- **One bad hint lost the slot**: a NaN or infinite hint raised inside A2 and took every candidate's acquisition with it. acquire.py:1582, :1601-1608, :1622; ce73aff, both.
- **_receive_pass_l accepted a CE frame** and died on a None attribute instead of refusing it. receiver.py:344-345; ce73aff, both.
- **--image was checked after the receive**: a bad path failed after up to about 35 s of FULL receive, losing the result, and formats Pillow cannot write passed. qrss_receive.py:75; ce73aff and 73c1d3d, both.
- **Slots before 1970 were not refused**: the Python died in the scrambler after acquiring and tracking, and the C++ wrapped q and printed a pass. sequences.py:184-190; ce73aff, with 2671a6b for the C++; both.
- **load_npz trusted the archive**: an fs of 0 or a 2-D fe failed deep in the code, naming neither file nor member. channel.py:918-948; ce73aff, both, then 73c1d3d (Python only) for that fix's refusal of byte-swapped complex64.
- **qrss_listen.py ended a bad --slot in a traceback.** qrss_listen.py:185-188; c0c830f, Python only.
- **Association's correlation check never ran on short frames**: its median over all 50,600 latents stays 0 until 25,300 are covered, so on TINY, SHORT and MEDIUM another picture's pass attached under a colliding header unchecked. associate.py:173-190; b3596d0, both.
- **prov_add clobbered a provisional cluster**, dropping its members, and a parity sequence had pinned the bug. store.py:660; b3596d0, both.
- **pass_zw with the pass cache off** returned the entry it had just evicted, a KeyError. store.py:328; b3596d0, Python only.
- **due(), which lists the passes due in a slot, kept half an accumulator** when one raised partway. store.py:456-492; b3596d0, both.
- **The merge log's "accepted" changed type**: a number normally, a boolean when nothing overlapped. store.py:628; b3596d0, both.
- **An empty HOME** put the store in /.local/share/qrsstvae. store.py:67-80; b3596d0, both.
- **The store's caches never looked at the files**, so a listener's next rebuild undid a decode --em run in another process. store.py:129-136; b3596d0, widened to inode and change time in 66c0abd; both.
- **Temporary names were per process, not per thread**: two threads could collide, and two pack_hour calls on one hour could rename a half-written FLAC into place. store.py:123, frontend.py:532; b3596d0, d6ea91e, 4982928 and 033e7a5; both.
- **Dotted callsigns shared a file**: "K1.AB" and "K1.CD" both became acc/K1.json. store.py:396-398; 66c0abd, both.
- **A malformed accumulator dropped every picture's hints**: a TypeError, and after that fix an OverflowError, escaped due(). store.py:456-492; 66c0abd and b7506d6, both.
- **A uid that is not a string** ended decode --list with AttributeError. store.py:301-306; 375a85c, both.
- **pack_hour's stat sat outside its try**: a raw hour that vanished mid-pack lost every later hour in that pass. frontend.py:581; d6ea91e, both.
- **A negative passband fs** made read() loop forever. frontend.py:521-522; d6ea91e, both.
- **_HOUR_RE was too broad**, matching non-ASCII digits and a trailing newline. frontend.py:459; d6ea91e, Python only.
- **_merge_ranges leaked numpy integers**, which json refuses; unreachable. frontend.py:808; d6ea91e, Python only.
- **write_capture with an infinite sample** raised OverflowError in the gain. frontend.py:720-737; d6ea91e, both.
- **The pack swap trusted mtime alone**: on FAT or exFAT a write during the roughly 1 s pack was deleted with the raw hour. frontend.py:581; d6ea91e, Python only (it gained the C++'s write counter).
- **The packed FLAC was not synced**: a power cut could leave an empty FLAC and no raw hour. frontend.py:603; d6ea91e in the Python, 2490e9f in the C++.
- **Crash leftovers stayed forever, and the first sweep for them could stop writes**: it raised out of every write on a temp-named directory or a file Windows held open. frontend.py:779-800; d6ea91e and 02500ac, both.
- **A short raw hour was handled two ways**: read raised while write silently grew the file. frontend.py:693; d6ea91e, both.
- **One corrupt dial-log line stopped the poll**: in the dial log, the app's record of dial changes, an infinite dial_hz raised OverflowError on every refresh until the file rotated. live.py:477-498; 098f743, both.
- **The dial log was read in the locale's encoding**: under the C locale a U+0085 lost two dial changes. live.py:487; 098f743, Python only.
- **restrict read its spans twice**, so spans given as a generator gave an all-False mask. live.py:558; 098f743, Python only.
- **The "signal(s) at" log line repeated** once per accepted pass instead of once per refresh. live.py:1372-1374; a12c3b8, both.
- **FeRing, the listener's ring of front-end samples, wrote behind its window**, overwriting newer samples. live.py:211-230; 098f743, both.
- **NaN reached state.json**, so the app's JSON reader rejected the file and lost every tile. live.py:668; 098f743, with a142bd3 in the app; both.
- **A wrong-shaped state.json stopped the listener starting** until someone removed it. live.py:867; 098f743 and a12c3b8, both.
- **One failed live refresh retired the slot**, losing its remaining tiles and its end-of-slot receive. live.py:1151-1173; 098f743, both.
- **Tile.from_state took None** in float fields that are not optional. live.py:619; 4982928, Python only.
- **qrss_listen.py aborted after Ctrl-C**, exiting with status -6 (SIGABRT), because StdinSource's thread held BufferedReader's lock. live.py:1721-1727; 033e7a5, Python only.

### Still open

The 89 open bugs, counted by the Correctness entry that would fix them:

| Entry | Open | C++ too | C++ right | Not established |
|---|---|---|---|---|
| B1, the listener's state | 4 | 4 | 0 | 0 |
| B2, gate V's runner-up | 1 | 1 | 0 | 0 |
| B3, the frequency path's spline | 4 | 2 | 2 | 0 |
| B4, damaged accumulators | 4 | 3 (1 inferred) | 1 | 0 |
| B5, the store across processes | 1 | 1 | 0 | 0 |
| B6, a crashed recorder's WAV | 2 | 2 | 0 | 0 |
| B7, 16-bit pictures | 1 | 0 | 1 | 0 |
| B8, acquisition and association edges | 6 | 6 (2 inferred) | 0 | 0 |
| B9, the pulse table and integer phase | 6 | 2 | 4 | 0 |
| B10, file and command-line slips | 10 | 6 | 4 | 0 |
| B11, non-finite numbers | 14 | 10 (1 inferred) | 4 | 0 |
| B12, the Python listener's threads | 8 | 0 | 8 | 0 |
| B13, frame, waveform and header checks | 9 | 4 | 2 | 3 |
| B14, degenerate sizes and ranges | 15 | 8 | 2 | 5 |
| B15, the timing fits | 4 | 3 | 1 | 0 |
| Total | 89 | 52 | 29 | 8 |

Each entry in Correctness lists its bugs and which the C++ reproduces.


## Correctness: the open bugs, ranked

The 89 open bugs in the reference fall into fifteen entries, ranked by expected value: what a fix is worth, times how likely the failure is in use, over what it costs. Other things equal, a bug the C++ reproduces ranks higher, because the C++ is what ships and a Python-only bug reaches only the scripts and the parity harness; B7 and B12 are Python only but outrank entries no current input reaches.

**B1. Delete a tile's picture when the tile leaves state.json, and tidy the listener's other state slips (both listeners)**
- *What:* A finished tile leaves state.json 6 h after its last update (sstvae/qrss/live.py:1131-1133), but neither listener unlinks its PNG (live.py:1642-1645, native/core/qrss/listener.cpp:1635); unlink it then, and sweep orphans at start-up. Three slips ride along: "finished" lists only slots within 48 h (live.py:912-913, listener.cpp:832), hiding a recovered old slot; the last live refresh races the slot's finish and can drop its log line (live.py:1358-1359); and both write listener.log and state.json with CRLF on Windows (listener.cpp:164-176). The C++ copies all four.
- *Why we think so:* read from both listeners; the race dropped the line in about 1 of 12 C++ TINY replays (measured).
- *Gain:* bounded disk; a station left listening otherwise keeps up to 96 PNGs a day per signal heard every slot (computed).
- *Cost:* Small: about 15 lines per language (inferred).
- *Risk / constraints:* the app reads both files, so a tile must leave state.json before its PNG goes. On air: no change.

**B2. Track gate V's strongest runner-up instead of the best timing twice (both; recalibrate first)**
- *What:* verify and verify_l track the best coarse timing, then loop over tms[1:] (sstvae/qrss/track.py:1450, ltrack.py:1301) as if tms[0] were that timing. On L noise, after segment refinement, it is not: tms[1] is tracked twice and tms[0] never. The C++ copies the loop (native/core/qrss/track_timing.cpp:1005, ltrack_timing.cpp:750). Loop over all of tms, skipping the one already tracked.
- *Why we think so:* inferred in wave 6a and measured in wave 7; test_fit_timing_tries_l pins the split.
- *Gain:* 8 distinct timings (V_TIMING_TRIES, track.py:150) instead of 7 (computed); the effect on weak L signals is unmeasured.
- *Cost:* Trivial: about 3 lines per language, plus the tests that pin today's order (inferred).
- *Risk / constraints:* one more distinct try is one more chance for noise to pass, so re-run gate V's false-alarm calibration first. On air: no change.

**B3. Check the frequency path's spline fit, not only that it is finite (both receivers)**
- *What:* PathFn keeps scipy's make_lsq_spline fit whenever its coefficients are finite (sstvae/qrss/track.py:270-275), but scipy 1.18.1 returns finite, wrong coefficients when early points are sparse before a gap, so the receiver follows a wrong frequency path; the C++ reproduces this bit-exactly (native/core/qrss/trackbase.cpp:183). Fall back to the straight line when its weighted residual is lower. Riding along: uncaught errors on NaN or degenerate times (track.py:253-262; C++ too), and an unstable argsort and unchecked lengths (track.py:255-258; C++ right).
- *Why we think so:* measured in wave 4: residuals 10^2 to 10^5 times the optimum in 1,324 of 2,000 targeted random trials.
- *Gain:* a correct path when a pass starts sparse and then fades; how often real captures do that is unmeasured (inferred).
- *Cost:* Small: about 15 lines per language (inferred).
- *Risk / constraints:* outputs move only where the spline was wrong. On air: no change.

**B4. Stop one damaged accumulator from stalling the store (both stores)**
- *What:* due() loops over range(mode), so a huge mode never ends, hanging the listener's hint pass: 10**20 in the Python (sstvae/qrss/store.py:480), 10**18 in the C++, which refuses values past int64 as it parses (Store::due in native/core/qrss/store.cpp). open_keys and find_member raise on a malformed accumulator (store.py:448-454, :494-498; Store::open_keys and Store::find_member), failing attach and update_pass for every pass. em_refine stores members one at a time (sstvae/qrss/em.py:378-394), so a damaged member leaves the store half refined. And member_wave's narrow catch (qrss_decode.py:57-65) lets a truncated pass file end --list part-way. Bound mode, skip malformed files as due() does, check members before em_refine writes, and widen the catch. The C++ copies the first three (the em_refine one inferred) and prints "?" for the last.
- *Why we think so:* a fuzzer hung the Python on mode=10**20 (wave 9b); the rest is read from the code.
- *Gain:* a damaged or hand-edited file costs one picture instead of the listener (inferred).
- *Cost:* Small: about 30 lines per language (inferred).
- *Risk / constraints:* outputs change only for damaged files. On air: no change.

**B5. Lock the store across processes (both stores)**
- *What:* An accumulator's read-modify-write is guarded only within one process: the C++ shares one recursive mutex per root (native/core/qrss/store.cpp's root_mutex), and the Python Store relies on the listener's lock. A listener holds the directory lock for its whole life, but decode --em, --retro, qrss_receive.py and, since 5e1ae13, sstvae-qrss receive take none (native/apps/sstvae_qrss/receive.cpp's header comment says so), and native/core/qrss/em_api.hpp:68-90 invites the app to run them beside a session. One of two concurrent merges can then be lost, since b3596d0's cache signatures catch only a stale read. Take an advisory lock per accumulator around each merge, on the pattern of native/core/qrss/dirlock.hpp.
- *Why we think so:* read from the code; no lost update has been observed.
- *Gain:* the app's listener is a separate process (native/gui/qrss_window.cpp:526-535), so an em_api call or decode --em beside it makes two writers (inferred), and a lost merge drops a pass's latents from a picture. This is the prerequisite for the app running O12 and R10 over em_api beside its listener.
- *Cost:* Moderate: about 60 to 100 lines per language (inferred).
- *Risk / constraints:* both implementations must share one lock protocol on the same files, and the lock must cover one merge, never a receive or an EM round, or the listener stalls. On air: no change.

**B6. Read the WAV a crashed recorder leaves behind (both WAV readers)**
- *What:* A streaming recorder patches its RIFF sizes only when it stops, so one killed mid-capture leaves sizes of 0 or too small. scipy's wavfile then fails with UnboundLocalError (sstvae/wavio.py:12; scipy's wavfile.py:729, :786) and the C++ refuses identically. Read the data chunk to the end of the file when the declared sizes are 0 or short. Riding along, C++ too on purpose: scipy misreads 8-bit PCM in a wider container and an odd-length data chunk (wavfile.py:492-493, :515-516, :541).
- *Why we think so:* reproduced side by side in the wave 6b receive review (cases riff-size-0 and riff-size-fmt-only).
- *Gain:* a killed recorder's capture, up to a 30 min pass, decodes instead of being lost (inferred).
- *Cost:* Small to Moderate: about 40 lines in the Python and 20 in native/core/qrss/wavread.cpp (inferred).
- *Risk / constraints:* sstvae/wavio.py is shared with SSTVAE, whose suite must stay green. On air: no change.

**B7. Reduce 16-bit pictures the way the C++ does (Python only)**
- *What:* sstvae/images.py:133-134 converts any picture that is not RGB with img.convert("RGB"), and Pillow clips a 16-bit greyscale PNG (mode I;16) at 255, so most of the picture comes out white. The C++ uses stb_image (native/core/images/images.cpp), which reduces 16-bit samples to 8 bits, so a release already sends the right picture. Shift 16-bit modes to their high byte first, as stb_image does (inferred), and add the case to the transmitter parity test.
- *Why we think so:* measured in the stage 1 completion review: one file gave picture ID 90cb2490 from the Python and 231e67e5 from the C++ (the only other recorded mismatch, a picture that is not 640x480, is a resize difference).
- *Gain:* the right picture from the Python scripts, and a bit-exact transmitter on this input class (measured mismatch, inferred fix).
- *Cost:* Small: about 10 lines and one parity case (inferred).
- *Risk / constraints:* images.py is shared with SSTVAE, whose 16-bit inputs improve too and whose tests must stay green. On air: no format change.

**B8. Settle the acquisition and association edge cases with one measurement (measure first)**
- *What:* Six edge decisions, all reproduced by the C++ (two of them inferred):
  - a strong preamble underflows gammaincc at every scale, np.argmin ties to scale 0, and preamble_search refines df and tau there whatever scale carries the signal (sstvae/qrss/acquire.py:852-857), from about 0 dB SNR for CE;
  - drop_symbol_rate_aliases drops an alias only within T/2 (15.2 ms), so a 0 dB CE signal hinted at its -1/T alias gave two preamble hits 19.4 ms apart (acquire.py:1497-1499);
  - an L hint within 2.5 Hz of a failed A1 hit reuses that hit unsearched (acquire.py:1587-1588, :1666-1667);
  - noise-free TINY and l-tiny audio gives spurious grating-lobe passes (receive_slot, receiver.py:748), so the chain tests add noise at -6 dB;
  - association rule 2 pairs passes by raw f_hz within 10 Hz (associate.py:68, :294), while Store.twin first moves an L pass by -9.5 Hz (store.py:569-574), so a CE and an L pass of one signal pair with 0.5 Hz to spare (computed);
  - TBD_GUMBEL_MU[0] is 9.845 where the reference's own fit, 9.8443, rounds to 9.844 (acquire.py:226).
- *Why we think so:* probes in waves 4 to 7, and the code for rule 2.
- *Gain:* unknown until measured; the right refinement scale may sharpen tau and df on strong signals, and the alias fix saves a gate V run (both inferred).
- *Cost:* Medium: a sweep over SNR and hint placement for both waveforms, then a few lines per decision (inferred).
- *Risk / constraints:* each change moves the receiver's output, so the false-alarm tests re-run; constants go through tools/gen_qrss_rx_config.py. On air: no change.

**B9. Freeze the pulse table and make the transmitter's integer arithmetic exact (mostly Python)**
- *What:* ce.PULSE_TABLE is computed at import with numpy's trigonometry (sstvae/qrss/ce.py:101), so another host's libm could move it by an ulp; the C++ uses a frozen table. Three integer computations wrap where the C++ is exact: ce.to_audio's int64 phase product (ce.py:426-427) for carrier denominators above about 380,000 with a large n0, lwave.mix_turns' (lwave.py:148-153) for periods past about 3e9 samples, and _data_symbols on numpy unsigned input (frame.py:60-70). Two more the C++ copies: phase_at steps at whole symbols because the pulse is cut at 8 symbols (ce.py:95-98, :166), changing off-grid chunks more than 2048 s after t0 by up to 7.4e-3 on a signal of amplitude 0.5; and lwave's interpolation filter is designed for 8 kHz only (lwave.py:332-334).
- *Why we think so:* probes in the wave 1 and 2 reviews.
- *Gain:* the bit-exact transmitter stops depending on the host's libm (inferred); normal slots keep n0 under 1.5e7 samples and no preset reaches 2048 s, so the rest is insurance (computed).
- *Cost:* Small, about 40 lines: PULSE_TABLE committed as data beside sstvae/modem/interleaver_perms.npy with a verify script, two int() casts, and Python integers for the products (inferred).
- *Risk / constraints:* freezing keeps today's values, so neither the C++ nor the transmitter references move; fixing phase_at changes the audio only past 2048 s after t0. On air: no change for any preset.

**B10. Fix the small file and command-line slips (both CLIs)**
- *What:* Ten small slips. The one that loses data: beaconfile.write truncates the target before to_bytes validates (sstvae/qrss/beaconfile.py:151-153), so a refused BeaconFile empties a good file. The C++ copies it and five more: an empty --slot or --start counted as absent (qrss_receive.py:229-230), a long --key callsign ending in an ENAMETOOLONG traceback (qrss_decode.py:166), a negative picture ID printed as "-0000001", a bytes path read as the file name "b'...'" (receiver.capture_from_wav), and four options on which the CLIs disagree, one of them the precision default (fp16 for encode, fp32 for receive, decode and listen). It gets four right: qrss_transmit.py raises UnicodeEncodeError after writing the WAV when the name is not UTF-8; the reference codec cannot load a model from a directory whose name is not UTF-8; qrss_decode.py's --em N has no cap, where the C++ clamps at 2^31 rounds; and a passband meta.json that is not JSON ends --retro in a traceback (sstvae/qrss/frontend.py:520).
- *Why we think so:* probed side by side in waves 2, 6b, 7, 9a and 10.
- *Gain:* each saves one confusing failure, and the beaconfile fix saves a file (inferred).
- *Cost:* Trivial each, about 40 lines per language together, once someone decides which CLI behaviour is right (inferred).
- *Risk / constraints:* the side-by-side CLI tests pin today's texts, and the precision default decides which codec artifact a station fetches, so change it in both CLIs together. On air: no change.

**B11. Write one policy for non-finite numbers and apply it everywhere (both)**
- *What:* Fourteen places handle NaN and infinity ad hoc. The C++ copies ten (one inferred), whose NaN rows the em pins hold: template_z and _gauss_z(-inf) (sstvae/qrss/em.py:406-427); a NaN weight poisoning an lmmse_latents block (em.py:118); em_refine's stop test on an empty accumulator (em.py:142-148, :399); verify's NaN first Z_ref (track.py:1445); _phase_line's unguarded sxx (acquire.py:823); load_npz's NaN t0_index (channel.py:945); an empty mean weight, floored at -99 dB in the store, -300 dB in the receiver and -inf in EM (store.py:103-106, receiver.py:856-858); and refine_timing's Newton step, which keeps a NaN Hessian because np.linalg.eigh returns NaN without raising (track.py:1302-1306). It already handles the other four: cw_phase_turns (ce.py:280-302), the casts in write_wav and to_int8 (sstvae/wavio.py:43-47, picture.py:167-170) and _clock_phase (ltrack.py:952-956). Write one rule (refuse at the boundary, never propagate) and apply it.
- *Why we think so:* probes and fuzzing in waves 2 to 10, and the em pins.
- *Gain:* none measured on real captures, since almost all need a NaN that the front end does not produce and Store.merge refuses (inferred); one floor instead of three gives the store, the receiver and EM one number.
- *Cost:* Moderate: about 50 lines per language (inferred).
- *Risk / constraints:* a new floor changes stored mean_w_db values. On air: no change.

**B12. Make the Python listener thread-safe and let it shut down cleanly (Python only)**
- *What:* The C++ listener gets all eight of these right; the Python one, still run by qrss_listen.py, the parity harness and SSTVAE_QRSS_BACKEND=python, does not. _refresh reads store.due and _keep mutates s.dets outside the lock, and _busy is written without it, so idle() can read True just before a receive starts (live.py:1155, :1255-1264). Association and rendering run under listener.lock (live.py:1401-1440). A synchronous step() still raises a refresh's exception (live.py:1144-1148). StdinSource's reader dies on a read error without queuing the end, so chunks() waits forever (live.py:1736-1752). qrss_listen.py writes the last state.json before an in-flight receive finishes (qrss_listen.py:203-209), so that tile never appears and recover() skips its slot. SIGTERM leaves listening=true, write_state's os.replace failure is uncaught (live.py:913-916), and recover() raises on a uid that str.isdigit accepts and int() refuses (live.py:1049-1050).
- *Why we think so:* read from the code and in the wave 9a and 9b listener reviews; the C++ shared the shutdown order until Session::Core::shutdown gained a final write.
- *Gain:* none for a release; a Python listener that cannot hang on a dead pipe or lose its last tile (inferred).
- *Cost:* Moderate: about 80 lines in live.py and qrss_listen.py (inferred).
- *Risk / constraints:* Python only; the whole-caller listener tests must still agree with the C++. On air: no change.

**B13. Check frames, waveforms and headers where they enter a module (mostly Python)**
- *What:* Nine checks missing from internal functions that no caller trips today. spread_signs checks only the header's shape, so a bit of 2 gives a sign of -3 (sstvae/qrss/frame.py:302-308). make_classes broadcasts a short header onto a -v1 frame (track.py:744-745). Four ce functions accept an L spec and six ltrack functions a CE spec, failing deep in layout (ce.py:124 to :471, ltrack.py:284 to :501). _l_only is a bare assert that python -O strips (ltrack.py:124-126). L_KEYED_POWER falls back to 1.0 for a frame it does not list (ltrack.py:869). CE demodulation reads track fields more loosely than ldemod (demod.py:71, ldemod.py:148-160). extract overwrites track.m on its argument (demod.py:238-239, ldemod.py:323-324), which EM relies on. And pilot_wave raises on a scalar tau where _pilot_gain answers (ltrack.py:319-336). The C++ copies make_classes' broadcast, the ce layout check, extract's write-back and L_KEYED_POWER's 1.0 default (keyed_power_db in native/core/qrss/ltrack.cpp), already refuses bad spread_signs bits and has no _l_only assert; the reviews did not establish its behaviour for the other three.
- *Why we think so:* probes in the wave 4, 6a and 7 reviews.
- *Gain:* a header-decode bug producing bits other than 0 and 1 would stop with an error instead of silently corrupting round B's spread subtraction (inferred); no decode changes today.
- *Cost:* Small: one guard per function, about 40 lines (inferred).
- *Risk / constraints:* extract's write-back is load-bearing for EM, so make it an explicit assignment in em.py first. On air: no change.

**B14. Refuse degenerate sizes and ranges instead of answering them (both)**
- *What:* Fifteen edge inputs that no current caller reaches. The worst: track_before_detect on a band narrower than its clip span returns a confident hit at the wrong frequency, a 1030 Hz candidate in 1000 to 1060 Hz giving a centre of 1020 Hz (sstvae/qrss/acquire.py:1270). The C++ copies eight: frontend.blank and live.capture_prepare on 1 to 100 samples; hint_line's and _phase_line's return types, which depend on the value; _path_line under 2 points; a single-frame (f, waveform) tuple hint; receiver._owned, which never checks a3 entries past the deciding one; em.rereceive on receive_pass's bare None; em._chunk_template on an empty span; and template_search's refusal, which depends on whether any candidate reaches the capture. It gets two right: track_before_detect on a narrow band, and template_search on a negative tau_s, which the reference answers with meaningless cells. Its behaviour was not established for five: a negative chunk, which leaves four functions returning uninitialised buffers; three lru_cached front-end arrays left writeable; derotate on a scalar path; _cell_line's out-of-range drift index; and tbd_viterbi's max_step between one and two band widths.
- *Why we think so:* probes and pins from waves 4 to 10.
- *Gain:* none on today's captures (inferred); the wrong-frequency hit is the one that would mislead a future caller.
- *Cost:* Small to Moderate: about 60 lines in the Python and 20 in the C++ (inferred).
- *Risk / constraints:* the C++ changes only where it copies. On air: no change.

**B15. Make the timing fits consistent with themselves (both; nothing reaches it today)**
- *What:* Four latent inconsistencies. _segment_timing's single-segment fallback fits with the outer delta, not the loop's d_ (sstvae/qrss/track.py:667, ltrack.py:1127). refine_timing_l refines against u_pl and P_pl from the old timing, so a second call moves tau0 by +0.915 samples of the 250 Hz channel on l-short and -0.583 on l-tiny (ltrack.py:699-747). fit_timing's FFT path (above 64 lags) and direct path disagree on an interpolation pair straddling an end of the capture, which channel_for's zero padding hides; the C++ follows the direct path. And L scores its timing statistic through the ok mask while CE does not (ltrack.py:1090-1110 against track.py:624). The C++ copies the other three.
- *Why we think so:* the wave 5 and 6a reviews; unpadded, the FFT path was off by more than 1e-3 of the grid's scale against 1.95e-15 for the C++, and test_receiver_channel_holds_every_lag pins the padding.
- *Gain:* none today (measured for the padding, inferred for the rest); fixing them makes a later change to the padding or the segment walk safe.
- *Cost:* Small for the first and last, Moderate for the middle two (inferred).
- *Risk / constraints:* the middle two change outputs, so they want a decode A/B before landing. On air: no change.


## Speed and memory of the C++

The C++ beats the Python on every whole command measured but one, an L FULL pass written as int16, and it uses less memory everywhere but a FULL listen session (see Measured). The entries below are ranked by expected value: what a change is worth to an operator, times the chance it pays off, over what it costs. Memory comes first, because it decides whether a small station can run the listener at all. None of them changes the Python, and only S7 can change a result, when the finish's time budget binds.

**S1. Find what holds a FULL listen session's 1263 MB, then release it (measure first)**
- *What:* Run the FULL listen replay (refresh every 600 s) under heaptrack or valgrind's massif, then fix whatever leads. The suspects, in order:
  - The codec. The listener keeps its decoder for the whole session (native/core/qrss/codec/listener_render.cpp:47-48) with onnxruntime's CPU arena on (native/core/codec/codec.cpp:47-52), and the arena keeps its largest decode's activations. A stored-pass decode peaks at 359 MB against 20 MB at start-up (measured), so roughly 340 MB (inferred).
  - Two workers. The live refresh and the end-of-slot finish run on separate threads (native/core/qrss/listener.cpp:477-478), and at a FULL slot's end both can hold a 30-minute capture and its tracks.
  - glibc's allocator, which keeps memory freed below the mmap threshold in each thread's arena. MALLOC_ARENA_MAX=2 and MALLOC_MMAP_THRESHOLD_=131072 (bytes; glibc starts there and raises it as it goes, and setting it stops the raising) test it without a code change.
  - The replay. WavSource holds the whole --wav recording as doubles (native/core/qrss/sources.cpp:465), about 110 MB at FULL (computed); a live session fed from stdin holds none, and S2 removes it from replays.
- *Why we think so:*
  - A FULL WAV receive's 796 MB is about 768 MB of batch front end at 56 bytes per 8 kHz sample (computed: the audio as doubles, the baseband, the convolution's buffer and its "same" copy). With the 20 MB start-up floor, the model gives 49, 136 and 788 MB against 54, 143 and 796 MB measured for TINY, SHORT and FULL. The listener never runs it: a stream front end feeds a 64 MB ring, 2100 s of 4000 complex floats a second (computed; native/core/qrss/listener.cpp, the fe_.push loop in LiveListener::feed; native/core/qrss/rx_config.hpp:264).
  - A FULL receive without the batch front end still costs about 0.4 to 0.5 GB (inferred from the 508 to 519 MB measured for FULL receives from a .npz), and two can overlap.
  - TINY and SHORT replays (94 and 209 MB, measured) are also much shorter, so they cannot separate the picture from the length. The ring, two receives and the 110 MB replay WAV come to roughly 1.0 to 1.2 GB (inferred), so only the profile can tell.
- *Gain:* Up to about 340 MB (about 27%) off a FULL session if the codec's arena holds most of it (inferred); the allocator settings are a cheap test, not an expected win. That could decide whether a FULL listener fits on a 1 GB single-board computer beside its operating system (inferred). Separately, the app's own send path, a different process from the listener, holds about 220 MB while it sends a FULL pass: the float32 audio copied to doubles, plus a conditioned copy of those (native/gui/qrss_tx.cpp:318-319, :495; native/core/tx/engine.cpp:77; computed). Conditioning in place drops 110 MB of that (computed), and a small board that sends and listens holds both.
- *Cost:* About an hour to profile. The likely fix is Small: a QRSS-only session option turning the arena off, about 5 lines (SSTVAE's receive loop shares those options and must keep its setting), or mallopt calls at listener start, about 3. Making the workers take turns would be Moderate. Conditioning the send audio in place is Trivial, about 5 lines.
- *Risk / constraints:* No arithmetic changes, so pictures, headers and pins stay as they are. Without the arena each decode allocates afresh, a small cost against a 0.57 s decode (measured). If the workers take turns, a tile's picture can wait about 10 to 15 s at FULL for the refresh in flight (inferred from the FULL receive times). On air: no change.

**S2. Stream a WAV through the receiver's front end (bit-identical)**
- *What:* capture_from_wav reads the whole file (native/core/qrss/wavread.cpp:547-559), converts it all to baseband and convolves it all before decimating by 2 (native/core/qrss/frontend.cpp:378-390, native/core/qrss/receiver_slot.cpp:512-515). Fuse those steps: read blocks, convert each with dsp::to_baseband_at (native/core/dsp/dsp.hpp:41), feed the overlap-add batches as they fill, and write the decimated output straight into the capture, which also drops the convolution's copy of its "same" slice (native/core/math/convolve.cpp:212-217, :283).
- *Why we think so:* S1's 56-bytes-per-sample model matches the measured receive peaks within 8 MB at all three frame sizes, and only the capture, 8 bytes per 4 kHz sample, must outlive the front end.
- *Gain:* A FULL WAV's front end falls from about 768 MB to under 90 MB (computed). A FULL receive from WAV would then peak at roughly 360 to 520 MB, against 796 MB today and the Python's 1393 MB (inferred, from a codec decode's 359 MB up to the 508 to 519 MB of a FULL receive from a .npz). That brings a recorded FULL pass within reach of a 1 GB machine.
- *Cost:* Small to Moderate: about 100 lines in frontend.cpp and convolve.cpp, plus a bit-for-bit test against the batch capture at TINY, SHORT and FULL lengths.
- *Risk / constraints:* Unlike the listener's stream front end, which matches the batch one only away from the first and last 128 audio samples, half its 255-tap filter (native/core/qrss/frontend.hpp:224-260, native/core/qrss/rx_config.hpp:260), this capture can be bit-identical: the WAV header gives the length that fixes the convolution's block layout (convolve.cpp:238, :258) before any sample is read, to_baseband_at reduces each phase exactly, and the overlap-adds keep today's order. A WAV at another rate is resampled whole (wavread.cpp:660), so it keeps the batch path unless a chunked resampler holds the same bits. No pins move. Both receive and listen --wav gain. On air: no change.

**S3. Render an int16 L pass once, and make its two hot loops cheaper (bit-identical; CLI only)**
- *What:* Three changes to the transmitter, cheapest first.
  - Render once. An int16 WAV renders the pass twice, once for np.max(np.abs(x)) and once to write it (native/core/qrss/tx.cpp:452-456, :459-465). Keep the first render when it fits, 110 MB of doubles at FULL (computed), or thread both renders' blocks, which keeps today's peak (PassAudio::render is const, tx.cpp:378-402).
  - Use a table for the carrier. ce::to_audio calls glibc's cos and sin for every sample of both waveforms (native/core/qrss/ce.cpp:563), but the phase is an exact fraction of a turn, an integer over 8000 × den (ce.cpp:561). A carrier of num/den Hz therefore repeats every 8000 × den / gcd(num, 8000 × den) samples: 16 at 1500 Hz, and at most 80,000 for the app's carriers, which have one decimal place (computed). A table of one cycle gives the same values; a longer cycle calls libm as today.
  - Vectorise the resampler. lwave::resample_into sums about 15 products per output sample in ascending order (native/core/qrss/lwave.cpp:486-532). Outputs sharing a polyphase phase share taps, so 4 can be summed at once in AVX2 registers, each in today's order with multiply and add separate, behind the dispatch at native/core/math/convolve.cpp:136.
- *Why we think so:* Measured's callgrind profile (43% in resample_into, about 28% in the carrier's sin and cos) and the float WAV's 2.58 s in the C++ against 3.86 s in the Python put the int16 gap in the second render. The reference pays libm once per sample too, because numpy's complex exp calls glibc on x86-64 (native/core/qrss/ce.hpp:25-26; sstvae/qrss/ce.py:428), so the table would put the C++ ahead rather than close a gap.
- *Gain:*
  - L FULL int16, the only command measured where the C++ loses to the Python (4.94 s against 4.30 s), drops to about 2.6 s by rendering once (computed from the float WAV's time), and to roughly 1.5 s with the table and the vector resampler (inferred), about 3 times faster than the Python. CE FULL int16 drops from 2.80 s to about 1.4 s by rendering once (inferred from its float WAV's 1.36 s).
  - The app renders float audio once, 60 s ahead (native/gui/qrss_tx.hpp:57), so an operator sees none of this.
- *Cost:* Rendering once is Trivial with a cache (about 10 lines) or Small with threads (about 30). The table is Small (about 30 lines), the vector resampler Moderate (about 80, with its scalar twin and the dispatch).
- *Risk / constraints:* The int16 WAVs and CE output stay byte-identical to the Python and the L float WAV as today, under the tx_audio parity tests and digest pins. Caching raises an int16 FULL transmit's peak from 80 MB to about 190 MB (computed). The CE baseband's own phase (one turns_to_phasor call per sample, tx.cpp:365) never repeats, so no table replaces it. On air: no change.

**S4. Put A2's preamble sums on AVX2 (bit-identical to the C++ today)**
- *What:* The sums' multiply-accumulate (mac, native/core/qrss/acquire_a12.cpp:971) takes four timing offsets at a time so the compiler can pair them in vector registers, but the build targets the x86-64 baseline, whose SSE2 registers hold two doubles. Add an AVX2 twin behind the runtime dispatch that track_timing.cpp:237 already uses, with the same unfused multiply and add for every element (the library builds with -ffp-contract=off, native/CMakeLists.txt:229).
- *Why we think so:*
  - One A2 search spends 17.6 ms in preamble_sums, against 5.5 to 8.6 ms for numpy's BLAS zgemm calls (measured).
  - A joint FULL acquisition runs 31 searches, and A2 is most of live acquisition, which takes 1.24 s per slot in the C++ at TINY and SHORT alike (measured). (The p-value after the sums is O15.)
- *Gain:* Perhaps 5 to 9 ms per search, 0.15 to 0.3 s off a joint FULL acquisition of 1.3 to 1.7 s (inferred). That is CPU at every listener refresh, which matters on a small board or a battery, not for when a tile appears.
- *Cost:* Small: about 40 lines, plus a test that both paths give identical sums.
- *Risk / constraints:* Only the vector width changes, so the sums stay bit-identical and no pin moves. Blocking the sum as a BLAS does would change the last bits; since the C++ already matches numpy's sum only to rounding (A2's statistic Lambda to a relative 1e-9, 2 to 3.5% of the sums bit-equal; measured), that would mean rerunning the acceptance tests but no format change. On air: no change.

**S5. Strip the Linux package's binary, and build a headless sstvae-qrss without Qt (packaging)**
- *What:*
  - tools/package_app.sh copies the RelWithDebInfo binary into the Linux package unstripped (package_app.sh:191, the cp of $BUILD_DIR/sstvae-qrss; nothing there or in .github/workflows/native-build.yml strips): 90,441,960 bytes against 4,796,672 stripped (Measured; no package was built). Strip it, keeping a separate debug file if wanted. sstvae-gui is probably the same (not measured).
  - The codec build links sstvae-qrss to the model fetcher and Qt6::Core only for a first-run model download (native/CMakeLists.txt:727-732). --model and a warm cache work without it, main.cpp compiles both ways (native/apps/sstvae_qrss/main.cpp:41, :172), and a build without Qt6 Network already drops the fetcher (native/CMakeLists.txt:485-492).
- *Why we think so:* The fetcher brings Qt and its ICU data into the codec build (Measured: 57 libraries, 92.5 MiB, ICU 30.8 MB); the build without the codec needs only libstdc++, libm, libgcc_s and libc (measured).
- *Gain:* The strip takes about 85 MB off the Linux package for this binary alone (computed). A headless build depends on about 30 MB in about five libraries (computed from the ldd listing, not built), for a listener on a small board that keeps its model files; the desktop packages ship Qt anyway.
- *Cost:* The strip is Trivial, one line. The headless build is Small: a packaging target and a CI configuration that builds and checks it, plus a CMake option if one tree must build both binaries.
- *Risk / constraints:* With no --model and an empty cache, the headless listener's first picture must fail with a message naming --model, under a test. The development tree's RUNPATH points into native/build/_deps, so check sizes on the packaged binary. On air: no change.

**S6. Search retro's frequency bins on several cores, prepare the next slot ahead, and run EM's two split tracks side by side (results unchanged)**
- *What:* Three pieces of independent work that the C++ runs one at a time.
  - The template search loops over its frequency bins in turn (native/core/qrss/em_search.cpp:323-341), each depending only on its own inputs. Search them on a pool and pick the winner in bin order with today's strict comparison (:340), so a tie goes to the same bin.
  - Retro detection walks the store's slots in order (em_search.cpp:876-918). Preparation does not read the accumulator, so the next slot can be prepared on another thread; the searches stay in order, because each reads the accumulator (:831) and a find can be associated into it (:851).
  - EM's block split runs two re-tracks, one per parity, from the same first track and prior into disjoint latents (native/core/qrss/em_refine.cpp:194-225), so they can run side by side.
- *Why we think so:*
  - Retro takes about 6 s per FULL slot, about 2.5 s of it preparation (measured), so about 20 min over a 48 h FULL store (O12).
  - The port estimated about 4 times on 4 cores for the search's 19 bins (inferred; no QRSS signal processing runs in parallel yet).
  - A FULL member's re-receive takes 4.22 s with the split against 3.24 s without (measured), about 1 s per split track.
- *Gain:* A 48 h FULL retro drops from about 20 min to 8 to 11 min on 4 cores, and each FULL member saves about 1 s of its 4.22 s per EM round (inferred). Both reach the app through em_api, so an operator waits less for either.
- *Cost:* Moderate: about 120 lines and the QRSS core's first thread pool, whose race check is the slow part (the receiver runs about 13 times slower under ThreadSanitizer; see Lessons).
- *Risk / constraints:* Results and log lines stay identical and in slot order, since no search runs against an accumulator a later find could change. ThreadSanitizer must confirm the bin threads share the pocketfft plan read-only. Look-ahead holds one prepared slot, about 140 MB at FULL (raw capture, normalised front-end output, keep mask; em_search.hpp:348-352, receiver.hpp:89-95; computed), and the lazily set SlotInput::prep (em_search.cpp:818-823) must be filled before the search thread takes it. EM's members stay sequential, since each update reloads the accumulator. On air: no change.

**S7. Thread the convolution's batches and a slot's independent units (bit-identical)**
- *What:* Three kinds of the single-threaded receiver's work are independent and merged in a fixed order: the overlap-add convolution's batches (native/core/math/convolve.cpp:258-276), each detection's passes within a slot, and gate V's eight timing tries for a candidate without timing (track_timing.cpp:981), which stop at the first accepted one (:999, :1014). Run them on S6's pool and merge in today's order, running the timing tries speculatively and keeping the first accepted one.
- *Why we think so:* A FULL CE receive_slot spends 1.31 s finding candidates, 1.78 s gating and 5.38 s on passes, and a FULL single-signal receive with a picture takes 12.06 s for CE and 14.99 s for L (measured).
- *Gain:* A FULL single-signal receive drops by at most about 1.8 s, from 12.06 s to roughly 10.3 to 11.5 s on 4 cores (computed upper bound, inferred range). A slot with several signals gains more (inferred).
- *Cost:* Moderate: one change per unit, each with an equality test, plus ThreadSanitizer time.
- *Risk / constraints:* No arithmetic changes, so results stay bit-identical; speculative timing tries waste CPU when an early one accepts. When the finish's 180 s wall-clock budget binds (native/core/qrss/rx_config.hpp:272; steady clock, native/core/qrss/receiver_slot.cpp:180-182), a faster receiver reaches more of its 12 candidates, as the C++ already does against the Python. Cap the pool so it cannot starve the app's GUI and audio threads. On air: no change.

**S8. Read pass files once, and check their CRC faster (low)**
- *What:* Three small costs around the store's .npz files.
  - Each EM round, the progress count loads every member's whole pass only to learn whether it has a channel and which waveform it uses (native/core/qrss/em_refine.cpp:251-259, :299-301). Read those from the file's metadata instead.
  - The CRC-32 check (native/core/qrss/npz.cpp:1097) uses slicing-by-8. A folded CRC with the CPU's carry-less multiply gives the same values several times faster.
  - receive::run's .npz load in native/apps/sstvae_qrss/receive.cpp holds the whole file while copying its front end out, about 123 MB at FULL (computed, wave 6b). K6 removes the same copy.
- *Why we think so:* known_keying, which loads passes, takes 31.7 ms in the C++ against 23.7 ms in the Python. Loading a 3.6 MB FULL pass takes about 5 ms, and the CRC over 2.5 MB takes 1.25 ms (measured).
- *Gain:* Milliseconds per call (inferred): about 5 ms per FULL member per EM round, and about 1 ms per pass load. Holding the .npz once saves about 60 MB of transient memory on simulator input (inferred).
- *Cost:* Small: about 20 to 60 lines each.
- *Risk / constraints:* The CRC values are the same by definition, and the EM tests already compare the progress total. On air: no change.

**Checked and left alone**
- Start-up: already 0.01 s and 20 MB (Measured).
- The Python's 131 s round B over a half-heard slot is not a C++ problem: the whole C++ FULL replay takes 68.79 s (measured).
- Packing a passband hour to FLAC stays inside its 2 s budget (Lessons).


## The earlier ideas, after the port

The ideas document of 2026-10-12 listed 66 ideas from building waveform L. The port mirrored the reference rather than changing it, so most are unchanged in substance. Five points frame the entries, which cite rather than repeat them.

- **Every receiver idea now lands twice,** in the reference and the C++, under the convention in How to read the entries.
- **Most receiver work got 2.5 to 9 times cheaper per module, but live acquisition did not.** receive_slot on FULL takes 9.1 s in the C++ against 32.4 s in the reference for CE, and 16.3 s against 51.6 s joint (measured, wave 6b; whole commands: Measured). A joint FULL live acquire takes 1.32 to 1.73 s against 1.43 to 1.81 s (measured, wave 6a): A2 dominates it, and numpy already ran A2's heaviest sum through BLAS.
- **The C++ listener is what the app runs,** started with only --state added (native/gui/qrss_window.cpp:526-535, 630), so it hears both waveforms by default; the app also transmits in process through qrss::api::PassRequest.
- **A false FULL candidate's gate V costs 7.0 to 8.3 s in the C++ against 50 to 63 s in the reference (measured, wave 7).**
- **Anything the app runs over em_api beside its listener needs B5's cross-process lock first.**

Two facts outside the port changed too: L went on the air on 2026-10-12, in mode A on 14.066 MHz, and everything is experimental, so no older listener needs to stay compatible. Entries keep the 2026-10-12 document's idea and reasoning and say what changed (What changed, Gain now, Cost now, Risk / constraints); each Risk ends with On air. A bare file name is in sstvae/qrss/ or native/core/qrss/, except the top-level qrss_*.py scripts.

Of the 66 ideas, 3 are done (one only in part), 3 are moot, 28 changed in cost, gain or premise, and 32 are unchanged.

### Changed by the port

Ranked by value now; ideas the port moved alike share an entry.

**F1. Send a lead-in by default for L, and have the app pass it (both waveforms)**
- *What changed:* The app fills a qrss::api::PassRequest in process and never sets lead_in_s (native/gui/qrss_tx.cpp:251-265); the 0.0 s default sits in qrss_transmit.py:53, native/apps/sstvae_qrss/transmit.cpp:89 and api.hpp:141.
- *Gain now:* Measured with a steady tone: about +2 dB of live A1 depth alone, 5 to 6 dB with L1 (not built). Stored passes gain about 0 dB alone, 1.5 to 2 dB with L1 (inferred).
- *Cost now:* One line setting r.lead_in_s, plus the setting, its tests/test_native_settings.py fixture entry and the dialog round trip; the transmitter parity tests catch the defaults drifting apart (both inferred).
- *Risk / constraints:* track_l handles a lead-in whether or not the header claims it; the key-up drift caveat and O20's drive rule stand. On air: up to 10 s more key-down per L pass, 0.56% of a FULL frame (computed); the format is unchanged.

**R10. Re-receive stored passes with their header once the store supplies it (CE now, L after L5)**
- *What changed:* em_api::refine runs full leave-one-out EM, re-tracking with the known header by default (RefineOptions::use_header, em_api.hpp:165-170) and always applying the data prior (em_refine.cpp:315); round B's re-track, em::rereceive (em_refine.hpp:152), has no app-facing entry. Only decode --em N reaches them; no listener or GUI code calls EM (live.py:85-91).
- *Gain now:* +0.8 dB of latent SNR for CE at -12 dB and 0.1 to 0.5 dB for L at -24 to -28 dB (measured; CE at depth unmeasured). A button over em_api::refine gives EM's gain, not R10's. L waits on L5 (em.py:283-288; em_refine.cpp:119, 310-313).
- *Cost now:* A FULL pass refines in 3 to 5 s (em_api.hpp:54-55); a FULL rereceive with its path takes 3.2 to 4.2 s against 17 to 23 s (measured, wave 10; em_refine.hpp:58-68). An app button or idle task needs B5 or an in-process listener (listen_api).
- *Risk / constraints:* Stored CE z and w change, so it needs a flag or Jeff's OK. On air: no change.

**D5. Fix stale and contradictory statements in the QRSS docs (docs)**
- *What changed:* docs/qrss/README.md says the app runs the qrss_*.py scripts from the .venv and that L "may go on the air once the listeners meant to hear it run this code"; no QRSS doc mentions sstvae-qrss. CE's column and the Status section still read "Simulation only", and docs/qrss/design.md still names spec rev 9 and a 90 s test limit.
- *Gain now:* A correct record for operators; no dB.
- *Cost now:* Small text edits (inferred).
- *Risk / constraints:* None. On air: no change.

**O12. Search back for a picture's missed earlier segments once a later segment's header decodes (CE now, L after L2)**
- *What changed:* CE's half exists: em_api::retro searches the passband store for the known waveform (em_api.hpp:59-64) and decode --retro runs it by hand, but nothing queues it, and store.due looks only forward.
- *Gain now:* A missed CE group-0 segment becomes recoverable to about -35 to -40 dB for senders who keep one frequency, as the app's scheduler does (inferred). L waits on L2 (em_search.cpp:760, 925; em.py:232-234).
- *Cost now:* About 0.2 s per TINY slot, 0.9 s per SHORT and 6 s per FULL (measured, em_api.hpp:59-64), so about 3 min at SHORT and 20 min at FULL over a 48 h store of 192 slots (computed). Left to build: an idle queue and a CPU cap in both listeners, or an app task after B5.
- *Risk / constraints:* Gate deep search's false accepts, for example by correlation against the accumulator; CE needs a flag, since it adds stored passes. On air: no change.

**O15. Compute A2's p-value from one gammaincc per scale (fast path, bit-identical)**
- *What changed:* An A2 line takes 43 ms in the C++ against 54 ms in the reference for CE, 41 ms against 59 ms for L (measured, wave 5); the C++ preamble_p, which still evaluates every cell (acquire_a12.cpp:387, acquire_stats.cpp:427), is 26 ms of a CE line (measured, wave 5).
- *Gain now:* About 25 ms per line in the C++ (inferred), 20 to 45 ms in the reference; over a joint FULL acquisition's 31 A2 searches (measured, wave 6a), up to about 0.8 s of a 1.3 to 1.7 s live acquire (computed upper bound: the breakdown over-sums the line). R3, L1 and L7 get cheaper too.
- *Cost now:* Very small, in acquire.py and acquire_a12.cpp, each with an equality test against the full map (inferred); no pins move.
- *Risk / constraints:* Both must match when every cell is clamped at p = 1 and on equal p at different Lambda. On air: no change.

**O3. Stop each live refresh at the heard horizon instead of tracking the whole frame (both waveforms, behind a flag)**
- *What changed:* The C++ mirrors the whole-frame refresh (listener.cpp:1733; live.py:1320) on a nice +10 worker thread outside the listener lock (listener.hpp:37-49), so feeding stays prompt (feed p95 0.34 ms against 2.34 ms; Measured). capture_prepare stops the blanker and AGC at the heard edge (af79a3d); the tracker and gate still run to the frame's end.
- *Gain now:* About half the live CPU per signal over a FULL slot: the doc's 330 s per CE FULL signal is roughly 95 s in the C++ (inferred from receive_slot's 3.5 times). First tiles come about 10 times sooner (inferred).
- *Cost now:* A horizon argument through capture_prepare, track and ltrack (inferred).
- *Risk / constraints:* Live tiles at partial progress shift; stored passes do not. On air: no change.

**R9 and O11. Keep failed hints out of the finish's gate queue, and size the finish to the time left (both waveforms)**
- *What changed:* The finish verifies at most 12 candidates in 180 s (live.py:109-110; rx_config.hpp:271-272). At 7.0 to 8.3 s a gate (measured, wave 7), twelve take 84 to 100 s in the C++, so the cap binds first (computed). Hints count against it, ahead of A3 hits (receiver.py:583-585, 675).
- *Gain now:* Today a failed hint costs one of the 12 places rather than a minute of budget; R9 frees that place for a weak A3 hit, such as one from a moving sender (inferred). O11's part (a), which sets the finish budget from the time left before the next finish and scales the cap with it, becomes mostly "raise the cap", with 80 to 96 s of slack per finish in the C++ (computed).
- *Cost now:* R9 is a few lines each (inferred). At a cap of 24 the reference still stops gating after 180 s (receiver.py:619, 671), reaching 3 or 4 false FULL candidates to the C++'s about 22 to 24 (computed from 50 to 63 s and 7.0 to 8.3 s a gate).
- *Risk / constraints:* A real hinted pass whose A2 narrowly fails loses its gate; both change CE output on busy bands, so they need a flag. On air: no change.

**R7. Raise the header decoder's list size from 8 to 32 (spec owner)**
- *What changed:* Decoding takes about 1.5 ms per attempt against 12 ms (measured, wave 1), so list 32's 1.4 times adds well under a millisecond (computed). "Do O1 first" now matters mainly for the reference, which associates under its listener lock (live.py:1401-1434; the C++ already associates outside the listener lock: O1).
- *Gain now:* About +0.3 dB of header threshold on steady paths (measured on synthetic LLRs); on fading paths, tracking still limits the header.
- *Cost now:* One default in header.py:204 and header.hpp:128.
- *Risk / constraints:* About 4 times the false accepts per attempt (2026-10-12 document), and the spec states list 8. On air: no change.

**O1. Associate incrementally, cache accumulators, and do it outside the listener lock (CE results unchanged)**
- *What changed:* Part (c) is done in the C++ only, which associates and identifies provisional tiles outside its listener mutex, under the store's lock (listener.cpp:1936 from f9c0b6f, 1991; listener.hpp:45-50); the reference holds self.lock (live.py:1401-1402, 1431-1434). Parts (a) and (b) are done in neither: resolve_provisional tests every cluster against every accumulator (associate.py:382, associate.cpp:336). Associating one pass on a 50-pass FULL store takes 38.8 ms against 78.1 ms, but reading an accumulator 9.5 ms against 4.5 ms (measured, wave 8), so the quadratic term did not shrink (inferred).
- *Gain now:* The app's live tiles no longer freeze during association, but the quadratic cost holds the store lock that refreshes and em_api calls wait on (inferred from listener.hpp:45-50); the reference's 14 to 16 s per decoded header at 30 accumulators stands (measured).
- *Cost now:* (a) and (b) in both implementations, (c) in the reference.
- *Risk / constraints:* A cache-invalidation bug could correlate against stale sums; an equality test against the old resolution guards it. On air: no change.

**O9, O10, O18 and O17. Stop repeating gates and tracks the listener has already run (both waveforms)**
- *What changed:* What they skip is 3 to 9 times cheaper: verify_track on FULL at -12 dB takes 1.85 s against 16.88 s, and gating in a FULL CE receive_slot 1.78 s against 5.81 s (measured, waves 5 and 6b). The paths are mirrored unchanged (live.py:1343-1351, listener.cpp:1777; live.py:355-358; receiver.py:709).
- *Gain now:* Inferred by scaling the doc's figures [in brackets]. O9: about 4 s of CPU per known signal per slot [18 to 26 s]. O10: about 2 s of finish budget per known signal [11 to 13 s], or a candidate place while the cap binds. O18: about 8 to 9 s per signal at the frame's end [30 s], through a callback, since the C++ finish is a thread (listener.hpp:37-41). O17: a few tenths of a second per CE accept on FULL [about 1 s].
- *Cost now:* Each small, in both (inferred).
- *Risk / constraints:* O9 and O10 can move stored CE results slightly, so they need flags. On air: no change.

**O13. Requeue a finish that threw, once, from the passband store (both listeners)**
- *What changed:* A live receive that throws is now retried and logged once, in both (live.py:1158-1171, from 098f743; listener.hpp:73-77), but a finish that throws still retires its slot (live.py:1161-1163). The C++ finish is a thread (listener.hpp:37-41), so the doc's example, an out-of-memory kill, takes the whole listener.
- *Gain now:* A slot's passes survive an exception in its finish, such as an unreadable passband stretch (inferred; how often is unmeasured).
- *Cost now:* Small, in both listeners (inferred).
- *Risk / constraints:* A deterministic exception repeats once, bounded by the retry count. On air: no change.

**L3 and L11. An L-matched A3 emission, and a looser A3 threshold for L-shaped hits (L only)**
- *What changed:* A false L FULL candidate is now gated in 7.0 to 7.2 s in the C++ (measured, wave 7), against the doc's 60 to 80 s. CE FULL acquisition with A3 takes 1.15 to 1.33 s against 2.88 to 3.14 s (measured, wave 6a) and track_before_detect alone runs 3 to 6 times faster (measured, wave 5), so doubling A3's work costs under 1 s a slot in the C++ (inferred). Their prerequisite, O2's own-band rule, landed narrower: a blind-found CE's sidebands are still gated (receiver.py:504-529; inferred).
- *Gain now:* L3 about 2 to 2.5 dB of blind L reach on white noise; L11 about 0.5 to 1 dB, to be measured first (both inferred).
- *Cost now:* L3 is a new emission and Gumbel table, L11 a few lines, each in both (inferred); their false-alarm calibration runs 2.0 to 2.4 times faster on the C++ per A3 capture (measured, wave 7).
- *Risk / constraints:* Each false candidate still takes one of the finish's 12 places, and D12 comes first. On air: no change.

**D2. Measure gate V's noise tail at Z_ACCEPT for each waveform, blind and timed (offline)**
- *What changed:* At 7.0 to 8.3 s a gate in the C++ (measured, wave 7), the 1,000 FULL gates the doc priced at about 20 CPU-hours take 1.9 to 2.3 CPU-hours (computed).
- *Gain now:* About 1.5 dB of CE blind depth if the tail allows a threshold of 5; L may need a higher one (inferred).
- *Cost now:* Z_ACCEPT is shared and the parity tests hold the C++ gate to the reference, so one C++ calibration serves both (inferred).
- *Risk / constraints:* White noise flatters the tail, so include the real false population. On air: no change.

**O16. A data-band matched filter by convolution and interpolation (L first, CE behind a flag)**
- *What changed:* The gathers it would replace are 3.5 to 5.2 times faster in the C++: ce.matched_filter 46.5 ms against 160.9 ms, L's data-band filter 45.8 ms against 236.1 ms (measured, wave 4).
- *Gain now:* The doc's 1.3 s of a 5.5 s L SHORT pass becomes roughly 0.3 s (inferred).
- *Cost now:* About 40 lines for L and more for CE, in both; not bit-identical (inferred).
- *Risk / constraints:* Interpolation error across the 19 Hz data band must stay below about -45 dB; build it only if a C++ profile puts the gather on top. On air: no change.

**L2. An L known-waveform template search and retro detection (L only)**
- *What changed:* The CE search it would mirror runs in the C++ behind em_api::retro (template_search 6.3 s against 16.6 s on FULL; measured, wave 10), and both still refuse L (em.py:232-234, called at em.py:571 and 728; em_search.cpp:760, 925).
- *Gain now:* The per-pass floor moves from about -31 dB to about -36 to -37 dB on quiet paths, once an accumulator exists (inferred).
- *Cost now:* Still weeks, in both (inferred).
- *Risk / constraints:* False accepts at depth; it needs some passes found blind first. On air: no change.

**L5. EM for L: port rereceive as a header-known re-track first (L only)**
- *What changed:* The C++ copies the CE-only skip (em_refine.cpp:119, 310-313; em.py:283-288); once both re-receive L members, decode --em and em_api::refine carry them with no new API (inferred).
- *Gain now:* 0.1 to 0.5 dB per header-less L pass at -24 to -28 dB (measured, slope-corrected); the data prior at most +0.38 dB where measured.
- *Cost now:* The dispatch, in both; its unit of work, round A of track_l on L FULL, takes 3.54 s against 8.92 s (measured, wave 6a).
- *Risk / constraints:* CE results move in mixed CE and L accumulators. On air: no change.

**D10 and D12. L multi-pass end to end, and L's gate V at -31 to -34 dB (offline, L only)**
- *What changed:* Only the cost.
- *Gain now:* Unchanged; both decide other items.
- *Cost now:* D10 (the L ranking) takes about 4 to 5 min of CPU on the C++ (computed: 16 passes at receive_slot's 16.3 s joint FULL, wave 6b), against the doc's "under an hour"; D12 (which bounds L2, L3 and L11) is a few FULL tracks per point, seconds each (inferred).
- *Risk / constraints:* None new. On air: no change.

**F3, F5 and F6. L's format changes: a smaller spread-copy rho, a retuned clipper, and the never-sent latents (L format)**
- *What changed:* No version bump is needed, since nothing older must stay compatible, but L passes stored before the change would combine by latent index with new ones and could be mis-weighted or mis-scaled (the old F3 and F5 risks), so clear the L accumulators when it lands (inferred). Each now moves the bit-exact C++ transmitter, its parity tests, the generated config.hpp and both receivers. F3's D_PASS_LX = D_PASS_L x (1 + SPREAD_RHO) (ldemod.py:132) is generated into rx_config.hpp:179, so one fix clears both. F5's three-element L_CLIP_OVERSHOOT (constants.py:87; config.hpp:72) is also hard-coded as three in the pybind default (native/bindings/module/qrss/lwave.cpp:338-339), though lwave.hpp:214 takes a span.
- *Gain now:* Small. F3: +0.11 dB per L latent at half the rho (computed). F5: about +0.1 to +0.3 dB for a PEP-limited station, -0.1 to -0.4 dB for a heat-limited one (inferred). F6: about +0.1 dB PSNR at the single-pass good level (inferred).
- *Cost now:* As before, in both; each can land alone.
- *Risk / constraints:* Each needs the spec owner, and F3 waits on D1. On air: each changes L's format, with no version bump.

**F7. CE format v3 candidate: thin the data references to 1 in 48 and send the never-sent latents (lowest priority)**
- *What changed:* The work grew: both transmitters, both receivers, the beacon-file writer in both (beaconfile.py, beaconfile.hpp) and the C beacon (qrss_beacon_c/qrss_ce.c) change with FORMAT_VERSION (constants.py:43).
- *Gain now:* About +0.1 to +0.15 dB PSNR at the good level, 0 dB at usable (inferred).
- *Cost now:* Larger, as listed (inferred).
- *Risk / constraints:* Each pass's kappa becomes about 1.7 times noisier (computed). On air: changes CE's FORMAT_VERSION.

**C1. Offer the D3 residual-ratio re-check under --just-ce (CE-only path)**
- *What changed:* The premise: the reason the doc gave for holding it back no longer applies. The check runs in joint mode, the app's default (receiver.py:707-712), not on the CE-only path (receiver.py:630), where with L on the air a station sees junk CE passes.
- *Gain now:* Joint mode refuses every junk CE pass from an L signal (measured); the false-reject rate on weak fading CE is unmeasured.
- *Cost now:* About 10 lines and a switch in both receivers (inferred); free with O17.
- *Risk / constraints:* Jeff could make it the --just-ce default instead of a flag. On air: no change.

### Done or moot

- **R1, done** in c131551 as decision D43, narrower than proposed: noise_psd takes noise only from bins within 0.1 dB of the passband (frontend.py:64, 340-352), with a response-corrected skirt fallback (frontend.py:425-432); QRSS_OLD_EDGE_NOISE (rules.py:36) turns it off, in the C++ too (frontend.cpp:667, rules.cpp:48-50).
- **O2, done** in c131551 as D44, also narrower: an in-band whole-slot candidate is skipped only when its owner was found on its preamble and the candidate is no stronger in A3 than the owner's line (receiver.py:40-52, 504-529); QRSS_OLD_GATING (rules.py:37) turns it off in both.
- **D6, part 1 done** in a12c3b8: the refresh log line prints once per refresh (live.py:1361-1374; listener.cpp:1844). Part 2 is open: a missing L preset still falls back to 1.0 with no test (ltrack.py:869; ltrack.cpp:1410; also B13). Part 3 was already covered: tests/test_qrss_lacquire.py:119 holds the two preambles to the equal duration live_end_s needs (acquire.py:1545-1548).
- **O8, moot:** the whole receiver, Kalman passes included, is C++ in the app's listener.
- **F2, moot:** its batch existed to use the window before L reached the air.
- **O7, moot:** the app's listener runs the joint receiver by default, and C1 covers --just-ce.
- **"One decision before all of this," moot** with F2 (it is not one of the 66).

### Unchanged

Each line gives the doc's estimate and size.

- **R2.** Report frequency from a line through the path: error from 0.23 to 1.09 Hz down to 0.03 Hz (measured in simulation); small. receiver.py:322 still reports tr.freq(0.0).
- **R4.** Neighbour exclusion by occupied band: 1 to 3 dB of noise bias in a band packed at 30 Hz (inferred); medium.
- **R3.** Station frequencies as A2-only hints: about +5 dB live, 1.5 to 2 dB stored, for repeat L stations (inferred); small.
- **R6.** Consistency-weighted header LLRs: under 0.1 dB (inferred); small to medium.
- **R5.** An AGC blind to a strong L signal: about 0 dB at operating thresholds (inferred from measured cross-modulation); medium.
- **R8.** Cancel the transmitter's own distortion (parked): about 0.1 dB at the good level (inferred); medium.
- **L1.** An L-only A1 window over the lead-in: A1 about 5 dB deeper (measured, steady tone); 0.5 to 1.3 s a slot; needs F1.
- **L9.** Sweep refine_path_l's fine-pass thresholds: unmeasured, bounded by 0.2 to 1.9 dB of wander loss (spec table); small to medium.
- **L4.** Re-normalise L gain scales: 0.05 to 0.2 dB (computed); small.
- **L6.** An L-shaped A1 and later L-only runs: (a) +2 to 3 dB live (inferred); medium.
- **L7.** A longer A1 list for L: about +1 dB live at -29 dB (measured); about 1.4 s a refresh, 0.5 s with O15.
- **L8.** Honest L weights on fading: 0.9 to 1.3 dB single-pass SINR at -12 dB (inferred), about 0 dB deep; medium.
- **L10.** L calibration tidy-up: each well under 0.1 dB (inferred); small.
- **C2.** TRK's refine_timing fixes for CE: 0.46 dB mean on SHORT at -27 dB (measured), 0.1 to 0.4 dB on FULL (inferred); small to medium.
- **C3.** A second A2 try for CE hints: under 1 dB in a niche (inferred); a few lines.
- **C4.** Honest CE header LLRs: at most 0.03 dB (spec); small.
- **F4.** A stronger pilot in L's preamble: (b) about +0.66 dB live A1 without a lead-in (computed); not recommended over F1 and L1.
- **F8.** A QRSSTVAE-trained codec: +0.05 to 0.1 dB PSNR at one pass (computed); large.
- **O4.** Transmit levels per waveform: up to 3.2 dB average power for heat-limited stations (computed); small. The in-process transmitter still uses one level (native/gui/qrss_tx.cpp:495).
- **O20.** A drive rule or Tune L button: unquantified. The L schedule card says to avoid the ALC (native/gui/qrss_schedule_window.cpp:229), with no 47%-of-PEP rule.
- **O5.** Choose a tile's waveform among the finish's passes: correctness; small.
- **O6.** Key slots by (slot, waveform): correctness; small.
- **O19.** Carriers 100 to 120 Hz inside the band: with R1 landed, only the 1 to 2 dB CE edge loss is left (inferred); trivial. docs/qrss/60m.md:18 still suggests 300 to 1900 Hz, and the app accepts 300 to 2700 Hz (api.hpp:54-55).
- **O14.** Recover the full 48 h at idle: no passes lost to an outage (inferred); small. sstvae-qrss listen still defaults to 3 h (native/apps/sstvae_qrss/listen.cpp:204, as qrss_listen.py:94).
- **O21.** Weak live tiles matched against the store: the combined picture 15 to 25 min sooner (inferred); small to moderate.
- **O22.** Warn when our send would blank a due segment: no segment lost to our own send (inferred); small.
- **D1.** L's header thresholds: decides F3; minutes per SNR point.
- **D11.** Replay the real recordings as acceptance: catches simulation-only wins; small.
- **D3.** The L ghost rule and GHOST_HZ: prevents ghost L passes (inferred); a few simulations. GHOST_HZ is still 20 Hz (receiver.py:120).
- **D7.** Pin L's acquisition rates: a regression guard; small. Only the slow -24 dB test exists (tests/test_qrss_lacquire.py:660).
- **D8.** Pin L's 8 kHz peak-to-average ratio: guards the drive advice; one test. Today's checks only the loopback rate (tests/test_qrss_lwave.py:146-149).
- **D9.** State in the spec that the signal is USB: a wrong-sideband listener decodes (inferred); one sentence.


## Code health: deviations and simplifications

The reviews recorded 101 places where the C++ knowingly differs from the reference and 34 simplifications. Checked against 4e92d36, most are refusal texts or rounding on inputs no caller produces and are best left alone; the nine below are worth acting on, ranked by expected value. Each title's qualifier names the move: align the reference to the C++, delete the difference, or simplify. The one align-the-C++ case, sstvae-qrss receive's store and passband, landed in 5e1ae13 (reviewed in 3787d20).

**K1. Fill unheard audio from one specified generator in both (delete the difference)**
- *What:* Every live refresh fills unheard audio with complex Gaussian noise: the reference from np.random.default_rng(q) (sstvae/qrss/live.py:308-313), the C++ from xoshiro256** seeded by splitmix64, with Box-Muller (native/core/qrss/live.cpp:568-603, live.hpp:268-272). The distribution is the same; the draws are not. Specify one counter-based generator that numpy can vectorise (splitmix64 of seed and sample index, then Box-Muller) and use it in both.
- *Why we think so:* the parity rows hand the C++ numpy's draws (tests/qrss_native/_live.py:24-36), so no whole-pipeline listener comparison runs the generator the app ships; QRSS_LIVE_NOISE=cpp runs it, but then tests/test_qrss_live.py:674-677 compares only pass counts. Wave 8 checked the C++ generator by distribution tests only.
- *Gain:* the listener the app runs becomes the tested configuration: live tiles and partly heard slots agree with the reference to rounding, not just in distribution (inferred; numpy's log and cos can differ from libm by an ulp), and QRSS_LIVE_NOISE goes away. A fully heard slot has no fill.
- *Cost:* Small: about 20 lines in live.py and a 30-line default_noise (inferred).
- *Risk / constraints:* porting numpy's PCG64 and ziggurat instead would spare the reference, but numpy does not promise Generator streams across versions. On air: no change.

**K2. Make q one checked type where it enters, and delete the deep checks (simplify)**
- *What:* q is std::int64_t in the records and std::uint64_t in the scrambler, so each crossing re-checks it ("q must fit in uint64"): native/core/qrss/api.cpp:161, em.cpp:128-130, em_refine.cpp:42, receiver.cpp:319 and receive::run in native/apps/sstvae_qrss/receive.cpp, beside entry refusals in slot.cpp:424 and simfile.cpp:288. Give the C++ one slot type whose constructor refuses q below 0, use it wherever q enters (api.hpp's PassRequest, rx_api.hpp's capture_from_audio and capture_from_wav, PassResult and simfile loads), and delete the deep checks.
- *Why we think so:* the one real bug of this class printed a pass for a slot before 1970: the native demodulators cast q to uint64 and wrapped it (wave 6b review, fixed). The reference refuses at entry (sstvae/qrss/sequences.py:185-190, sstvae/qrss/channel.py:947-948); only the C++'s in-process APIs take a bare number, and api::slot_of still returns a negative q (wave 7).
- *Gain:* five duplicated checks deleted, and the class closed by the type for every new caller, the app's included (inferred).
- *Cost:* Small to Moderate: a 30-line type and edits at about 20 call sites (inferred).
- *Risk / constraints:* C++ only. The in-process transmit API's q refusal moves from last to first (api.hpp:153), which nothing in the app depends on (inferred); the CLIs refuse at parse, so their texts do not move. On air: no change.

**K3. Fold the reference's duplicated code the way the C++ did (align the reference)**
- *What:* In five places the reference does one thing twice and the C++ does it once, with bit-equal results:
  - _receive_pass_l (sstvae/qrss/receiver.py:331-380) repeats receive_pass (:267-329) with five functions swapped for their L forms; the C++ has one receive_pass that dispatches on the waveform (native/core/qrss/receiver.cpp).
  - demod.calibrate's inverse-variance combination (sstvae/qrss/demod.py:179-191) repeats ldemod._combine (ldemod.py:210-222); the C++ has demod::combine (native/core/qrss/demod.hpp:172).
  - track._parabola (track.py:707-711) is acquire._parabola (acquire.py:311-316); the C++ has one (acquire_stats.hpp:127-128).
  - em.rereceive builds the pass's channel (em.py:296) and receive_pass builds it again (:300); the C++ hands it over through the channel_for seam (em_refine.cpp:140-146).
  - retro_detect prepares each slot's capture twice, for template_search and receive_template (em.py:745 and :752, both through _search_input at :531); the C++ once (em_search.cpp:817).
- *Why we think so:* the C++ forms pass the swap and whole-caller tests with identical numbers (waves 4, 6b and 10).
- *Gain:* about 70 lines of Python gone, and one channel mix and noise floor per re-received pass and one blank-and-normalise per retro slot saved (computed, not timed).
- *Cost:* Small: about 60 lines changed, mostly a per-waveform dispatch in receiver.py (inferred).
- *Risk / constraints:* Python only, and outputs stay bitwise, but rx_pins.json hashes receiver.py, demod.py, ldemod.py, track.py and acquire.py (not em.py), so it regenerates although nothing moved; em.py's ctest pins stand. Tests that call _receive_pass_l change (tests/test_qrss_lreceiver.py:140, tests/test_qrss_native_receiver.py:1129). On air: no change.

**K4. Map the C++ exceptions to Python's in one table (simplify)**
- *What:* The parity module registers six exception translators of its own: native/bindings/module/qrss/rx_types.hpp:98, frame.cpp:340, demod_rx.hpp:225-229 (a second copy of frame.cpp's), infra.cpp:132 (a mapping rx_types.hpp also makes), tx.cpp:282 and acquire_tbd.cpp:74. Nine binding files also raise OSError themselves (fspath.hpp:89-95 holds a helper). The listener binding registers only the shared rx translator (listener.cpp:728), which has no case for std::system_error, so the listener's file errors reach Python as RuntimeError, not OSError (wave 9b review, open). Register one translator in native/bindings/module/module.cpp from one table (core exception, Python class, errno handling) and delete the rest.
- *Why we think so:* counted with git grep at 4e92d36.
- *Gain:* the listener's file errors read as OSError with errno and filename, and a new core exception has one place to be mapped instead of a seventh file (inferred).
- *Cost:* Small: about 80 lines added and 120 removed (inferred).
- *Risk / constraints:* bindings only, no pins; tests that assert an exception class must still pass under --native-qrss. On air: no change.

**K5. Delete the private pairwise sums in the transmitter (simplify)**
- *What:* native/core/qrss/npsum.hpp:49-71 is numpy's pairwise_sum, included by 23 files, but polar.cpp:42-61, precoder.cpp:31-50 and filters.cpp:71-90 keep their own copies, which already disagree: polar and filters start a short run from +0.0, precoder and npsum from -0.0. Call npsum from all three.
- *Why we think so:* npsum.hpp landed in 5a91fe8 (wave 1), 33 to 75 minutes after these three files, and the open item to switch them was never done.
- *Gain:* about 60 lines out of the bit-exact transmitter and one definition to keep in step with numpy. No output changes: every caller adds the result to a +0.0 identity (polar.cpp:63-66, precoder.cpp:144-146, filters.cpp:92), so an all-zero run's sign cannot reach the output (computed).
- *Cost:* Trivial: three deletions and three includes.
- *Risk / constraints:* C++ only; the transmitter's golden and parity tests confirm bit-exactness. On air: no change.

**K6. Read every .npz through one reader (simplify)**
- *What:* simfile.cpp walks the zip itself (native/core/qrss/simfile.cpp:20-115) and re-frames a 0-d '<U' member as '<u4' to read the frame name (:158-251), only because npz::parse_npz cannot return a string member. The two readers also disagree on zip64: simfile refuses it (:49-50), npz.cpp reads and writes it (npz.cpp:818-819, :941-943). Teach npz.cpp the string member and let simfile call it.
- *Why we think so:* proposed in the wave 6b port of the receive CLI and its review; still true at 4e92d36.
- *Gain:* about 120 lines and one set of refusals gone (wave 6b estimate). Two savings could ride along: a member-selective read, so the store inflates only the latents and weights it needs, as np.load does (open since wave 8); and not holding the file buffer and a copy of its front-end samples at once (the transient copy S8 describes).
- *Cost:* Small: about 40 lines in npz.cpp; the rest is deletion (inferred).
- *Risk / constraints:* C++ only. The .npz refusal texts, byte-identical with the reference since wave 7 and pinned by the side-by-side receive tests, keep their wording. On air: no change.

**K7. Retire the two 2026-10-12 rule switches after one on-air comparison (delete the difference)**
- *What:* QRSS_OLD_EDGE_NOISE and QRSS_OLD_GATING restore the pre-2026-10-12 noise floor and gating (sstvae/qrss/rules.py). The reference keeps them in the environment so its spawned finish process inherits them (rules.py:24-29); the C++ takes them per call in ReceiveConfig (native/core/qrss/rx_api.hpp:110-116) but keeps the environment form for the CLIs, where getenv and setenv race across threads (rules.hpp:13-14). Once a band check shows the new rules hold, delete both switches and their flags in both implementations.
- *Why we think so:* the switches exist so that "a fix that misbehaves on the band can be turned off without a code change" (rules.py:5-8), and nothing under native/gui exposes them.
- *Gain:* about 750 lines deleted, mostly tests (computed from rules.py, the switch half of tests/test_qrss_rules.py, tests/test_qrss_native_rules.py, tests/qrss_native/rules.py, the C++ rules files, their binding and ctest, and about 80 lines in 24 other C++ files; the new rules' tests stay), plus the receiver_slot parity file's two old-rule scenarios, one process-global setting and one thread race.
- *Cost:* Small: deletion, after an A/B on a real recording with a signal near 300 Hz and one beside a strong CE signal.
- *Risk / constraints:* it removes the fallback, so it is Jeff's call. If they stay, the reference should take them per call as the C++ does, passing them to its finish process. On air: no change.

**K8. Accept a refusal difference instead of emulating Python's error on inputs no caller produces (simplify)**
- *What:* Make it the rule for the parity bindings: on input no real caller produces (a CE frame handed to an L function, a 2-D array where 1-D is meant, a float where an int is meant), the C++ refuses with its own type and text, and the parity test asserts only that both refuse. Delete emulation that exists only to match an exception class, module by module as each is next touched.
- *Why we think so:* about 30 of the 101 recorded deviations are of this class (counted). The ltrack binding alone raises Python's AttributeError, AssertionError, TypeError or IndexError text at 16 places (native/bindings/module/qrss/ltrack.cpp, git grep), and a wave 10 review left four more unfixed (prior_from on a 2-D S, int(q) before the broadcast refusal, template_z on a float n_chunks, known_keying on a lone surrogate). A release must match decoded pictures and headers, not exception classes (the parity bar; see the head).
- *Gain:* review and porting time on every later module, and part of the bindings' 20,759 physical lines, 16,197 of code (counted; the emulated share was not counted).
- *Cost:* Trivial to adopt; Small to Moderate to delete existing emulation, file by file (inferred).
- *Risk / constraints:* harness only. Keep exact emulation wherever a real caller reaches the case, and in the CLIs, whose texts an operator reads. On air: no change.

**K9. Drop stereo decorrelation from the passband FLAC encoder (simplify)**
- *What:* The C++ encoder analyses mid and side as well as I and Q in every block and costs the best assignment (encode_stream in native/core/qrss/flac.cpp; EncodeOptions::stereo_decorrelation, flac.hpp:85). Turn the default off; delete the path if a timing confirms the saving.
- *Why we think so:* the passband's I and Q are uncorrelated. Over 8 simulated 4 kHz hours, mid/side was chosen in 2 of 3516 frames, and stereo modes saved 0.01 to 0.05% of file size (measured, wave 9b).
- *Gain:* up to half of the per-block channel analysis (inferred, not timed), on an encode of 1.29 to 1.50 s of one core per fully heard passband hour, about 0.4 s for one heard 12% (wave 9b; the FLAC lesson); about 50 lines if deleted.
- *Cost:* Trivial for the default; Small to delete.
- *Risk / constraints:* C++ only, since the reference packs with soundfile. The files stay valid FLAC that soundfile and dr_flac read, and passband parity compares samples, not bytes. On air: no change.

### Accepted as they are

Eight differences that a station or a later porter will meet stay as they are. Paths are under native/core/qrss/ unless given in full.

- The listener has one mutex and a finish thread in place of the reference's lock discipline and child process, and its stop is bounded at 1.5 s, abandoning a receive in flight (a sink blocking on the stopping thread deadlocks; open by design, wave 9b): listener.hpp:40, listen_api.hpp:73-77 and :157.
- SIGTERM stops listen as Ctrl-C does, and a build without the codec runs as --no-pictures: native/apps/sstvae_qrss/listen.cpp:31, :44, :178-181.
- Pictures are written as PNG through stb; a JPEG input decodes differently from Pillow's, so its picture ID differs and transmit is bit-exact for PNG input only (Measured, Agreement): png.hpp:1-12.
- FLAC bytes differ from libFLAC's; samples, MD5 and cross reads agree: flac.hpp:4-24.
- Raw .i16 passband writes are synced before packing, not per write like the reference's msync: PassbandStore's durability note (passband.hpp).
- int64 where Python's integers are unbounded (hours, picture IDs, JSON ints), and decode --em caps rounds at 2^31: pyjson.hpp:19, em_api.hpp:161-164.
- CLI texts: decode --retro on a corrupt passband meta.json says ValueError where Python says JSONDecodeError (native/apps/sstvae_qrss/decode.cpp:60); usage lines never wrap (native/apps/sstvae_qrss/argparse.hpp:46-49); paths keep POSIX slashes on Windows (pypath.hpp:11); two refusal texts differ for a broken state directory, 37 of 41 probe cases byte-identical (wave 9b).
- Rule constants (GHOST_HZ, MERGE_HZ and the rest) compile in from the generated rx_config.hpp, checked by tools/gen_qrss_rx_config.py --check.

The other 13 are rounding differences held within the parity tolerances, refusals on inputs no caller produces, and two internal choices that change no output (two Store objects sharing one mutex, and TbdHit.f_hz refitting per call where the reference caches it); the review records list them with file references.


## Tests and the parity harness

The harness did what it was built for: at 4e92d36 the transmitter is bit-exact except L's float output, which stays within one float32 ulp (Measured), and the receiver's headers and pictures match the reference's within the parity bar, on the port's one machine. Ranked by expected value; none of these changes the air format.

**T1. Run the substituted suites in CI, and fail when the port is missing (trivial)**
- *What:* Beside the `pytest --native` step in `.github/workflows/native-build.yml` (:559-561), run the substituted suites (the reference's own suites with every transmit and receive row applied), `pytest -q --native-qrss=rx -m 'not slow' -k qrss`, with an `SSTVAE_REQUIRE_QRSS=1` that turns the "not built" skips (`tests/qrss_native/__init__.py:104`) into failures, as `SSTVAE_REQUIRE_CODEC` does for the codec. Give the ctest step (:340-341) `SSTVAE_REQUIRE_CODEC=1` and `QRSSTVAE_MODEL_DIR` set to the models the job already fetches.
- *Why we think so:* CI runs the parity files' fast tiers through `pytest --native` on four platforms, but no workflow passes `--native-qrss`, so the substituted suites, which check the rows, have never run in CI. CI itself runs only on pushes to master, on pull requests and by hand (`ci.yml:19-30`), and GitHub lists no run on waveform_l (checked 2026-10-15), so none of the port's commits has run there at all. The only check that a release decodes with no Python, `native/tests/qrss/test_em_chain.cpp`, prints `skip:` and passes when the models directory is unset (:200-207).
- *Gain:* No dB. The rows get checked on four platforms per push to master or pull request, for about 2.5 min of runner time each (inferred from CLAUDE.md:11-13).
- *Cost:* Trivial, about 15 lines of YAML and 10 of Python.
- *Risk / constraints:* The first run off x86-64 Linux will probably fail somewhere; T2 and T7 keep that triage short. On air: no change.

**T2. Pin the toolchain the parity bars were measured on (parity runs only)**
- *What:* Run the QRSS parity jobs on CPython 3.13 with numpy, scipy and onnxruntime pinned by a constraints file (onnxruntime at the C++'s version; Measured gives both), and have `_configure_native_qrss` (`tests/conftest.py:411`) refuse an unmeasured toolchain unless overridden.
- *Why we think so:* The bars were set on CPython 3.13.16, numpy 2.5.3 and scipy 1.18.1 (checked), already out of step with `uv.lock`'s numpy 2.5.1 and scipy 1.18.0, while CI installs CPython 3.12 (`ci.yml:65`) with unpinned numpy and scipy. Results already depend on it: `tests/test_qrss_native_slot.py:145` runs only on 3.13, and `-2j*np.pi`'s real part is +0.0 under 3.13 and -0.0 under 3.14 (`native/core/qrss/ltrack_timing.cpp:102-103`). A numpy kernel change would read exactly like a port bug.
- *Gain:* No dB; a class of false failures removed before T1 produces them (inferred).
- *Cost:* Small, about 50 lines.
- *Risk / constraints:* Each later bump becomes a deliberate re-measure. On air: no change.

**T3. One command for the parity files, with a whole-caller group for integration (test tooling)**
- *What:* A committed runner, for example `tools/run_qrss_parity.py`, that shards the 61 parity files into groups of about 150 s on 4 CPUs, names the node id a timeout stopped, fails when a group skips more than its recorded count, and passes `-m slow` for slow node ids (`pyproject.toml:155`'s default deselects them even by name). A `whole_caller` group, every whole-caller A/B with every row applied, runs at each merge.
- *Why we think so:* CLAUDE.md:44-45 leaves the grouping to whoever runs it, and one merge-verification group timed out (exit 124) and was rerun as two of 153.61 s and 201.73 s (wave 7). 4e92d36 is the case for the whole-caller group: stage 4's new `em.rereceive` and `em.pass_path` rows made `test_whole_caller[every row]` in `tests/test_qrss_native_receiver.py` miss the receiver's Python attributes, which was found only after the merge (fix at :1236-1240).
- *Gain:* Stale whole-caller tests found at the merge, and no 150 to 200 s reruns (inferred).
- *Cost:* Small, about 150 lines.
- *Risk / constraints:* Test-only. On air: no change.

**T4. Rebalance the tiers, starting with the two-hour slow tier (CI time)**
- *What:* Three changes.
  - Run the QRSS slow tier (about 120 tests and 2 h, CLAUDE.md:22-23) nightly, plain and under `--native-qrss=rx`, and correct CLAUDE.md:46, which gives `pytest -m slow` about 2 min. Today `ci.yml:154-156` and `native-build.yml:573-575` both run it per push, and `--native` substitutes no QRSS code into it.
  - Let the typed `receive_slot` binding return its `Acquisition`, so the calibration noise slots stop rerunning acquisition in Python, half of each slot's cost; the 60 SHORT slots already fill two node ids of 110 to 133 s each (wave 7).
  - Run the reference side of the CLI parity tests through one pre-imported server that forks per case and never imports `sstvae_native`: the reference starts in 1.00 s against 0.01 s (Measured), and the decode parity file's fast tier takes 136 s against a 40 s target (wave 10).
- *Why we think so:* The fast tiers stayed fast by moving tests to slow (receiver_slot's fast tier went from 104.00 s to 32.61 s, waves 6b and 7), and nobody ran the slow tier during the port (waves 4 and 5).
- *Gain:* About 4 h of runner time per push to master or pull-request update (computed from two runs of the 2 h tier), about half of the calibration file's 4 min (inferred), and most of the CLI files' start-up (inferred).
- *Cost:* Small for the workflow and docs (about 20 lines); Moderate for the binding (about 80) and the server (about 120).
- *Risk / constraints:* Nightly runs need a reader. On air: no change.

**T5. Stop reading the shared folder from the suite (trivial)**
- *What:* Commit the spec's cwid simulator, or its Morse table with a line naming its source, under `tests/data/`, point `_cwid_sim()` (`tests/test_qrss_format.py:413-419`) at it, and move the plan CLAUDE.md:25 names into the repository too (P1).
- *Why we think so:* `test_morse_table_matches_the_spec_sim` reads `/mnt/project-files/qrss-spec/sims/cwid/cwid.py` and skips when it is absent, so it has never run in CI. In the port's container that folder has returned I/O errors since about 03:00Z on 2026-10-14, so the test fails for that reason only, and every verification from wave 10 on ignored it: a red test everyone ignores is where a real failure hides.
- *Gain:* One real check that runs everywhere (inferred).
- *Cost:* Trivial, about 10 lines plus the copy.
- *Risk / constraints:* None. On air: no change.

**T6. Check every generated artifact in CI, and split the receiver pin's two reasons (CI)**
- *What:* Three changes.
  - Give the 15 `native/tests/qrss/gen_*_pins.py` scripts and `tools/gen_qrss_unicode_tables.py` a `--check` mode and run every generator's check from one CI entry point; today CI runs two (`ci.yml:71-74`), the listener pins' check runs nowhere, and four run only inside deselectable pytest files.
  - Turn generating expressions kept only in ctest comments (about 30 of 74 QRSS ctest sources, counted; `native/tests/qrss/test_frontend.cpp:1-18`) into generators, and discover `tools/gen_qrss_rx_config.py`'s modules rather than hand-keep them (:59-60).
  - Split `rx_pins.json` so the three sources pinned only because the C++ mirrors them (`live.py`, `wavio.py`, `qrss_receive.py`; `tools/gen_qrss_rx_reference.py:77-81`) ask for a re-read of that C++, not a regeneration.
- *Why we think so:* Committed pins have changed between runs with no edit (Lessons, the pins lesson), and each of stage 3's many `live.py` changes cost about a minute of `--all` that could not move a reference.
- *Gain:* Stale pins caught per push (inferred), and about a minute saved per listener-only change (computed from CLAUDE.md:32).
- *Cost:* Moderate, about 300 lines over 17 scripts.
- *Risk / constraints:* Pins regenerated once into the split format. On air: no change.

**T7. Record each bar's worst case per platform, and derive the bar from it (test only)**
- *What:* Nine parity files already collect worst cases (as `tests/test_qrss_native_receiver.py:125-134` does). Write them to a committed JSON per platform, make each bar a fixed multiple of its recorded worst, and fail when a value exceeds the bar or the worst falls far below it, since a loose bar is where a mutant hides.
- *Why we think so:* The receiver file keeps its worst cases in a hand-copied docstring (:19-29), where the element-by-element z error has under 2x of margin (5.7e-7 relative against a 1e-6 bar). Once T1 runs elsewhere, libm and SIMD differences will move values that are bit-identical here, and without data the easy answer is to widen bars until the run is green.
- *Gain:* Bars that move for a measured reason (inferred).
- *Cost:* Small to Moderate, about 120 lines plus one recording run per platform.
- *Risk / constraints:* Test-only. On air: no change.

**T8. Make every fixture assert its premise, and put a test on every threshold (test only)**
- *What:* Each fixture builder asserts what it is for (callsign windows present, a symbol erased, header LLRs nonzero, an input exactly on the comparison under test), as `test_the_tracks_cover_what_they_claim` (`tests/test_qrss_native_demod.py:66`) does, and every `<`, `<=`, `>` and `>=` in a ported module gets one case on the boundary and one on each side.
- *Why we think so:* The fixture gaps and boundary mutants in Lessons (Wrong fixtures; Mutation testing) were all found late, by reviewers.
- *Gain:* Those gaps found when the fixture is written (inferred).
- *Cost:* Small, about 5 to 15 lines per file.
- *Risk / constraints:* Test-only. On air: no change.

**T9. Commit the mutation runner the reviewers kept rewriting (test tooling)**
- *What:* A `tools/qrss_mutate.py` with committed mutant lists per module (file, line, replacement, the tests expected to catch it) that applies one mutant at a time, rebuilds in its own build directory, runs the named tests, restores the source, and refuses a binary older than the mutated source.
- *Why we think so:* Every wave mutation-tested from scratch scripts that now exist only in the port's scratch directory, and the stale-binary case in Lessons shows the hazard the binary check guards.
- *Gain:* Mutation results that outlive the session that found them (inferred).
- *Cost:* Moderate, about 250 lines plus lists the reviews already wrote once.
- *Risk / constraints:* One rebuild per mutant, so it belongs in the nightly job. On air: no change.

**T10. Pin what the no-Python chain decodes, not only that it ran (release check)**
- *What:* Commit the reference's transcript for the chain test's fixed-seed recording (header fields per step, the members `--retro` attaches, the decoded latents and PNG), and have `native/tests/qrss/test_em_chain.cpp` compare against it: header and members exactly, the picture byte for byte where the onnxruntime version matches and to a PSNR bar where it does not.
- *Why we think so:* The release bar is the reference's decoded picture and header, but this test checks only exit statuses, "the lines that matter", written PNGs and the absence of libpython (:19-21): a release decoding a different picture passes. It is also the only decode check the macOS x86_64 slice can run, since that slice is cross-built without the Python module (`native-build.yml:275`).
- *Gain:* The release bar checked on all five platforms (inferred).
- *Cost:* Small, about 120 lines.
- *Risk / constraints:* Generate the transcript under the C++'s onnxruntime and use the PSNR bar on any platform that runs another, macOS x86_64 among them (versions: T2). On air: no change.

## Running the next port

Lessons describes how the port was run (see the head for its timeline). These entries are about what cost time or nearly lost work, ranked by expected value for a port of the same shape; most are cheap.

**P1. Keep the plan, the records and the work in progress in git (process)**
- *What:* The plan and the running insights log live in the repository (for example `docs/qrss/cpp-port/`), edited only by the integration step. Each module's porter pushes work in progress to a `wip/<module>` branch at every green step, and the wave's module manifest (worktree, base commit, files owned, state) sits beside the plan with a resume procedure.
- *Why we think so:* The shared folder has returned I/O errors since about 03:00Z on 2026-10-14, so the insights log (`port-insights.md`) stops at wave 6 and CLAUDE.md:25 still points at a plan there. A container restart cut wave 3 off at 02:55Z on 2026-10-13; its work survived only as commits 584110a and 60b0c73, and the rerun resumed within about 40 min (f828256 at 03:35Z; computed from git log), finishing stage 1 at 06:16Z. A lost disk would have taken that work too (inferred).
- *Gain:* No work lost to a restart, and one record every later reader can find (inferred).
- *Cost:* Small: a convention and about 50 lines of resume script.
- *Risk / constraints:* The `wip/` pushes need the git proxy to allow a second branch, which it probably does (inferred). On air: no change.

**P2. Run the five-platform CI once per stage, and close the gaps it will show (CI)**
- *What:* Five changes.
  - Dispatch CI on the port's head at the end of each stage, or keep a draft pull request open, and triage before the next.
  - Replace the ThreadSanitizer list kept by hand twice (`ci.yml:257-265`, `:272`) with a ctest label beside the glob (`native/tests/CMakeLists.txt:401`). It names 23 QRSS ctests and none of stage 4's five, though `test_session_sharing` (`native/tests/qrss/test_em_api.cpp:646`) starts a thread.
  - Run the chain test against the packaged binary (`$SSTVAE_QRSS_BIN`) with the models set; the packaged check runs only `listen --help` (`native-build.yml:421-423`), which shows the binary loads, not that it decodes.
  - Budget a round for Windows, whose file-locking, I/O and console-handler branches were only compiled, with mingw `-fsyntax-only` (wave 8).
  - Measure the scalar paths: the AVX2 clones exist only for GCC or clang on x86 (`native/core/qrss/em_search.cpp:142-143`), so MSVC and aarch64 run scalar loops of unmeasured speed.
- *Why we think so:* Each is a check that exists but does not reach the newest code; the ThreadSanitizer list, for one, dates from wave 9b, before stage 4.
- *Gain:* No dB; platform faults found while the porter who wrote the code still has it in mind (inferred).
- *Cost:* Small, about 30 lines of YAML and CMake, plus one CI round per stage.
- *Risk / constraints:* receiver_slot's ctest runs about 13 times slower under ThreadSanitizer (see Lessons), so label only tests that start threads. On air: no change.

**P3. Cut the cost of touching a shared header (build)**
- *What:* Build one tree per module while it is ported, and the codec tree only where needed and at integration. Add ccache with `CCACHE_BASEDIR` at the worktree root so worktrees share objects. Lighten the most-included headers (direct includers at 4e92d36: `qrss/config.hpp` 80, `frame.hpp` 76, `qrss/types.hpp` 46, `rx_config.hpp` 36, `store.hpp` 24, `pyjson.hpp` 23, `trackbase.hpp` 22), starting with `pyjson.hpp`'s templates and `store.hpp`'s class body, and list the ctest sources rather than glob them (`native/tests/CMakeLists.txt:401`).
- *Why we think so:* Touching `pyjson.hpp` or `store.hpp` rebuilt most of a tree in about 7.5 min at `-j2`, about 15 min for both trees (measured, wave 10), and one glob re-check rebuilt 197 steps (wave 10). The codec tree's first build took 9.5 min against a 600 s command cap (measured, wave 7).
- *Gain:* About 7.5 min per header touch from building one tree (computed); with ccache, a fresh worktree's first build mostly hits the cache (inferred).
- *Cost:* Small for the tree and ccache; Moderate for the header split, about 200 lines moved.
- *Risk / constraints:* 18 GB of disk is free beside the 2.7 GB and 2.8 GB trees (measured), so a 5 GB cache fits (computed). On air: no change.

**P4. Settle the parity bar and the binding scope before wave 1 (before the first module)**
- *What:* One page in the plan, before the first module starts: what a release must match, which intermediates get bindings and to what tolerance, whether bindings copy the reference's refusals for inputs no caller produces, and which file formats are promises.
- *Why we think so:* The bar was set at 05:17Z on 2026-10-13, after waves 1 and 2 had worked to the plan's per-module parity and its rule that every refusal reproduce the reference's text, with no release bar above them (where the reviews went: Lessons).
- *Gain:* Review time spent on numerics, concurrency and I/O instead of emulated argument errors (inferred).
- *Cost:* Trivial: about an hour of the owner's and maintainer's time.
- *Risk / constraints:* An early bar can be wrong; change it in the plan, not module by module. On air: no change.

**P5. Integrate continuously, with an owner for every shared file (process)**
- *What:* Give each file every module edits (the rows under `tests/qrss_native/`, `tools/gen_qrss_rx_config.py`, the CMake lists, CLAUDE.md) an owner who takes the others' patches. At each wave's midpoint, merge every module's head into a throwaway branch, build once, and run the combined `--native-qrss=rx` suites and T3's whole-caller group. Fix reference bugs on a branch of their own and merge it everywhere, as waves 7 and 9a did.
- *Why we think so:* Faults between modules surfaced only at integration or later (cross-module faults: Lessons; the 4e92d36 case: T3).
- *Gain:* Cross-module failures found while both porters are still working (inferred).
- *Cost:* One union build per wave, about 7.5 to 15 min (inferred from P3's figures), plus the combined run.
- *Risk / constraints:* Process only. On air: no change.

**P6. Staff reviews by the edges a module owns, and commit the checklist (review)**
- *What:* Two reviewers (numerics; concurrency and I/O) on modules that own file I/O, threads, processes, command lines or platform branches, one on pure computation. Commit the checklist the wave scripts retyped: spies installed and the reference computed before rows apply, and one configuration per row whose caller stays Python (wave 6a); call counts, target-is-binding assertions and no Python objects in a released-GIL block (wave 5); a rebuild before probing a mutant (wave 10).
- *Why we think so:* Fixes followed the edges (Lessons): the numeric waves 5 and 6a fixed 4 and 3, wave 4's 25 were almost all argument handling in bindings, and the I/O waves 8, 9a and 9b with their follow-up fixed 45 (computed). Wave 10 still fixed 21, so a second reviewer may pay on large numeric modules too (inferred).
- *Gain:* More I/O and concurrency bugs found in the module's own review (inferred).
- *Cost:* One more reviewer per module with I/O or threads, and a page that already exists in the scripts.
- *Risk / constraints:* Process only. On air: no change.

**P7. Generate binding probes from recorded calls (test tooling)**
- *What:* Record every call into each row during one reference run (types, dtypes, shapes, strides, memory order, `None`, value ranges), then replay each with its layout and types mutated (Fortran order, non-contiguous, read-only, lengths 0 and 1, NaN, `np.int64` for int, float32 for float64) against both implementations, requiring the same result or the same refusal.
- *Why we think so:* A memory layout no hand-written parity test used broke the first whole-caller A/B (the cell_lam case in Lessons).
- *Gain:* That class of binding bug found for every row without hand-written probes (inferred).
- *Cost:* Moderate, about 300 lines.
- *Risk / constraints:* Test-only; nightly. On air: no change.

**P8. Measure with a committed harness, as an exclusive step (tooling)**
- *What:* Commit the harness used for Measured (a C launcher reading CPU time and peak RSS from `wait4`, a runner and a summarizer, about 220 lines) as `tools/qrss_measure/`, adding a machine-wide lock so nothing builds beside it, a load threshold, provenance (load, CPU, toolchain, commit) with every number, and best of N with the spread.
- *Why we think so:* The 56 `qrss_core` ctests took 60.7 s beside a build against 33.8 s idle (wave 7), 80% longer (computed) and more than many C++ optimizations gained, such as 29.6 ms to 18 ms for the preamble sums (wave 5). `ru_maxrss` on a pytest-spawned child is floored by pytest's own RSS (wave 9b).
- *Gain:* Numbers comparable across waves and machines (inferred).
- *Cost:* Small, about 100 lines on top of the existing ones (inferred).
- *Risk / constraints:* A measurement holds the whole machine; schedule it between waves. On air: no change.

**P9. Give each porter a sandbox-friendly runner and its own scratch (tooling)**
- *What:* A `tools/port_run.py` with literal-argument subcommands (build, clean, and thin wrappers over T3's runner and P8's harness) that sets a per-module `TMPDIR` and `--basetemp`, removes only its own directory, stops only process groups it started, and refuses `uv run` in a worktree without a `.venv`.
- *Why we think so:* The sandbox refused compound commands, shell variables and loops over a command name (waves 4 and 9a); `pkill -f` on a script name killed the porter's own shell, exit 144 (waves 9a and 9b); `uv run` silently made a `.venv` without pytest (wave 6b); and wave 10's skeleton build deleted the shared scratch directory `bt/`, which could have held another module's live temporaries (wave 10).
- *Gain:* Fewer turns spent on tooling, and no module's work able to delete another's files (inferred).
- *Cost:* Small, about 200 lines.
- *Risk / constraints:* Sandbox rules may change; keep to plain argument lists. On air: no change.


## Where the port stands

**Delivered.** Stages 0 to 4 and packaging are on waveform_l (commits in What the port delivered). On 2026-10-15, 5e1ae13 (reviewed in 3787d20) removed the CLI's last "not ported" refusal: sstvae-qrss receive now opens the multi-pass store (--store), writes the capture to the passband store (--passband, default STORE/passband; --no-passband turns it off), associates every pass and prints its "stored" line, as qrss_receive.py does. Its parity file compares both implementations' store trees file by file in 21 cases. The app's listener, sstvae-qrss listen, always stored.

At 4e92d36 every check passed but one: 75 of 75 QRSS ctests without the codec and 76 of 76 with it (the extra is qrss_core_codec; the no-Python chain test, 12.8 s, runs only where the binary has an encoder and models), every parity group, the QRSS suites plain and with --native-qrss=rx, and the generator, layering and include checks. One test, test_morse_table_matches_the_spec_sim, fails only because the shared folder returns I/O errors (T5).

Python still runs in three places: SSTVAE_QRSS_BACKEND=python selects the Python transmitter and listener in the app (native/gui/qrss_window.cpp:531); qrss_simulate.py, channel.py and si5351.py have no C++ (simulation, not station code); and the parity harness needs the reference by design. Accepted differences, such as L's float output within one ulp, are listed in Code health.

**Not covered.** Everything ran on one x86-64 Linux machine with simulated audio (Measured lists what was not timed). The Windows-only file-lock and I/O branches were only compiled (mingw -fsyntax-only), and the macOS x86_64 slice cannot run the parity files (P2, T10).

None of the port's commits has run in CI, no workflow runs the substituted suites, the chain test passes as a skip without models (T1, T10), and the ThreadSanitizer list lacks stage 4's threaded tests (P2). The bars were set with the test machine's numpy 2.5.3, scipy 1.18.1 and onnxruntime 1.31.0, already out of step with uv.lock and with the C++'s onnxruntime 1.28.0 (T2). The running insights log in the shared folder stops before the end of stage 2, and CLAUDE.md still names a plan there; Lessons stands in for the log, and P1 moves both into the repository.

**Needs Jeff.**
- Rebuild the app on Hamlet, the station computer that runs the app and its streamed listener, so its sstvae-qrss carries stage 4 and the receive fix. The plan kept SSTVAE_QRSS_BACKEND=python until each stage had run on Hamlet; say when that switch can go, which decides B12's worth beyond the harness.
- One CI run of waveform_l, by a draft pull request or workflow_dispatch (T1, P2).
- Decisions: retiring the two 2026-10-12 rule switches after an on-air A/B (K7); flag or default for changes that move stored CE results (R10, O12, O9, O10, R9, O11, C1); the CLIs' precision default and other disagreements (B10); the spec owner's items (R7, F3, F5, F6, F7); and a go for B2's and B8's false-alarm recalibration.
- Recordings: off-air captures for D11's replay acceptance, K7's A/B (a signal near 300 Hz and one beside a strong CE signal) and B8's measurement.


## How these were checked

**Sources.** The main source is the records kept during the port by every porter and reviewer, sorted into 808 items of eight kinds: 135 on process, 150 Python bugs, 144 review bugs, 76 on performance, 94 on numerics, 101 deviations, 74 open items and 34 simplifications (counted). The others are the approved port plan, the 2026-10-12 ideas document, whose 66 ideas were each given a port status, and one measurement run of the finished port on 2026-10-14 and 15 (Measured).

**Rechecking.** A record's status could be stale, since later waves fixed items that earlier ones left open, so every status, count and file:line in these parts was rechecked against the code at 4e92d36. 5e1ae13 and 3787d20 changed receive.cpp, the store and the passband store after that, so those three were read at 3787d20 and are cited by function name, which survives line shifts. The Python bug count was deduplicated: 150 recorded findings, plus 4 bugs found while fixing the Python, came to 127 distinct bugs (counted).

**Measurement method.** See "How it was measured" at the top of Measured.

**Not done.**
- No test suite was run for this document, and nothing in the repository was edited. The results at 4e92d36 in Where the port stands are the port's own final run.
- The measurement scripts (mtime.c, bench.py, summarize.py and the comparison scripts) are in the session's scratch directory, not in the repository (P8).
- The port plan and the 2026-10-12 document were read from local copies, since the shared folder returned I/O errors (T5).
- No inferred gain was tried, and the line counts given for fixes are estimates (inferred).
- The per-module timings in the records were not re-run.
- A claim marked "read from the code", such as B5's lost merge, was not exercised.


