# Facts every part of the retrospective must agree on

These come from the Build thread's own records and git log of waveform_l. Use them as given; if a source you read contradicts one, say so in your return value rather than silently picking one.

## What the port was
- Jeff asked for a C++ implementation of QRSSTVAE on 2026-10-12 at 20:02Z, "same strategy as SSTVAE/Data2G": module by module, Python stays the reference, pybind bindings plus a `--native-qrss=tx|rx` pytest switch so the existing tests/test_qrss_* suites run against the C++.
- On 2026-10-13 at 05:00Z Jeff relayed the maintainer's (Andrew, KC2G) wish: no Python in release builds. So stage 4 (EM refine, retro search, `decode --em/--retro`) was in scope.
- On 2026-10-13 at 05:17Z Jeff left the parity bar to Claude, after Andrew said to match decoded pictures rather than intermediates. The bar chosen: the transmitter is bit-exact; a release must match the decoded picture and the header; per-module Python/C++ intermediate parity is kept in the test harness only; the store's npz/JSON layout is no longer a compatibility promise.
- On 2026-10-13 at 05:24Z Jeff asked for this retrospective: "Once you complete Stage 4 and are at the end of this implementation -- as I have earlier, I would love for you to express your insights about how things could be better and how we can make optimizations that might be useful."
- Everything in QRSSTVAE is experimental (Jeff, 2026-10-12): there are no older listeners to stay compatible with.

## Stages and commits (waveform_l, baitisj/SSTVAE)
- Stage 0 (build wiring, generated constants, SHA-256, sequences, the --native-qrss harness): 3843897, 2026-10-12 21:17Z.
- Stage 1 (transmitter bit-exact, `sstvae-qrss` CLI encode/beacon/transmit, in-process API qrss/api.hpp): done 7268bc6, about 2026-10-13 06:16Z. The app's QRSS sends moved to the in-process C++ transmitter in 2b6d29b (UI thread, 2026-10-13 06:32Z).
- Stage 2 (per-pass receiver: front end, math, demod, A1/A2/A3, track, ltrack, acquisition arbitration, receiver, receive CLI): waves 4 to 7, done c0c830f.
- Stage 3 (store, association, passband store with its own FLAC encoder, live pieces, listener, `sstvae-qrss listen`, in-process listen_api): waves 8, 9a, 9b and a follow-up, done 89653b9. The app's QRSS listener has been the C++ `sstvae-qrss listen` since 39fa50c (UI thread, 2026-10-14 17:35Z).
- Stage 4 (EM refine, retro template search, `sstvae-qrss decode --em N / --retro`, Qt-free app API em_api.hpp): wave 10, merged b84c1f8 (2026-10-14 22:54Z), plus 4e92d36 (a stale whole-caller test fixed after the merge).
- Packaging: 8cd20f5 (2026-10-14 17:37Z) ships sstvae-qrss beside the app in all three desktop packages, signs it on Windows and checks in CI that `sstvae-qrss listen --help` runs from the package.
- Elapsed: 2026-10-12 20:02Z to 2026-10-14 22:54Z, about 51 hours.

## How it was run
- Workflow "waves" of agents, each lane porting one module in its own git worktree, followed by an adversarial review agent per lane that read the C++ against the Python line by line, probed edges, mutation-tested the new tests, and fixed what it found. Then an integration step merged lanes, rebuilt both trees and re-ran everything.
- About 98 agents across the waves (map 10, wave 1 10, wave 2 6, wave 3 2 plus its rerun 6, wave 4 8, wave 5 8, wave 6a 6, wave 6b 1 plus 6, wave 7 7, wave 8 6, wave 9a 6, wave 9b 6 plus a 2-agent follow-up, wave 10 8). Wave 3 was cut off by a container restart and redone.
- Review bugs fixed (findings marked fixed): about 211 in all (wave 1 46, wave 2 12, stage 1 completion 13, wave 4 25, wave 5 4, wave 6a 3, wave 6b 16, wave 7 19, wave 8 15, wave 9a 12, wave 9b 16, follow-up 2, wave 10 21 of 28).
- Two builds: native/build-qrss (codec off, the pybind module on) and native/build-qrss-codec (codec on). Machine: a shared 4-CPU cloud container.
- The shared project folder /mnt/project-files returned I/O errors in this container from about 03:00Z 2026-10-14, so the running log port-insights.md was not updated for waves 7 to 10 (held locally) and one test that reads the spec from that folder (test_morse_table_matches_the_spec_sim) fails here for that reason only.

## Final verification on the merged tree (HEAD 4e92d36)
- ctest: 75 of 75 in the codec-off tree, 76 of 76 in the codec tree (the extra one is the no-Python end-to-end chain test, test_em_chain, 12.8 s).
- Every parity group passes; the QRSS suites pass plain and with --native-qrss=rx, except the one EIO test above.
- Generator checks current (tools/gen_qrss_rx_config.py --check, gen_qrss_listener_pins.py --check, gen_qrss_live_pins.py --check, tools/gen_qrss_rx_reference.py --all --check), check_layering and check_includes ok.

## Inputs you may read
- Per-kind findings gathered from every wave: /tmp/claude-0/-home-user-sstvae/92006b74-04c2-5210-829f-64b8a831fdbf/scratchpad/retro/in/{process,python-bug,review-bug,performance,numerics,deviation,open-item,simplification}.json (each item: wave, kind, status fixed|open|n/a, where, what, evidence, group). Statuses can be stale: later waves fixed some items an earlier group marked open. Check the current code before calling something open or fixed.
- waves.json and notes.json (per-group notes; notes["format"] describes the earlier optimization-ideas.md document and lists its 66 ideas with their port status).
- The earlier document Jeff liked, whose format this one imitates: scratchpad/opt/ideas.md ("QRSSTVAE: optimization ideas from building waveform L", 2026-10-12, about 22,000 words).
- Wave 10's 60 insights: scratchpad/w10_insights.json. Raw wave results: scratchpad/w6b_result.json, w7_result.json, w8_result.json, w9a_result.json, w9b_result.json, w10_result.json. The approved plan: scratchpad/port_plan.md.
- Measurements of the finished port (when present): scratchpad/measure/results.md and results.jsonl.
