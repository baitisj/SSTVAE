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
