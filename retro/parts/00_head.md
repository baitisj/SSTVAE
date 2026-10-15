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
