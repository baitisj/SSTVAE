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
