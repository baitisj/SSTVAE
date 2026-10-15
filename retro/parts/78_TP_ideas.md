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
