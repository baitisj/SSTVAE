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
