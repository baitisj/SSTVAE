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
