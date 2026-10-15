# QRSSTVAE: optimization ideas from building waveform L

2026-10-12

**Short answer.** Yes. Building L found these:

- **A measured fault in both waveforms.** Near the 300 Hz and 2700 Hz band edges the noise floor reads low, so the reported SNR is up to 8.4 dB too high.
- **A cheap way to find L sooner during a pass.** If L senders send a lead-in tone and the receiver looks at it, L is about 5 dB easier to find while the pass is still coming in.
- **A finished CE feature the listener never calls.** It can re-receive a pass once that pass's header is known.
- **Wasted time in the live listener.** In a few places it spends tens of seconds of CPU, or holds its lock for that long, on work it does not need to do.

Building L also showed that L's deep multi-pass figures come from models that nobody has yet checked end to end. The expensive L work should wait for two offline measurements.

QRSSTVAE is an ultra-slow, very narrow HF picture mode built on SSTVAE. It sends the SSTVAE v5 codec's 132x30x40 latents as analog values on one narrow signal. Each pass lasts about 30 minutes and starts on a quarter hour. Modes A, B and C carry 1, 2 and 3 latent groups. Several stations fit in 2.4 kHz, and repeat passes of the same picture, later in the day or the next day, are combined for sensitivity. There are two waveforms:

- **CE** uses constant-envelope phase modulation, which suits Si5351-based rigs such as the uBitX. It puts 99% of its power in 51.5 Hz. It is on the air, in format v2 since 2026-10-10.
- **L** is a pilot tone plus QPSK for linear SSB rigs. Its 99% width is 21.5 Hz and its PAPR is 3.15 dB. So far it has only been run in simulation.

The spec puts a good one-pass picture on a quiet fading path at -10.9 dB SNR (CE) and -14.0 dB (L), measured in a 2500 Hz noise bandwidth. After 16 passes the figures are -26.4 dB and -29.6 dB. These are model figures. CE's has been confirmed in simulation; L's has not.

SSTVAE, the parent mode, sends the same codec's latents on 24 OFDM carriers in about 1.2 kHz. It delivers a picture in 32 to 95 s for signals around the noise level and stronger. Compared with it, QRSSTVAE takes about 56 times the airtime for the same mode, in a signal 23 to 56 times narrower. In return it works roughly 10 to 30 dB further below the noise, depending on the waveform and how many passes are combined.

## Top picks

1. **R1: fix the noise floor near the band edges (both waveforms).**
   - Within about 100 Hz of 300 Hz or 2700 Hz, the receiver takes its noise bins from the skirt of its own front-end filter. SNR then reads up to 8.4 dB high (measured).
   - In the one joint-mode case measured, an L pass beside a CE signal lost 6.5 dB of latent SNR and ended "lost".
   - It matters now because docs/qrss/60m.md recommends audio offsets that start at 300 Hz.
   - On air: no change. CE in mid-band stays bit-identical; CE channels near an edge need a flag.
2. **O2: stop gating a strong signal's own sidebands.**
   - Every CE signal at -15 dB or stronger produces A3 hits about 11.5 Hz either side of its carrier, and each of those is gated and then refused.
   - Measured on SHORT at -12 dB, gating a lone CE signal takes 4.9 to 6.8 s; without the refused gates it would take about 1 s. With an L neighbour it takes 8.0 to 8.6 s instead of about 1.5 s. That is most of joint listening's extra CPU.
   - On FULL, each refused gate costs 60 to 80 s of the 180 s finish budget.
   - On air: no change.
3. **F1 + L1: send the lead-in by default for L, and have the receiver look at it.**
   - Two measurements put A1, the first acquisition stage, about 5 dB deeper.
   - Gain while the pass is coming in: L reach goes from about -26 dB to about -32 dB.
   - Gain for stored passes: about 1.5 to 2 dB, because the end-of-slot search already finds them to about -31 dB.
   - On air: F1 changes only a transmitter default inside the current format, so old listeners decode as before.
   - Both measurements used a steady lead-in tone. A rig that drifts at key-up will gain less.
4. **R10: re-receive stored passes once the store supplies their header.**
   - The CE code for this exists and the listener never calls it.
   - Measured gain per deep L pass: 0.1 to 0.5 dB. At -12 dB and -6 dB: 0.8 to 1.8 dB.
   - On air: no change. CE needs a flag, and L needs the port in L5.
5. **O1: make association incremental and move it off the listener lock.**
   - Measured: the lock is held for 16.3 s per decoded header with a store of 30 accumulators and 58 provisional clusters. During that time the live tiles freeze.
   - The cost grows quadratically with the size of the store.
   - On air: no change. CE results unchanged.
6. **D10 and D1: measure L's deep chain before building for it.**
   - L's 16-pass figure of -29.6 dB assumes EM-aided tracking, which L's receiver does not have. L's header thresholds are still estimates.
   - Both measurements run offline and take minutes to an hour.
   - They decide whether to build L2, L5 and F3.

**Build first: R1 and O2.**

- Both are small and measured.
- Both are receiver-only.
- Both fix behaviour that is wrong on the band today.

Land the CPU items first. Each of the L acquisition additions (R3, L1, L3, L6, L7, L11) adds work to every slot. The end-of-slot finish runs in one child process at nice 5 (live.py:359-430). Listening for both waveforms already costs 64% more: 12.6 s against 7.7 s on a CE SHORT recording.

After those, do F1 + L1, once the key-up chirp has been checked. Then do R10 and O1, with D10 and D1 running offline alongside. For CE on its own, C2 is the best-supported item.

**One decision comes before all of this.** The L transmitter (d357ec8) exists only on the waveform_l branch, pushed 2026-10-11. The app on that branch already offers L transmit (native/gui/qrss_tx.cpp:33-34, 155-157). L format changes (the F2 batch) cost nothing only until someone keys L from that build. Either hold L off the air until the spec owner has decided the batch, or accept that later changes will need a format version bump.

## What building L taught us

### Where L is limited

- **L's weak pilot causes most of L's receiver weaknesses. Gate V, the verification stage, is where L is strong.**
  - The pilot carries 13.8% of L's power. In the preamble it carries 10.5%, because the clipper squeezes it to gain 0.656 there against 0.708 in the data. CE's carrier carries about 53%.
  - **Live A1 limit.** That one fact explains why live A1 stops at about -26 dB, while A2-L passes 8 of 8 at -31 dB.
  - **Fading loss.** It explains the 1.2 to 1.8 dB round-A loss on moderate fading, which is noise in the pilot track. Using the true pilot gain as the data gain costs only 0.05 to 0.10 dB (design.md:935, 945).
  - **Gain-scale spread.** Per pass, the slope is 0.83 to 1.49 at -24 to -28 dB against CE's 0.92 to 1.04 (opt/buildrec/m_rounds.py).
  - **Fine path.** It explains why refine_path_l's fine pass switches off by -16 dB.
  - Meanwhile L's Z_ref at equal average power is about 1.5 times CE's: 95 against 62 at -12 dB.
- **Which stage binds depends on the path.**
  - **Stored passes.** When at least half the slot was heard, the finish runs A3 over the whole slot (live.py:341-344). Measured this round, A3 finds L_FULL in 10 of 10 trials at -30 dB on steady and quiet paths, and in 0 of 5 at -32 dB steady. Its cliff is therefore about -31 dB. Quiet fading reads slightly stronger than steady: z 55 to 57 against 52 to 54 at -30 dB.
  - **Live tiles.** A1 binds only for live tiles, and for a finish that heard less than half the slot (live.py:345-357).
  - Throughout this list, gains while a pass is coming in are large and gains for stored passes are smaller.
- **At the 16-pass good point, detection is not yet L's limit. Deeper, it is.**
  - L's 16-pass good picture needs about -29.6 dB per pass, and A3 finds passes at -30 dB.
  - The deeper targets lie past A3's cliff: the 32-pass and 64-pass good points (-32.3 dB and -34.9 dB), and the 16-pass usable and recognizable points (-34.3 dB and -37.7 dB).
  - CE has more headroom: its blind detection found every pass tried at -32 dB, against its 16-pass point of -26.4 dB (spec 5.1).
  - L's multi-pass figures come from the spec model (sims/l_spread.py). The model assumes EM-aided tracking, which L's receiver does not have, and no L multi-pass run has been done end to end. The run in e2e-results.md, where 8 passes combined as well as theory allows, was CE.
  - The L figures are therefore probably optimistic (inferred). That is why D10 comes before L2.
- **The lead-in is L's cheapest acquisition lever, and it is already in the format.**
  - Ten seconds of pilot at full average power carry about 6.8 dB more pilot energy than the preamble's 20 s at a 10.5% share.
  - Two independent measurements put A1 about 5 dB deeper once the A1 window reaches back over the lead-in (L1).
  - More A1 lines are not the cure that lesson 2 in waveform-l.md suggests. Going from 16 to 64 lines adds 0 to 3 detections in 20 between -24 and -29 dB. At -29 dB it is the 1e-4 threshold that loses the pilot, not the line count (opt/docs-spec/a1_rank2.py).
  - First caveat: the simulated lead-in tone did not drift, but spec 2.8's stated reason for the lead-in is key-up drift.
  - Second caveat: the finish's A3 already starts 10 s before t0, so the 5 dB applies to live tiles, not to stored passes.
- **L buys bandwidth, not dB, at a fixed peak.**
  - L's one-pass good picture is -14.0 dB against CE's -10.9 dB at equal average power, but -10.8 dB against -10.9 dB at equal peak power. It does this in 21.5 Hz against 51.5 Hz.
  - On a band limited by peak power, such as US 60 m (9.15 W ERP), the tie is what counts.
  - The 3.2 dB average-power edge matters for stations limited by heat or duty cycle, which waveform-l.md names as L's case.
  - The app uses one peak-scaled transmit level for everything (tx_panel.cpp:1585), so that edge cannot be used today (O4).

### What the clipper and the header cost

- **Clipper self-noise matters only where L is not meant to operate.**
  - With the scrambler and precoder, it averages down across passes like channel noise.
  - L's 12.2 dB clipper SINR costs about 0.46 dB at the one-pass good level; CE's 13.3 dB costs about 0.36 dB. At 16 passes near -29.6 dB the cost is under 0.05 dB (computed, and consistent with the 0.06 dB fixed part in spec 2.4).
  - The clipper is close to a hard limit even at 8 kHz. The peak is 3.20 to 3.24 dB against 3.15 dB at the 99.99% point, and nothing exceeds 3.5 dB in 13 million samples. A plain peak scale therefore costs only about 0.07 dB.
- **The clipper treats sections unequally.**
  - Constant-amplitude preamble and header QPSK keep gain 1.006 and 16.9 dB SINR. Gaussian data drops to gain 0.889 and 12.2 dB (opt/transmit/m2_peaks.py).
  - So on steady paths L's header is probably deeper than its -27 dB estimate. CE's equivalent estimate turned out 5 dB too pessimistic.
  - A1 during the preamble is 0.66 dB worse off than it would be with a uniform pilot.
  - The spread header copy is the most expensive constant L inherited. For CE it is free once the header is known; for L it costs every latent 0.23 dB, and its gain on L has not been measured (D1, F3).
- **On fading, headers are limited by tracking, not by the code.**
  - With both copies, CE's header decodes at -30.3 dB steady, -29.0 dB quiet and -26.9 dB moderate.
  - At -28 dB moderate, gate V fails to fit the timing on 14 of 16 passes (spec 5.1).
  - L's tracking is weaker, so its fading header is probably no better than CE's.
  - A larger decoder list measured +0.3 dB on steady paths at 1.4 times the decode time (R7). On fading, timing fixes (C2) are the better lever.

### What the receiver inherited from CE

- **The spacing constants were sized for 50 Hz CE and do not scale to L's packing.**
  - They are: the 45 Hz neighbour guard, the 85 to 120 Hz noise windows, 16 A1 lines at least 10 Hz apart, GHOST_HZ of 20 Hz (L's band reaches 22.07 Hz below its pilot), and MERGE_HZ of 5 Hz.
  - At the spec's 30 Hz L spacing, one neighbour 90 Hz away already removes a whole noise window.
  - The one field failure, at KC2G, was a different case: a CE neighbour that had not been excluded at all (README.md:348-357). The dense-L case falls on a similar "take the quieter side" fallback.
  - The receiver, not the waveform, limits how densely L can pack.
- **The band-edge problem is purely a noise-estimate problem.**
  - The front end is flat to within 0.01 dB at 326 Hz and is down only 0.29 dB at 300 Hz, so the signal itself is intact.
  - The noise bins for a 326 Hz signal sit 85 to 120 Hz below it, about 9 to 26 dB down the filter's skirt. SNR therefore reads 6.9 dB high at 326 Hz and 8.4 dB high at 305 Hz.
  - With no neighbour, the two sides are pooled into one median, which the skirt half drags down. With the in-band side excluded, only the skirt side is used (frontend.py:336-347).
  - The flat region (better than -0.1 dB) is about 310 to 2690 Hz, not the 250 to 2750 Hz the build notes assumed.
  - **On air, a rig's SSB filter adds the same bias.**
  - **60 m advice.** The recommended 60 m offsets start inside the failing zone (docs/qrss/60m.md:17-18, from 300 Hz).
  - **Lead-in detector.** acquire.lead_in_span uses the same floor with no exclusions.
- **Estimators inherited from CE turned out biased in proportion to how much L's references feed its gain track.**
  - CE's kappa form read 0.2 to 0.7 on moderately fading L, where the honest value was 0.9 to 1.2 (2 to 3 seeds per case, wfB_parts.txt:282). That overstated W by up to 1.7 dB (design.md D34).
  - Regression dilution makes |delta| read about 9% high at -27 dB.
  - The textbook errors-in-variables correction overshot on fading (0.81 at -20 dB moderate), so a fit against known symbols is the safer fix (L4).
- **Aliases differ per waveform.**
  - CE's symbol-rate aliases sit at ±33 Hz.
  - L's -1/T_L alias lies 16.49 Hz below its pilot, inside its own data band. It passes A2-L in 3 of 12 trials at -12 dB and 12 of 12 at -6 dB and 0 dB.
  - drop_band_owned (acquire.py:1498-1519) already suppresses it for preamble hits. Only the ghost rule after gating is unmeasured (D3).
- **"No CE change" needs care in joint mode.**
  - Several items leave CE's own code alone but add L candidates or hints that share the 12-candidate, 180 s finish budget with CE: L1, L3, L6, L7, L11 and R3.
  - Others keep or add CE passes: O5 and O6.
  - --just-ce stays bit for bit. The default listener's CE output can change in busy slots.

### Measurement traps

- **A pass's frequency at t0, read from the end of the frequency spline, is a poor report on both waveforms.**
  - **Simulated fading.** On moderate fading with zero-mean Doppler it read +0.40, +0.66 and -0.23 Hz off on L, and +0.41 and +0.56 Hz off on CE. On a weak steady L pass it read +1.09 Hz off. The frame median stayed within 0.031 Hz in all 13 cases.
  - **KC2G 04:15Z.** KC2G's real carrier is not a straight line either. On this pass it drifted +0.030 Hz/min with 0.22 Hz rms wander, and the stored value was 0.62 Hz off.
  - **KC2G 03:45Z.** On this pass it drifted +0.005 Hz/min with 0.684 Hz rms and some outliers, and the stored value was only 0.013 Hz off.
  - **Andrew's 0.5 Hz error.** It is consistent with this end-of-spline effect, but has not been traced to a particular pass.
  - **Cross-pass priors.** Timing moves between passes (0.72 s, then 0.82 to 0.90 s), so a cross-pass prior can only use frequency.
- **Latent SNR measured by mean squared error rewards a z that has been shrunk.**
  - **The apparent win.** In the chain runs, round A seemed to beat the header-known genie at -28 dB: -6.37 dB against -7.57 dB (lrx/scratch_chain/extra.txt).
  - **Why it was not one.** z was 17% low there (slope 0.826). Slope-corrected it is -7.99 dB, worse than round B's -7.50 dB.
  - **Reading the slope.** z is unbiased when u_hat is correct (demod.py:208-219), so any slope other than 1 is the tracker's gain scale, which is a useful way to split the error.
  - **Cost of the spread.** The weights scale as 1/k², so the decoder's z times w already down-weights over-scaled passes. The combined cost of the spread is only 0.05 to 0.2 dB (computed).
  - **Mean W above latent SNR.** Mean W reading above latent SNR (+3.94 dB against +1.81 dB) comes from comparing an arithmetic mean with a harmonic one, not from miscalibration: the mean of w·e² is 0.97 to 1.07.
  - **Seeds.** Single-seed figures for how honest W is are noise (1.075 against 0.962 for two seeds). Use 3 to 5 seeds.
- **EM and data-aided re-tracking measure small on both waveforms, unlike the models of them.**
  - CE's EM gives +0.0 to +0.15 dB, against the spec's +0.5 dB (README.md:883; e2e-results.md).
  - L's data prior gave at most +0.38 dB accumulated at -12 dB and about ±0.2 dB at -20 dB. With a block split it gave +0.05 to +0.19 dB. Without the split, a pass's own SNR rises by a false 1 to 2 dB (opt/live-ops/m2_l_em.py).
  - What does pay is knowing the header (round B): +1.0 dB at -12 dB and +1.8 dB at -6 dB for L, +0.8 dB at -12 dB for CE, and 0.1 to 0.5 dB slope-corrected at -24 and -28 dB.
  - The listener never does this. live.py imports nothing from em, so a pass whose header came from association stays a round-A pass in the store (R10).
- **Live CPU does not follow progress.**
  - A refresh at 5% heard costs as much as one at 95% heard: 29.8 against 30.0 CPU s for one CE FULL signal at -12 dB. The tracker and gate run over the whole frame, with the unheard future filled with noise (O3).
  - The costly stages differ. In CE the pure-Python Kalman forward pass is 28 to 30% of receive time. In L it is 10%, because T_L = 2T halves the positions, and the data-band matched-filter gather leads instead.
  - L's tracker is not cheaper than CE's: round A on FULL takes 7.25 s against 6.33 s, and gate V 10.9 s against 13.3 s.
  - The 64% extra cost of joint listening (12.6 s against 7.7 s on a CE SHORT recording) lies in acquisition and refused gates, mostly on CE sidebands.
  - Profiles in this round did different amounts of work (712 against 460 Kalman calls), so compare wall-clock pairs.
- **The SNR estimate's bias depends on where the signal sits.** In mid-band simulated end-to-end CE runs, SNR read 0.3 to 0.9 dB low (e2e-results.md). Near the band edges it reads up to 8.4 dB high (R1).

### Telling the waveforms apart

- **Every CE signal at -15 dB or stronger appears in A3 as three lines: its carrier, and sidebands about 11.5 Hz either side.**
  - The upper sideband has an unmistakable L shape (z1 from +112 to +593).
  - The carrier can be gated a second time as an "L copy", because only candidates within MERGE_HZ (5 Hz) are skipped.
  - On SHORT, the refused gates cost 4 to 7 times the real one.
  - Otherwise, spectral shape separates the waveforms cheaply. L's z1 is +155, +50 and +16 at -15, -20 and -25 dB. CE's |z1| is at most 2.3 at those levels and the noise standard deviation is 1.025. Strong CE reached 4.2 to 5.1 at -10 to 0 dB, which is why the z_above limit of 3 was added.
  - L's band energy is still visible at -34 dB on L_FULL (z1 5.0 along the true path), and at -30 dB A3's best path already runs in the data band. An L-matched A3 emission is therefore worth about 2 to 2.5 dB, not the full 6 dB gap (L3).
- **The D3 residual ratio is a near-perfect waveform discriminator.**
  - CE's own tracks give R from 0.55 to 2.16, fading included. An L signal under CE's gate gives 3.6 to 15.4. CE's sideband ghosts give 5.2 to 6.2.
  - Because L's values overlap the ghosts', the rule needs both R above 2 and Z/sqrt(R) at most 6.
  - Dropping P from the residual model would reject real fading CE passes, which reached R 2.43 and 3.22.
  - The only CE pass seen above R 2 was strong (Z 48), so the margin for a weak fading CE pass is unmeasured.
  - Fielded CE-only listeners will not fail on L. They will misreport it as junk tiles (z 7.2 to 7.3, latents near -8 dB), and each also becomes a junk provisional cluster in that station's store (C1, O7).

### The store and operations

- **Multi-pass association grows quadratically and runs under the listener lock.**
  - Re-measured this round: 16.31 s for one header-decoded pass with 30 accumulators and 58 provisional clusters, about 9.4 ms per pair.
  - **CE and L already combine.** CE and L passes of one picture combine with no extra work:
    - picture_id hashes codec, mode and latents with no waveform in it (picture.py:113-122).
    - The soft header uses the same 2,474 coded bits for both.
    - store.twin compares band centres across waveforms.
  - **store.due gap.** store.due predicts only missing later segments, at the same waveform and frequency. It never predicts a repeat of a segment already held, which is the most common multi-pass case (R3).
  - **resolve_provisional gap.** resolve_provisional runs only on rule-1 and rule-2 attaches, so an incremental version needs a dirty set (O1).
- **For this decoder, erasure costs far more than weakness.**
  - "Half a pass at +3 dB is worth a whole pass at -3.6 dB" (decoder-measurements.md). That argues for sending the 2,200 latents that are never sent today, though the gain is only about +0.1 dB PSNR at the good level and nothing at usable (F6).
  - Identification by matching beats reading. At -20 dB the Morse callsign was read in 2 of 17 passes, while the match test against a known callsign agreed in 17 of 17 (Z 15 to 50; e2e-results.md:93, README.md:756).
- **Half duplex keeps the QRSS listener running through our own send, but feeds it no audio.**
  - The ring is set to nullptr (rx_panel.cpp:379-380; qrss_window.cpp:673-674).
  - Slots under our own send are lost from the ring and the passband store alike (O22).

### Checked and left alone

- **Several transmit parameters are not worth changing.**
  - **CE's beta.** Moving it from 0.8 to 1.0 rad would gain 0.2 dB at 16 passes, but would drop the carrier from 53% to 37% of the power and widen the 99% band from 51.5 Hz to 60 Hz.
  - **Si5351 synthesis error.** It sits 43 dB below the latents.
  - **RRC roll-off.** It sets no SNR.
  - **Preamble length.** The preamble is 1.1% of the frame, with about 7 s of slack before the next pass's full lead-in.
  - **Callsign windows.** Four windows instead of three costs only about 0.03 dB.
- **The AGC's cross-modulation is real but small (measured this round).**
  - A strong L signal modulates the band's gain by 3.95% rms at -6 dB, 6.8% at 0 dB, 15.4% at +10 dB and 20.4% at +20 dB.
  - That caps other signals' SINR at 28.1, 23.4, 16.3 and 13.8 dB, which does not matter for weak neighbours at any operating threshold (R5).
- **"The L preamble is 6 ms longer" is wrong.**
  - Both preambles last 20.00625 s: 660 x 0.0303125 s and 330 x 0.060625 s (constants.py:38, 74). This holds by a stated design rule (constants.py:56-58).
  - A change to either preamble must update acquire.live_end_s. Otherwise the live candidate cache (live.py:1185) freezes before the L preamble ends (D6).

### How the build went

- **Building L exposed CE bugs that were already there.**
  - associate._soft_header decoded an empty LLR sum on headerless frames, so --just-ce failed to store its second TINY pass.
  - CE's refine_timing has a noise term in its likelihood and gives up when the Hessian is not positive definite. Measured this round on CE SHORT at -27 dB, from a 0.15 T start it corrected 0 of 18 tracks and lost 0.46 dB on average (1.47 dB worst). The flaw is therefore live on CE at its multi-pass operating SNR (C2).
  - The joint end-to-end run tested the shared code harder than either waveform's own suite.
- **Mutation testing is what made the build's tests trustworthy.**
  - The demodulator's tests caught 13 of 22 injected changes.
  - Acquisition's first pass left 11 of 20 mutants alive.
  - The receiver integration tests left 6 of 77 alive at first and 2 of 78 at the end (wfC_parts.txt:480-482).
  - Test time is under pressure. L added 33.5 s to the default QRSS tier before six tests moved to slow, and test_qrss_lacquire alone runs 7.4 to 13.5 s against a 5 s target.

## Receiver ideas for both waveforms

Ordered by value. R9 and R10 were added from the verifiers' findings.

**R1. Take the noise floor only from bins where the front end is flat (band edges)**
- *What:* noise_psd takes noise from 85 to 120 Hz either side of a signal, beyond a 45 Hz guard (frontend.py:301-347). Within about 100 Hz of 300 Hz or 2700 Hz, one side lies on the skirt of the 255-tap Kaiser front-end low-pass (frontend.py:59). The fix:
  - Drop bins where the front-end response is below about -0.1 dB, that is below 310 Hz or above 2690 Hz.
  - Take the missing bins from the side toward the passband interior.
  - Never take a minimum. The channel is only ±125 Hz wide, so the noise band cannot widen outward.
  - Make the same change in acquire.lead_in_span (acquire.py:965-968), which calls noise_psd with no exclusions at all.
- *Why we think so:* The front-end response, measured this round, is -6.0 dB at 250 Hz, -0.29 dB at 300 Hz, -0.10 dB at 310 Hz and -0.01 dB at 326 Hz, flat from 340 to 2660 Hz, and mirrored at the top. Further evidence:
  - On white noise, SNR reads high by 8.40 dB at 305 Hz, 6.89 dB at 326 Hz, 2.19 dB at 360 Hz, 0.03 dB at 400 Hz, 4.79 dB at 2660 Hz and 8.31 dB at 2695 Hz (opt/rxcode/m3_edge_cpu.py).
  - In the build records, an L pilot at 326 Hz beside CE at 400 Hz reports +7.3 dB, kappa clamps at 2.0, and the tile ends "lost". Its latent SNR is -0.04 dB, against 6.49 dB with --just-l (wfC_parts.txt:266-269, 302).
  - live.py:1424-1428 then throws away weak headerless edge passes that read above -6 dB, as "not QRSSTVAE".
- *Gain:*
  - Measured: removes a noise-floor bias of up to 8.4 dB.
  - In simulation: up to 6.5 dB of latent SNR in the one measured case (joint mode, a headerless TINY pass, with CE 74 Hz further inside the band).
  - For a lone signal at the edge, the measured harm is the SNR report, the kappa clamp and the headerless drop.
  - On air, a rig's SSB filter skirt adds its own bias, and only the drop-and-fill form handles it (inferred).
- *Cost:* Small:
  - 20 to 40 lines in noise_psd and lead_in_span, plus edge tests near 320 Hz and 2690 Hz.
  - ChanCapture.noise_psd_at already knows mix_audio_hz (track.py:186-193).
  - An alternative is to raise the front-end cutoff by about 50 Hz, or lengthen the filter, so that 300 to 2700 Hz sits inside the flat region. That fixes the signal as well (inferred), but changes every CE result.
- *Risk / constraints:*
  - Touching only bins below -0.01 dB (under 324 Hz or over 2676 Hz) changes only channels centred below about 444 Hz or above about 2556 Hz, and keeps mid-band CE bit-identical.
  - Dividing every bin by |H_FE|² would not keep it bit-identical, because of 2.5e-4 dB of passband ripple.
  - CE channels near an edge need a flag or Jeff's OK.
  - Receiver only.

**R10. Re-receive stored passes with their header once the store supplies it**
- *What:* Sometimes a pass's header comes from association (a soft-header sum, or a later segment) rather than from its own decode. Re-track that pass with its header known (a "round B" pass) in the idle finish child, and replace it in the store. The pieces exist:
  - em.rereceive(p, None, hdr) already does this for CE (em.py:270-345).
  - Store.update_pass exists.
  - Every pass keeps its 250 Hz capture.
  - The listener never calls any of it: live.py imports nothing from em, and EM runs only from qrss_decode.py:177-181.
- *Why we think so:* Round B was measured:
  - L: +1.0 dB latent SNR at -12 dB (from 5.98 to 6.97 dB, lrx/scratch_chain/all1.txt), and +1.8 dB at -6 dB.
  - CE: +0.8 dB at -12 dB.
  - L at -24 and -28 dB: 0.1 to 0.5 dB, slope-corrected (opt/buildrec/m_rounds.py).
  - Round B also cuts the worst per-pass gain-scale error from 49% to 23%.
- *Gain:* Measured on L, 0.1 to 0.5 dB per affected pass at the deep multi-pass levels, and the same in the accumulated SNR. Deep in multi-pass, most passes are affected. CE at deep SNR has not been measured.
- *Cost:*
  - CE: moderate. It means wiring existing functions together, and running about one track per pass at idle (6 to 7 s on FULL).
  - L: needs L5's rereceive port first, plus a frequency path from refine_path_l, since pass_path is CE-only (em.py:250).
- *Risk / constraints:*
  - Stored CE z and w change, so CE needs a flag.
  - The update must replace the pass, not add a second member.
  - Receiver only.

**R2. Report and hint a pass's frequency from a line through the whole path**
- *What:* PassResult.f_hz is tr.freq(0.0), the value at the end of a spline with 10 s knots (receiver.py:292, 337). track.report already fits a line through the frame, but keeps only its slope, as drift_hz_per_min. The fix:
  - Store the line's value at t0 as a new field, with outlier rejection.
  - Use it for live hints, store.due, detection_from_pass and the display.
  - Keep f_hz for pass_uid, so uids do not change.
  - detection_from_pass (receiver.py:398-406) currently mixes the spline's end value with the line's slope; use the intercept there as well.
  - Hints could also carry the fitted drift. CarrierCandidate accepts it, but _hints drops it (live.py:1191-1223).
- *Why we think so:*
  - Simulated moderate fading with zero-mean Doppler: f(0) was off by +0.663, +0.400 and -0.231 Hz on L, by +0.561 and +0.413 Hz on CE, and by +1.090 Hz on a weak steady L pass at -28 dB. Frame medians stayed within 0.031 Hz in all 13 cases (opt/buildrec/m_rounds.py).
  - KC2G 04:15Z: stored 1206.082 Hz against the line's 1205.459 Hz at t0, a 0.62 Hz error, with a slope of +0.030 Hz/min and 0.22 Hz rms.
  - KC2G 03:45Z: the stored value was only 0.013 Hz off, with 0.684 Hz rms and outliers at 1208.38 Hz and 1203.76 Hz (opt/rxcode/m1_kc2g_freq.py, re-run this round).
- *Gain:*
  - Reporting, measured: in simulation the error falls from 0.23 to 1.09 Hz down to 0.03 Hz or less. On KC2G it is 0.6 Hz better on one of two passes and unchanged on the other.
  - Acquisition: about 0 for L, and for CE above about -31 dB. hint_line already takes any A1 line within 2 Hz (acquire.py:196, 424-438), and L hints already try ±1 Hz (acquire.py:189).
  - The 0.5 Hz error on Andrew's (KC2G's) reception is consistent with this end-of-spline effect, but has not been traced to a pass.
- *Cost:* Small. The intercept is already computed. It needs one store field, with a fallback to f_hz for old entries.
- *Risk / constraints:*
  - The line fit needs outlier rejection, as the 03:45Z pass shows.
  - Hinted acquisition changes, so CE needs a flag.
  - Receiver only.

**R4. Exclude each neighbour by its own waveform's occupied band, and never fall back onto a neighbour**
- *What:* An L neighbour is excluded with CE's symmetric 45 Hz guard around its band centre. That covers from 54.5 Hz below its pilot to 35.5 Hz above it, though L occupies only about 22 Hz (receiver.py:62-65). The fix:
  - Exclude each neighbour by its own occupied band plus a small margin: CE about ±26 Hz, L from 22.1 Hz below its pilot to 1.5 Hz above it.
  - When both sides are empty, take the median of unoccupied gap bins across the band, never the quieter side.
  - The same functions as R1 are involved, including lead_in_span.
- *Why we think so:*
  - With a 45 Hz guard, any neighbour centred 75 to 130 Hz away removes a whole 85 to 120 Hz noise side. At spec 10's 30 Hz L spacing, the neighbours at 90 Hz and 120 Hz each do.
  - The one field case is similar but not the same. KC2G (CE, 101 Hz from AG7EW and 14 dB weaker) failed gate V at Z_ref about 1. It passed at 8 to 11 once the neighbour, which had not been excluded at all, was left out (README.md:347-357).
- *Gain:*
  - 0 in today's sparse band.
  - In a band packed at 30 Hz with neighbours above about -25 dB, about 1 to 3 dB of noise-floor bias removed (inferred).
  - Up to KC2G's improvement in Z_ref, from 1 to 8-11, when a strong neighbour fills the floor.
- *Cost:* Medium:
  - Exclusions travel as bare centre frequencies through channel_for, ChanCapture and noise_psd (receiver.py:176-198, track.py:168-193). They would need (centre, half-width) pairs.
  - Plus a dense-band test with many L signals 30 Hz apart.
- *Risk / constraints:*
  - At 30 Hz L spacing the gaps are only 5 to 8 Hz in every 30 Hz, so in a dense band the fallback decides the result. A band-wide median is itself biased once more than half the band is occupied (inferred).
  - A narrower guard lets skirt power in: L's skirt measured -60.4 dB at 80 Hz against a -65 dB target (README.md:893).
  - The fallback change touches CE, so it needs a flag.

**R3. Remember stations' audio frequencies and offer them as A2-only hints**
- *What:* Keep a table of recently heard stations (callsign, dial, audio frequency, waveform, last heard). Offer their frequencies as hints in every slot, marked "A2-only" so that a hint that fails A2 is simply dropped instead of being gated. Today hints come only from earlier tiles and from store.due, which covers only missing later segments (store.py:393-417). A repeat of a segment already held, the most common multi-pass case, is never hinted.
- *Why we think so:*
  - At the true pilot, A2-L passes 8 of 8 at -25, -28 and -31 dB. The joint live chain gets 6 of 8 at -24 dB, 2 of 8 at -28 dB and 0 of 8 at -31 dB, "limited by A1, not A2" (wfB_parts.txt ACQ section).
  - The CE carrier ranks first in A1 down to -31 dB (opt/docs-spec/a1_rank.py), so CE gains little.
  - Against the idea: spec 10 (qrsstvae-options.md:662) tells senders to change frequency on every pass. The app's scheduler, however, keeps one freq_hz per entry (native/gui/qrss_schedule.hpp:80).
- *Gain:* Inferred:
  - Live L tiles: about +5 dB (from about -26 dB to about -31 dB) for repeat stations that keep their frequency within a session.
  - Stored L passes: about 1.5 to 2 dB, because the finish's A3 already finds L_FULL to about -31 dB (measured this round).
  - About 0 for CE, for senders following spec 10, and across days, when temperature drift of 1 Hz to a few Hz per pass (spec 2.6) moves the station outside the 2 Hz hint span.
  - It largely overlaps what F1 and L1 give without any memory.
- *Cost:* Small:
  - A JSON table beside the store, and an A2-only flag that also keeps failed hints out of the finish's gate queue (acquire.py:1648-1651; see R9).
  - A2 measured 81 to 101 ms per line on shared CPUs (opt/verify_feas/m2_a2.py), so 20 stations cost about 2 to 6 s per acquisition.
  - A cross-pass A1 sum would give under 0.5 dB per doubling of passes, because the line moves between passes (inferred).
- *Risk / constraints:*
  - Used as gate hints rather than A2-only, stale frequencies would eat the 180 s finish budget.
  - CE-only listeners get new detections, so CE needs a flag.
  - Route the open question in spec 6.5 (whether senders should keep one frequency) to the spec owner.

**R9. At the finish, keep hints that fail A2 out of the gate queue**
- *What:* At the finish, a hint that fails A2 is not dropped. It becomes a blind candidate and is gated before every A3 hit (acquire.py:1403-1428; _joint_detections returns pre + hinted + tbd). Make such hints A2-only, as the live path already does (live.py:1189).
- *Why we think so:*
  - store.due and _hints predict "the same waveform at the same frequency" two slots later (store.py:396-411, live.py:1191-1223). That matches the app's scheduler, but not spec 10's advice to move every pass.
  - On FULL, each refused blind candidate costs 60 to 80 s (waveform-l.md:245) of a 180 s finish budget (live.py:108).
- *Gain:* Inferred, not measured: for senders who move, due hints stop spending the budget that weak A3 hits need.
- *Cost:* A few lines.
- *Risk / constraints:*
  - A real hinted pass whose A2 narrowly fails loses its gate.
  - CE needs a flag.
  - Whether a picture's segments keep one frequency is left open in spec 6.5 (line 523) and belongs to the spec owner.

**R7. Raise the header decoder's list size from 8 to 32**
- *What:* Decode headers with CA-SCL list 32 instead of 8. header.decode already takes list_size (header.py:204-216).
- *Why we think so:* Measured this round on steady BPSK AWGN LLRs with random headers, 150 to 200 trials per point (opt/verify_feas/m1_list.py):
  - At Es/N0 of -11.0, -10.5, -10.0 and -9.5 dB, list 8 decodes 12%, 34%, 67% and 87%, and list 32 decodes 20%, 55%, 77% and 93%.
  - The 50% point moves from about -10.26 dB to -10.57 dB, and the 90% point by about 0.3 dB.
  - Decode time rises from 24 to 26 ms to 33 to 36 ms per attempt, about 1.4 times, not 4 times.
- *Gain:* About +0.3 dB of header threshold on steady paths (measured on synthetic LLRs).
  - It counts for single passes, and for sums of one to three passes near threshold. A 16-pass sum has about 12 dB of margin.
  - On fading the header is limited by tracking (CE with both copies: -30.3 dB steady, -29.0 dB quiet, -26.9 dB moderate, spec 5.1), and C2 is the better lever there.
- *Cost:* One argument.
- *Risk / constraints:*
  - False accepts rise about 4 times per attempt: from the spec's "below once in 10^8 after the field checks" to below about 4 in 10^8 (spec line 382).
  - Attempts grow with the store, since rule 2 decodes once per nearby provisional cluster under the listener lock (associate.py:274-292, live.py:1303). Do O1 first.
  - The spec states list 8, so this goes to the spec owner. CE needs a flag. Transmitters and the C beacon are unaffected.

**R6. Weight each pass's header LLRs by their own consistency in soft-header sums**
- *What:* associate._soft_header sums every cluster member's hdr_llr unweighted, then falls back to pairs (associate.py:276-291). The fix:
  - Scale each member by a measured consistency factor (agreement with the sum's hard decisions, or LLR variance against the Gaussian model), or drop outliers.
  - The stored hdr_llr already combines the header block and the spread copy, so consistency can be judged per pass, not per copy.
  - C4 fits into the same change.
- *Why we think so:*
  - Spec 5.1 says a faded block's mis-scaled LLRs "can spoil a sum" (2 trials in 40 at -26 dB) and should be weighted by their own consistency or dropped.
  - qrss-build/spread-header-review.md lists it.
  - How often the pair fallback already rescues such sums has not been measured (waveform-l.md:166-168).
- *Gain:* Under 0.1 dB of header threshold, for clusters of a few passes only (inferred). A 16-pass sum has about 12 dB of margin.
- *Cost:* Small to medium, plus a fading soft-sum test in the slow tier. No extra decode attempts, so the false-accept rate is unchanged.
- *Risk / constraints:*
  - A consistency estimate from 2,474 noisy bits at -26 dB is itself noisy.
  - CE association outcomes change, so CE needs a flag.

**R5. Make the inverse AGC blind to one strong L signal (low priority)**
- *What:* frontend.normalise divides the band by a 100 ms running median of wideband power (frontend.py:168-208). Above about -6 dB, a strong L signal, which is not constant-envelope, sets that median. The fix:
  - Notch only verified strong L lines (above -6 dB, which A1 finds easily on the un-normalised front end) before taking the median.
  - Otherwise leave normalise unchanged.
  - The alternative, a median across frequency bins per frame, would change every CE result to fix an L-only problem.
- *Why we think so:*
  - An l-short genie at +10 dB reads 12.13 dB latent SNR before the AGC and 9.89 dB after it (waveform-l.md:378-382, design.md:943).
  - The cross-modulation was measured this round (opt/verify-gain/m2_agc_xmod.py): 3.95% rms gain modulation at -6 dB, 6.8% at 0 dB, 15.4% at +10 dB and 20.4% at +20 dB. That caps every other signal's SINR at 28.1, 23.4, 16.3 and 13.8 dB.
- *Gain:* About 0 at every operating threshold:
  - At most 0.16 dB for a neighbour at the single-pass good level beside a +10 dB L signal.
  - About 0.3 to 0.5 dB of PSNR for a very strong L signal itself (inferred).
- *Cost:* Medium. capture_prepare normalises before acquisition runs (live.py:298-300), so the notch needs a cheap A1 pre-pass.
- *Risk / constraints:*
  - The 100 ms AGC exists for impulsive and fading band noise.
  - With the notch form, CE stays bit-identical whenever no strong L signal is present. Otherwise CE needs a flag.

**R8. Cancel the transmitter's own deterministic distortion on strong passes (parked)**
- *What:* CE's phase-modulator intermodulation and L's clipper distortion are deterministic functions of the transmitted symbols. On a strong pass:
  - Re-synthesise the waveform from the first estimates.
  - Subtract the predicted distortion and demodulate again.
  - Gate all of this on the pass's own SNR.
- *Why we think so:*
  - L's data SINR is 12.2 dB (opt/transmit/m2_peaks.py; spec 2.2). CE's D_PASS of 0.0466 (demod.py:55) sits 13.3 dB below the latents.
  - At the good level (+2.2 dB latent SNR, decoder-measurements.md:45) that distortion costs about 0.46 dB (L) and 0.36 dB (CE) in one pass. At very good (+5.7 dB) it costs about 1.1 dB and 0.85 dB (computed).
  - But at +2.2 dB the latent estimates carry an error of about 0.4 to 0.6 of the signal power. A re-synthesised distortion therefore correlates poorly with the real one.
  - L's clipper is also not sample-exact at 8 kHz.
- *Gain:* Inferred:
  - About 0.1 dB at the good level.
  - At most about 0.5 dB on one very strong pass.
  - 0 from two passes on, where the precoder already averages the distortion out.
- *Cost:* Medium: a re-synthesis and a re-track per strong pass.
- *Risk / constraints:*
  - Below about 0 dB latent SNR, wrong estimates feed back, so it needs an SNR gate.
  - CE needs a flag.
  - Not worth building before the multi-pass work.

## L only

Two facts frame every acquisition item in this section.

- **Stored passes are already found to about -31 dB.** The finish's whole-slot A3 finds them that deep (measured this round: 10 of 10 at -30 dB, 0 of 5 at -32 dB). Acquisition gains are therefore large for live tiles and smaller for stored passes.
- **The CE effect is indirect.** None of these items touches CE's own A1, A2 or A3, or --just-ce. But in joint mode, extra L candidates share the finish budget of 12 candidates and 180 s (live.py:106-108), so in busy slots the default listener's CE output can change.

**L1. An L-only A1 window that reaches back over a lead-in (pairs with F1)**
- *What:* Add a second A1 statistic for L pilot hypotheses only, over a window starting before t0, beside today's A1_SPAN_S of -2.5 s to 22.5 s (acquire.py:160).
  - Its lines are added to today's, never replace them, through an A2-L-only path beside pilot_lines (acquire.py:1630-1632).
  - Start the window about 8.5 s before t0 rather than 10.5 s. Spec 2.8 (line 311) says key-up drift happens early in the lead-in, and that receivers should take the starting state from its last 8 s.
  - capture_read already reads 15 s before t0 (live.py:276-284), so the audio is there.
- *Why we think so:* Two measurements, where "found" means an A1 line within 0.5 Hz of the pilot.
  - **l-short, 8 seeds** (opt/buildrec/m_a1lead.py):
    - With no lead-in and today's window, 3 of 8 at -26 dB and 0 of 8 at -28 dB.
    - With an 8 s lead-in and today's window, 8 of 8 at -26 dB and 3 of 8 at -28 dB.
    - With the new window, 8 of 8 at -28 dB, 6 of 8 at -30 dB and 4 of 8 at -32 dB.
  - **l-tiny, 12 seeds** (opt/transmit/m3_leadin_a1.py):
    - With no lead-in, 8, 5 and 0 of 12 at -26, -28 and -30 dB.
    - With a 10 s lead-in and today's window, 12, 7 and 5 of 12, then 0 of 12 at -32 dB.
    - With the new window, 12, 11 and 11 of 12 at -28, -30 and -32 dB.
  - The pilot runs at gain 1.0 in the lead-in, against 0.656 in the preamble. Both measurements used a lead-in tone that did not drift.
- *Gain:*
  - A1's 50% point moves about 5 dB (measured, with a clean tone).
  - Live L reach goes from about -26/-27 dB to about -32/-33 dB, where A2-L (8 of 8 at -31 dB, wfB_parts.txt:342) and gate V take over as the limit.
  - Stored passes gain about 1.5 to 2 dB (inferred), since A3 already finds them to about -31 dB.
  - Less on rigs that chirp at key-up. It pays only together with F1.
- *Cost:* Small: one more carrier_line_stats over about 31 s for L, plus A2-L on up to 16 new lines. At 30 to 80 ms per line (the higher figure measured on shared CPUs this round), that is 0.5 to 1.3 s a slot.
- *Risk / constraints:*
  - The longer window dilutes A1 for passes with no lead-in, which is why it adds lines rather than replacing them.
  - Hold the false-alarm rate at 1e-4 per test.
  - design.md:605 keeps the lead-in out of A1 so that it cannot move the preamble timing. That reason does not apply to a window that yields only frequency and drift.

**L2. An L known-waveform template search and retro detection**
- *What:* em.template_search and em.retro_detect raise an error for L (em.py:232-235). Build the L versions:
  - A linearised L template: 0.881 times the paired x_hat through lwave, plus the pilot.
  - It is built from a header known from a pass or from the store, and searched over timing and drift at the station's pilot frequency.
  - Live wiring is a separate job (O12). live.py does not call these searches even for CE.
- *Why we think so:*
  - Spec 6.5: known-waveform search reaches -40, -37 and -35 dB, against -34, -32 and -30 dB for whole-slot detection.
  - On L_FULL, the band's shape statistic z1 along the true path is still 21.5, 13.3 and 5.0 at -28, -30 and -34 dB (design.md:933). The signal is still there where A3 stops.
  - For CE the search has been demonstrated: a -40 dB quiet FULL pass found against a +5 dB accumulator, Z 7.7, one seed (tests/test_qrss_em.py:625-636).
- *Gain:* Inferred:
  - The template captures W/(1+W) of the data power, and L puts about 80% of its power in data.
  - With the weak accumulator of the 16-pass usable point (about -3.6 dB), L's template holds about 0.44 of the pass power. The per-pass floor then moves from blind A3's -31 dB to about -36/-37 dB on quiet paths, a gain of 5 to 6 dB.
  - About +9 dB only once the accumulator nears +5 dB.
  - That is what L's deep thresholds would need: -34.3 dB usable and -37.7 dB recognizable at 16 passes (spec model figures).
- *Cost:* Large, weeks:
  - The template is not sample-exact, because the clipper works on the whole frame.
  - It needs a FULL frame, its own false-accept calibration and Gumbel table, and a correction for searching many timings and drifts.
  - L round A on FULL takes 7.25 s.
- *Risk / constraints:*
  - False accepts at depth, and CPU on consumer hardware.
  - It needs some passes found blind first, to build the accumulator it searches with.
  - Measure L's gate V at depth first (D12).
  - CE unchanged.

**L5. EM for L: port rereceive as a header-known re-track first; measure the data prior at depth before building it**
- *What:* em.py skips every L member: "An L pass is not re-received (EM is CE-only)" (em.py:283-288).
  - Port rereceive for header-less L passes, using the accumulated header, which makes them round-B passes.
  - make_classes_l(spec, hdr_bits, prior) already exists (ltrack.py:242-267). L also needs a frequency path from refine_path_l, since pass_path is CE-only (em.py:250).
  - Do not build the data-aided prior until a measurement at -28 dB with CE's block split shows that it pays.
  - The listener never runs EM (live.py has no em import), so pair this with R10 to reach the field.
- *Why we think so:*
  - Round B was measured, as in R10.
  - The data prior (opt/live-ops/m2_l_em.py: l-short, moderate fading, two seeds, baseline already round B) moved the accumulated SNR by at most +0.38 dB at -12 dB and about ±0.2 dB at -20 dB. With CE's block split it gave +0.05 to +0.19 dB. Without the split, a pass's own SNR rises by a false 1 to 2 dB.
  - CE agrees. README.md:883 gives +0.15 dB on a quiet path, against the spec's +0.5 dB target. The build's CE run with `--em 1` gained +0.09 dB on the accumulator (lrx/scratch_c_intrx/out_e2e.txt:60-61), and e2e-results.md records 229 s of CPU for it.
  - Against these stand two models: spec 8 (L's tracking reference rising from 20% to 66% of the power, +5.2 dB), and the transmit reader's model (0.7 to 1.9 dB at 16 to 64 passes).
- *Gain:*
  - Header-known re-track: 0.1 to 0.5 dB per header-less L pass at -24 and -28 dB (measured, slope-corrected).
  - Data prior: at most +0.38 dB where measured; unknown at -28 dB.
- *Cost:*
  - The rereceive dispatch is medium, a few days, with one track_l per pass (about 7 s on L_FULL round A).
  - The data prior would take weeks.
- *Risk / constraints:*
  - Without the block split, a pass's own SNR shows a false gain.
  - In mixed CE+L accumulators, better L members change the prior that CE members are re-received against, so CE results move there. CE-only stores are untouched.

**L3. An L-matched A3 emission (pilot plus the one-sided data band)**
- *What:* A3's Viterbi emission is a single-bin line statistic. Add an L hypothesis whose emission sums the pilot bin with the data band's energy, from 22.07 Hz to 3.1 Hz below the pilot. It needs its own Gumbel table by number of frames. It is the A3 counterpart of L6(a).
- *Why we think so:*
  - The build left it open: "A3's pilot-only emission is about 6 dB behind CE's" (wfB_parts.txt:395).
  - Measured this round (opt/verify-gain/m1_a3_lfull.py): A3 finds L_FULL in 10 of 10 trials at -30 dB (steady and quiet) and in 0 of 5 at -32 dB steady.
  - At -30 dB its best path already ran 3.3 to 16.1 Hz below the pilot in 5 of 5 trials, so A3 is partly a data-band detector today.
  - z1 along the true path is 5.0 at -34 dB (design.md:933), but a searched path pays a penalty for searching over many paths and channels.
- *Gain:* About 2 to 2.5 dB on white noise, from about -31 dB to about -33/-33.5 dB. Less on a busy band (inferred).
- *Cost:* Medium: a new emission and Gumbel table. A3 costs about 2 s a slot today, and that roughly doubles.
- *Risk / constraints:*
  - A strong CE's upper sideband reads L-shaped (z1 from +112.8 to +593, lrx/scratch_chain/all1.txt and extra.txt), so this needs O2's own-band rule.
  - On a real band, any narrow signal responds to an energy emission.
  - Each false candidate costs a 60 to 80 s gate V, so it needs a false-alarm calibration.
  - CE unchanged.

**L11. Loosen A3's threshold for L-shaped hits only**
- *What:* Raise TBD_PFA (acquire.py:203) from 1e-3 to about 1e-1 for A3 hits whose shape statistic says L (z1 above 4). Leave 1e-3 for all others.
- *Why we think so:*
  - A3's cliff is steep. At -30 dB steady its z was 52.3 to 54.6, against a threshold near 51.4 (opt/verify-gain/m1_a3_lfull.py).
  - The shape test is N(0,1) on noise along A3 paths (z1 mean 0.03, sd 1.00, wfB_parts.txt:348-353), so z1 above 4 happens by chance about 3e-5 of the time.
  - z1 along the true path scales with SNR (21.5, 13.3 and 5.0 at -28, -30 and -34 dB), so it would be about 8 at -32 dB (inferred).
- *Gain:* About 0.5 to 1 dB of blind L reach at the finish (inferred). Measure first, because at -32 dB the Viterbi path may no longer lie on the signal.
- *Cost:* A few lines, plus a false-alarm check.
- *Risk / constraints:*
  - CE sidebands also read L-shaped, so this needs O2.
  - Each false L candidate costs a 60 to 80 s gate V.
  - CE is unchanged except through the shared finish budget.

**L9. Sweep refine_path_l's fine-pass thresholds for L's weaker pilot**
- *What:* refine_path_l reuses CE's fine-path constants: 20 s segments, a 40 times line-to-floor ratio and a 0.7 share (track.py:144-147; ltrack.py:1146). L's pilot is 5.6 dB weaker than CE's carrier, so the fine 5 s pass turns off between -12 and -16 dB. Below that, a drifting rig is followed only by the coarse path. The plan:
  - Sweep the constants on warm-up and wander channels at -16 to -26 dB.
  - Scale the segment length (40 to 80 s) or the ratio with pilot power, or add the data band's energy to the line statistic.
- *Why we think so:*
  - The thresholds were "not swept" (wfB_parts.txt:115, 182).
  - Real carriers wander. KC2G 04:15Z wandered 0.22 Hz rms within 60 s and 0.6 to 1.25 Hz over the pass (opt/rxcode/m1_kc2g_freq.py).
  - There is no GPSDO anywhere in the project.
- *Gain:* Unmeasured, but bounded by spec 2.6's wander table:
  - 0.2 Hz rms of wander costs L 0.2 to 0.9 dB at the good level in one pass, and 1.0 to 1.9 dB at the usable level after 16 passes.
  - How much of that a better fine path recovers is unknown.
- *Cost:* Small to medium: a sweep in slow-tier time, then new constants.
- *Risk / constraints:*
  - Lower thresholds let noise lines into the path, and longer segments follow fast wander worse. The coarse fallback guards against both.
  - CE unchanged.

**L4. Re-normalise each L pass's gain scale against its known symbols**
- *What:* After track_l, fit one complex scale of the data-band matched filter against u_hat times the known symbols: the preamble, the references, and the header and spread copy once known. On fading, fit one scale per 60 s window, with shrinkage. Then correct |u_hat| and P before extract_l. The errors-in-variables correction was already tried and overshot on fading (0.81 at -20 dB moderate, wfB_parts.txt:179), so use this fit instead.
- *Why we think so:* The slope k of z against the true latents was measured (opt/buildrec/m_rounds.py, l-short steady):
  - Round A: 0.826, 1.020, 1.486 and 0.993 at -28 dB, and 0.929, 1.003 and 1.376 at -24 dB.
  - Round B: 1.000, 1.037 and 1.232 at -28 dB, and 1.018, 1.038 and 1.172 at -24 dB.
  - CE on the same harness: 0.919 to 1.039.
  - The joint estimator is unbiased when u_hat is correct (demod.py:208-219), so k is the tracker's gain scale. |delta| reads about 9% high at -27 dB (wfB_parts.txt:159).
- *Gain:* Much smaller than raw MSE suggests:
  - The weights w scale as 1/k² while z scales as k, so an over-scaled pass is down-weighted automatically.
  - Combining the measured round-A passes loses about 0.16 dB against ideal, and the round-B passes about 0.04 dB (computed).
  - So the fix is worth about 0.05 to 0.2 dB in multi-pass accumulation, and about 0.05 dB once passes are round B (R10, L5).
- *Cost:* Small: a least-squares fit and a rescale, a few ms a pass. Tests at -24 to -28 dB go in the slow tier.
- *Risk / constraints:*
  - One global scale is wrong on fading, hence the windows.
  - kappa_estimates_l, P and the reference-pull correction must stay consistent with the rescaled u_hat.
  - CE unchanged.

**L6. An L-shaped A1, plus L-only A1 runs later in the slot**
- *What:* Three options:
  - (a) An L-shaped A1 cell that sums the pilot bin with the preamble's known data-band spectrum, or a coarse decimated A2-L. Its best cells are added as L-only lines after CE's 16.
  - (b) A second L-only A1 about 60 s in, combining 25 s blocks non-coherently over the data section.
  - (c) A1 at the four 11.64 s callsign windows, which sit at fixed times relative to t0, for late joiners and restarted listeners.
- *Why we think so:*
  - waveform-l.md:360-366: "A1 sees only the pilot, 10.5% of the preamble's power ... The fix is estimated at 2 to 3 dB."
  - The pilot is 13.8% of the data section's power. In the callsign windows it runs at the data's average power, but it is shifted 16.495 Hz down during Morse key-down units (spec 2.7).
  - KC2G's carrier wanders 0.22 Hz rms over 60 s, so blocks must be combined non-coherently.
- *Gain:* Inferred:
  - (a) +2 to 3 dB live, about 1.5 to 2 dB for stored passes. Redundant with F1 + L1 for senders who send a lead-in.
  - (b) About +3 dB live, but later in the slot. About 0 for stored passes, since the finish's A3 already searches the data section.
  - (c) Helps stored passes only when less than half the slot was heard (live.py:345-357), that is for late joins and restarts. How often that happens should decide whether it is worth building.
- *Cost:* (a) medium, a few days. (b) and (c) small to moderate.
- *Risk / constraints:*
  - More A1 cells mean more false lines.
  - The (a) template ties A1 to the L preamble, so it must change if the preamble does.
  - CE is unchanged except through the shared finish budget.

**L7. A longer A1 list for L pilot hypotheses (live only)**
- *What:* Two changes:
  - (a) Run A2-L, and nothing else, on the next 16 to 48 A1 local maxima, whatever their level.
  - (b) Before cutting at 16 lines, drop lines inside a stronger line's own L band. The existing drop_band_owned rule (acquire.py:1498-1519) does this today only after A2; move it ahead of the cut.
- *Why we think so:*
  - carrier_lines stops at the 1e-4 CFAR threshold (acquire.py:384-421).
  - Measured on AWGN with 20 seeds per SNR (opt/docs-spec/a1_rank2.py), at -24, -25, -26, -27, -28 and -29 dB, the pilot is:
    - in the top 16 in 15, 12, 11, 10, 8 and 5 of 20;
    - in the top 64 in 16, 15, 13, 11, 10 and 7 of 20;
    - found today (top 16 and over threshold) in 15, 11, 11, 10, 8 and 1 of 20.
  - At -31 dB the pilot ranks 96th to 98th.
  - One L signal at -6 or -12 dB takes two lines (lrx/scratch_chain/all1.txt).
- *Gain:*
  - Live, measured: at -29 dB detection goes from 1 of 20 to 5 to 7 of 20, about +1 dB at the 25 to 35% detection point. 64 lines move the 50% point only 0.5 to 1 dB.
  - Stored passes: about 0, since the finish's A3 finds them at -30 dB.
- *Cost:* Small: about 1.4 s a refresh for 48 lines at 30 ms each. More on shared CPUs, where A2-L measured 81 ms a line. About 0.5 s with O15.
- *Risk / constraints:*
  - About 0.1 extra A2-L false passes per refresh (inferred), each costing a gate V.
  - Redundant once F1 + L1 or L6(a) exist.
  - CE's 16 lines must not be displaced.

**L8. Make L's weights honest, and recover SINR, on fading paths**
- *What:* Three effects the L noise model misses on fading:
  - (a) The channel gain changes within the ±8 T_L (±0.49 s) pulse. Equalise at channel rate before the matched filter when SNR is high, or add a Doppler-dependent term to s2.
  - (b) The pilot-to-data ratio delta wanders on 2 ms two-path channels. Model it as a slow second state, or fit it in windows, holding it at 1 inside gate V.
  - (c) Estimate kappa per segment (about 60 s) from the residuals on known symbols.
- *Why we think so:*
  - A genie with P = 0 on moderate fading showed data residuals 1.23 to 1.36 times the predicted noise (wfB_parts.txt:304).
  - On mps (0.15 Hz, 2 ms; channel.py:176-177) the corrected reference reads 1.1 to 1.8, and W comes out up to 22% too small (wfB:303).
  - The chain on moderate fading at -12 dB gave L latent SNRs of 2.82 dB and 1.74 dB, against genies at 4.19 dB and 3.35 dB (lrx/scratch_chain/extra.txt).
  - With a delayed path, the gain at the data centre differs from the pilot's by 0.3 to 1.9 dB (lrx/DESIGN.md section 2.2).
- *Gain:* Inferred:
  - (a) 0.9 to 1.3 dB of SINR at -12 dB on moderate fading in a single pass; about 0 deep in multi-pass.
  - (b) and (c) under 0.05 dB, since weight errors are second order when passes are combined. The exception is CE-against-L weights in mixed accumulators.
- *Cost:* Medium for each, with slow-tier genie tests.
- *Risk / constraints:*
  - Equalising with a noisy u_hat amplifies noise.
  - A free delta weakens gate V.
  - Per-segment kappa is noisier on SHORT frames.
  - CE unchanged.

**L10. L calibration tidy-up: preamble class, ramps, preamble in kappa, per-section D_REF**
- *What:* Four small items:
  - (1) Give the preamble its own class instead of G_REF_L and D_REF_L in make_classes_l and genie_track_l. G_PRE_L (0.995) and D_PRE_L (0.0200) already exist (ldemod.py:89, 129).
  - (2) Mask about 5 symbols at each callsign-window and lead-in ramp, where the pilot template reads up to 27% low, or model the ramp. The PILOT_REACH masking already exists.
  - (3) Add the 330 preamble symbols to calibrate_l's reference estimate.
  - (4) Use a D_REF per section.
- *Why we think so:*
  - On the ramps, the pilot gain reads 0.87 to 1.00 against a model of 0.70 to 0.85 (wfB_parts.txt:256-258).
  - genie_track_l puts u 0.3 to 1% below the references, biasing the data estimate by +0.1 to +0.16 at -12 dB.
  - Header-section references distort by 0.019 to 0.021, against 0.023 to 0.025 in the data section. D_REF_L is 0.0236 against a genie value of 0.0196 (wfB:305-307).
  - The preamble adds 19% more reference symbols on L_FULL and 2.5 times as many on L_SHORT.
- *Gain:* Each well under 0.1 dB (inferred). Kappa's standard deviation falls about 8% on L_FULL and 37% on L_SHORT (inferred from 1/sqrt(n)).
- *Cost:* Small: a few lines and a test each.
- *Risk / constraints:*
  - Low risk.
  - The preamble's gain must be modelled per class, or it biases kappa.
  - The preamble-in-kappa change could reach CE behind a flag.

## CE only

**C2. Port TRK's two refine_timing fixes to CE's tracker behind a flag, and A/B them on FULL**
- *What:* track.refine_timing has two flaws:
  - Its likelihood includes a sum|m(theta)|² term that is noise, not evidence about theta (track.py:1183-1220).
  - Its Newton loop gives up on the first Hessian that is not positive definite (track.py:1257).
  - The L tracker's version, refine_timing_l, fixed both: a correlation-only likelihood, and gradient steps of up to 1.5 samples kept only when they improve the fit (ltrack.py:699-716).
  - Port both under a flag and A/B them on FULL at CE's deep operating points (-30 to -36 dB, quiet and moderate). Also measure CE's coarse timing error there.
- *Why we think so:*
  - **CE, measured this round.** On CE SHORT, steady, -27 dB, 9 seeds, with tracks started ±0.15 T off (opt/verify-gain/m3_ce_timing.py):
    - refine_timing corrected 0 of 18 starts.
    - 14 ended exactly at 0.150 T, and 4 walked further off, to 0.20 to 0.28 T rms.
    - The mean latent-SNR loss against the true-timing track was 0.46 dB (worst 1.47 dB). Started at the true timing, it stayed there (2 of 2).
  - **L, from the build records.** The fixes took l-short at -27 dB from 0.105 T rms and 0.33 dB mean loss (worst 1.71 dB) to 0.040 T and 0.13 dB (worst 0.28 dB). On moderate fading at -20 dB they took the timing error from 0.121 T to 0.017 T (wfB_parts.txt:152-158).
  - **Fading.** Spec 5.1: on moderate fading at -28 dB, gate V fails to fit the timing on 14 of 16 passes.
- *Gain:*
  - On SHORT at -27 dB, a mean loss of 0.46 dB (1.5 dB worst) is there to recover from a 0.15 T start (measured).
  - On FULL, probably 0.1 to 0.4 dB at -30 to -36 dB, since FULL's Hessian is about 2.5 times better conditioned (inferred).
  - On fading it is a better-supported CE header lever than R7.
- *Cost:* Small to medium. The code exists in ltrack. The A/B is several runs of under 5 minutes each, or 12 to 40 paired FULL passes offline.
- *Risk / constraints:*
  - It changes CE timing and therefore every CE number, so it needs a flag or Jeff's OK.
  - Check that CE's phase model does not pull the timing.

**C1. Offer the D3 residual-ratio re-check on the CE-only path (--just-ce), behind a flag**
- *What:* In joint mode, the receiver re-checks every CE gate-V accept against R, the ratio of the actual residual on known symbols to the predicted one. It refuses L signals that CE's gate would otherwise pass (receiver.py:600-618: refuse when R exceeds 2.0 and Z_ref/sqrt(R) is at most Z_ACCEPT).
  - The CE-only detect path (receiver.py:494-532) has no such check, so under --just-ce an L signal becomes a junk CE pass.
  - Add the check behind a flag, or as a first step just mark such passes suspect.
  - With O17 it costs nothing extra.
- *Why we think so:*
  - Junk under --just-ce (wfC_parts.txt:301, 437, 545):
    - l-short at -12 dB gives Z_ref 6.41 (R 4.26).
    - l-tiny at -6 dB gives Z 9.2 to 12.6 (R 11.2 to 14.0).
    - Live tiles appear at z 7.2 to 7.3 with latent SNR of -8 dB.
    - One suspect WAV pass appears at Z_ref 6.5.
  - Real CE, by contrast, gives R 0.93 to 1.48 on TINY and 0.55 to 1.31 on fading TINY. On SHORT, one strong disturbed pass reached 2.16 at Z 48 and was kept because Z/sqrt(R) exceeded 6 (wfC:377-380; lrx/scratch_c_intrx/fade2.txt, fade3.txt).
- *Gain:*
  - Measured in joint mode: every junk CE pass from an L signal is refused.
  - The false-reject rate for a weak, fading CE pass near Z 8.5 is unmeasured, since the only CE pass seen above R 2 was strong.
- *Cost:* Small: about 10 lines and a flag, and about 0.09 s per CE accept on TINY without O17 (wfC:439). It is mainly a policy decision for Jeff.
- *Risk / constraints:*
  - It helps only updated stations that run --just-ce. Fielded pre-L listeners will misreport L whatever is built (see O7).
  - With the flag off by default, CE stays bit-identical.

**C3. Give CE hints a second A2 try at A1's best nearby cell**
- *What:* When A1's best cell within ±2 Hz of a hint is under threshold, hint_line discards it and returns the bare hint frequency (acquire.py:424-438).
  - A CE hint then gets one A2 at that frequency, while an L hint tries the frequency and ±1 Hz (L_HINT_OFFSETS_HZ, acquire.py:189).
  - Add the below-threshold best cell as an extra A2 line beside the bare frequency, for both waveforms: the CE-only acquire (acquire.py:1569-1581) and the joint one (1636-1651).
- *Why we think so:*
  - A2's frequency grid spans ±0.5 Hz (acquire.py:170). With 2 s chunks, a residual more than about 0.75 Hz beyond the grid's edge nulls even the shortest scale, so a 1 Hz hint error loses A2 outright.
  - KC2G's stored f_hz, which hints hand to the next pass, was 0.62 Hz off on one pass and 0.013 Hz off on the other (opt/rxcode/m1_kc2g_freq.py).
  - The CE carrier ranks first in A1 across the band down to -31 dB, so the nearby best cell is reliable.
- *Gain:* Niche:
  - It applies only to hinted CE passes below about -31 dB, where A1 finds no line, with a hint error of 0.5 to 1.5 Hz.
  - CE's gate V is near its limit there: at quiet -34 dB, Z_ref is about 4 at the true timing (README.md:866-869).
  - Probably under 1 dB of extra reach in that window (inferred). R2 reduces the hint error itself.
- *Cost:* A few lines, and one or two extra A2 runs (about 0.1 s each) per hint with no A1 line.
- *Risk / constraints:*
  - A few more cells at A2's false-alarm rate of 1e-7 per cell, which is negligible.
  - Hinted acquisition changes, so CE needs a flag.

**C4. Scale CE's header LLRs honestly (lowest priority)**
- *What:* CE's header LLR divides by s2n_h times the scale, plus D_PASS (demod.py:256). The spec measured the result at about 0.85 of honest scale. L uses its own header class (ldemod.py:339).
  - At the header's operating point, s2n_h is about 25 per symbol against a D_PASS of 0.0466 (inferred). The 0.85 therefore comes from the gain term applied to the ±1 symbols, not from D.
  - Fix the gain, behind a flag, bundled with R6.
- *Why we think so:*
  - Spec 5.1: "under-scaled by about 0.85 ... would gain about 0.03 dB".
  - associate.py:280 sums hdr_llr across a cluster whatever the waveform.
- *Gain:* About 0, at most 0.03 dB. A 15% relative weight error in a mixed sum costs under 0.01 dB (inferred).
- *Cost:* Small.
- *Risk / constraints:* Changes CE soft-header outcomes at the margin.

## On-air format proposals

Everything here goes to the spec owner, the "Decoder SNR measurement" thread, as a proposal. Nothing in this section is to be built before that thread agrees. Each entry says whether old listeners keep working.

The L-only items are free only while no L transmission exists in the field. The L transmitter is on the waveform_l branch, and that branch's app already offers L transmit.

**F1. L senders send a lead-in by default (8 to 10 s), and the app passes it, for both waveforms**
- *What:*
  - Make --lead-in default to 8 to 10 s for --waveform L. It is 0.0 today (qrss_transmit.py:53).
  - Add a lead-in setting that the app's transmit_args passes on (native/gui/qrss_tx.cpp:139 never passes it).
  - Expose the same setting for CE, where it only keeps key-up drift out of the preamble.
  - A default is legal under spec 2.8. Making the lead-in a "should" for L is for the spec owner. Pairs with L1.
- *Why we think so:* See L1.
  - With today's A1 window, a lead-in alone takes A1 from 3 of 8 to 8 of 8 at -26 dB, and from 0 of 8 to 3 of 8 at -28 dB (opt/buildrec/m_a1lead.py).
  - On l-tiny it takes A1 from 0 of 12 to 5 of 12 at -30 dB (opt/transmit/m3_leadin_a1.py). With L1's window it reaches 11 of 12 at -32 dB.
  - The pilot runs at gain 1.0 in the lead-in, against 0.656 in the preamble.
- *Gain:*
  - Live: about +2 dB alone, and 5 to 6 dB with L1.
  - Stored passes: about 0 alone, since the finish's A3 already starts 10 s before t0 (acquire.py:195) and finds L_FULL to about -31 dB. About 1.5 to 2 dB with L1 (inferred).
  - Measured with a steady lead-in tone. Spec 2.8 exists for rigs that drift at key-up, so expect less on those.
- *Cost:*
  - One default, and one app setting. The setting must be added to the tests/test_native_settings.py fixture where no field holds its default, and to the settings-dialog round trip.
  - Up to 10 s more key-down per pass, 0.56% of a 1782.7 s frame. The WAV already starts 12 s before t0, and the app keys 11 s early (ui-handoff.md:31).
- *Risk / constraints / compatibility:*
  - The lead-in is a steady tone at the data's average power, so drive must be set from the peaks (O20).
  - Fully compatible. The lead-in is already in the format, and track_l handles it whether claimed or not, so today's L listeners decode as before and acquire better.
  - The C beacon sends CE only and already keys early (qrss_beacon_c/qrss_ce.h:32-34).
  - CE results unchanged.

**F2. Decide all of L's format changes in one batch, before L goes on the air from waveform_l**
- *What:*
  - The candidates are F3, F5, F6, and a check of L's density of one reference in 16 symbols. Each is worth about 0.1 to 0.3 dB at most.
  - None justifies a format revision on its own, but together they cost nothing now.
  - Put the survivors to the spec owner as one batch, after D1, and mark L's format version if any land.
- *Why we think so:*
  - L exists only in the Python reference (lwave.py), and the C beacon is CE-only (qrss_beacon_c/qrss_ce.h).
  - But the L transmitter (d357ec8) is on the waveform_l branch, pushed 2026-10-11, whose app already offers L transmit (native/gui/qrss_tx.cpp:33-34, 155-157). The free window ends the first time someone keys L from it.
  - FORMAT_VERSION 2 is shared by both waveforms, but L headers accept only version 2 (receiver.py:110). An L-only version bump is therefore cheap on the receiver side.
- *Gain:* Inferred: makes a few tenths of a dB of L-only changes available at no compatibility cost. After release, each would need a version bump and support for two versions, as CE's v1 to v2 change did.
- *Cost:* One spec review, and one re-pin of L's receiver constants.
- *Risk / constraints / compatibility:*
  - Batch only the items that survive. F4 is redundant with F1 + L1, and F5's harder clip loses for heat-limited stations.
  - Nothing fielded breaks if this is done before L ships beyond this branch.
  - Needs the spec owner.

**F3. An L-only spread-copy rho, chosen after measuring L's header thresholds**
- *What:* L makes room for the spread header copy by scaling every latent by 1/sqrt(1+rho). At rho 0.054 that is a permanent 0.23 dB. CE's copy costs its latents nothing once the header is known.
  - If L's header turns out several dB deeper than its -27 dB estimate, a smaller L-only rho (for example 0.027) could still leave the header deeper than L's acquisition limit.
  - Measure first (D1), then choose: keep rho, halve it, or carry the copy on top and re-clip.
- *Why we think so:*
  - The l_spread.py docstring gives the -0.23 dB. qrss-build/l-spread-header.md says the copy's gain on L is still inferred from CE.
  - CE's estimate was -23.5 dB. Measured on steady paths, it decodes at -28.7 dB from the block alone, -30.3 dB with both copies and -26.9 dB from the copy alone (spec 5 and 5.1).
  - Spec line 384 calls L's estimate "probably pessimistic too".
  - L's header symbols pass the clipper at gain 1.006, against 0.889 for data (opt/transmit/m2_peaks.py).
- *Gain:*
  - +0.11 dB on every L latent at rho 0.027 (+0.23 dB at rho 0), computed by scaling CE's figures.
  - The cost is about 0.7 dB of combined-header depth, and 3 dB for a decode from the copy alone.
  - On fading the copy alone is only 0.3 to 0.7 dB short of the block (spec 5.1). Its value in rescuing passes whose 80 s header block was lost is therefore real.
- *Cost:* The code is mostly there. hdr_rho is a per-frame attribute (lframe.py:92-105), and ldemod.d_pass_x uses it (ldemod.py:137). But D_PASS_LX hard-codes the shared SPREAD_RHO (ldemod.py:132), which is exactly the silent mis-weighting hazard below; remove it.
- *Risk / constraints / compatibility:*
  - Copies combine across passes and waveforms by latent index, so any code that assumes a single SPREAD_RHO mis-weights silently.
  - Free now: there are no fielded L listeners, and the C beacon is CE-only. A format break once L is fielded.
  - Needs the spec owner.

**F5. Retune L's clipper: a fourth pass and/or a lower clip threshold (station-dependent)**
- *What:* Two options:
  - (a) A fourth clip pass: L_CLIP_OVERSHOOT from (1, 1.5, 2) to (1, 1.5, 2, 2) (constants.py:69-71).
  - (b) A clip threshold of -1 or -2 dB instead of 0 dB.
  - Re-run clipstudy on real v5 latents and score both the one-pass good point and the 16-pass point.
- *Why we think so:*
  - (a) was measured (opt/transmit/m2_peaks.py, L_FULL, Gaussian latents, two seeds):
    - 99.99% PAPR from 3.15 to 3.03 dB.
    - Pass peak from 3.20-3.24 dB to 3.06-3.08 dB.
    - Data SINR from 12.24 to 12.12 dB.
    - Average power from 0.631 to 0.624.
    - Pilot share unchanged at 0.139.
  - (b) comes from the spec 2.2 table. At 0 dB: PAPR 3.2 dB, SINR 12.3 dB, and +1.6 dB of useful data power at fixed PEP. At -2 dB: 2.7 dB, 10.6 dB and +1.9 dB.
  - Clipper distortion averages down across passes (spec 2.2, lines 117-119).
  - Real v5 latents measured only 0.1 dB below Gaussian (3.15 dB PAPR, 12.2 dB SINR).
- *Gain:* The sign depends on the station:
  - **PEP-limited station:** (a) about +0.1 dB; (b) about +0.3 dB at deep multi-pass and about 0 at one pass.
  - **Heat- or duty-limited station:** -0.1 to -0.4 dB. waveform-l.md names this as L's main use (an IC-7100 run at 25 to 50 W).
- *Cost:* Constants, plus re-pinning G_DATA_L (0.881), D_PASS_L, PILOT_GAIN_*, L_AVG_POWER and A_ID, and updating the spec 2.2 table.
- *Risk / constraints / compatibility:*
  - Separate transmit levels (O4) change which limit binds.
  - An L format change: free before L is fielded. Afterwards, old L receivers would mis-scale latents by about 1%.
  - Needs the spec owner.

**F6. Send the 2,200 never-sent latents in L's layout**
- *What:* Spec 12 lists sending the latents that are never transmitted today as a possible extension. For L, which is not yet fielded, it costs no compatibility. It does not fit as is, and has to be paid for from the references, from the spread copy (F3), or from a slightly faster symbol rate.
- *Why we think so:*
  - decoder-measurements.md:84-101: the never-sent latents are worth +0.13 to +0.37 dB of PSNR gross, and "half a pass at +3 dB is worth a whole pass at -3.6 dB".
  - Only the data section grows: 1,100 QPSK symbols plus about 73 references, about 1,173 symbols or 71 s. That gives a frame of about 1,854 s against 1,800 s, with only 18 s of slack (spec line 296).
  - Spec 12 itself sizes the change as +4.3% width (22.9 Hz), that is, a faster symbol.
- *Gain:*
  - About +0.1 dB PSNR net at the single-pass good level: about +0.3 dB SNR-equivalent, less 0.18 dB of energy per latent.
  - About 0 at usable and below.
  - Before counting the cost of making room (inferred from measured decoder figures).
- *Cost:* A spec revision plus changes to the frame layout, transmitter and receiver: medium.
- *Risk / constraints / compatibility:*
  - The two waveforms' layouts diverge, which complicates mixed accumulators.
  - Free for L now. It must not be applied to CE without a version bump.

**F4. Raise or even out the pilot inside L's preamble (not recommended over F1 + L1)**
- *What:* Constant-amplitude preamble QPSK drives the clipper hard, so the preamble's pilot comes out at gain 0.656 (10.5% of the power), against 0.708 in the data. Two options:
  - (a) Raise the pilot's share of the preamble to about 30%.
  - (b) Scale the pilot per section before the clipper, to match the data's level.
- *Why we think so:*
  - Section pilot gains (opt/transmit/m2_peaks.py): preamble 0.656, header 0.667, data 0.708, lead-in and callsign windows 1.0.
  - (a) moves A1 by about +4.6 dB and A2-L by about -1.1 dB (computed, assuming A1 scales dB for dB with line power, which is inferred).
- *Gain:*
  - (a) is a net loss at depth compared with F1 + L1. Once A1 is fixed, A2-L becomes the binding stage, and (a) costs it 1.1 dB.
  - (b) gives about +0.66 dB of live A1 depth for passes with no lead-in, and about 0 for stored passes.
- *Cost:* A spec change; changes to the transmitter, the preamble ramps and A2-L's normaliser; and re-pinning ldemod's PILOT_GAIN_* values.
- *Risk / constraints / compatibility:*
  - One verifier would drop this as redundant.
  - Free now for L, but needs the spec owner.
  - Kept only as the fallback if senders will not send a lead-in.

**F7. CE format v3 candidate: thin the data references to 1 in 48 and send the never-sent latents (lowest priority)**
- *What:* CE's tracking reference is almost all carrier: 52.7% of the power in the carrier plus 2.1% in references (spec 6.6).
  - Thinning references from 1 in 16 (constants.py:38) to 1 in 48 frees 4.17% of data slots, against the 4.08% the never-sent latents need.
  - Hold it for a future v3, decided with Jeff and the spec owner.
- *Why we think so:* The slot arithmetic above, and decoder-measurements.md:84-91 for the latents' value.
- *Gain:*
  - About +0.1 to +0.15 dB PSNR at the single-pass good level.
  - 0 at usable and below, where CE's deep multi-pass work happens.
  - The tracking reference loses 0.11 dB.
- *Cost:* A v3 format: receivers must support v2 and v3, the C beacon must be rebuilt, and the golden vectors and spec text change.
- *Risk / constraints / compatibility:*
  - Breaks compatibility. v2 listeners refuse a v3 header (safe, but no decode), and the C beacon must be rebuilt.
  - Each pass's kappa becomes about sqrt(3) times noisier.
  - CE has been on the air in v2 since 2026-10-10.

**F8. Future: a QRSSTVAE-trained codec through L's clipper and CE's phase modulator**
- *What:* Fine-tune an encoder through L's clipper and CE's phase modulator, as SSTVAE's v5 was trained through its own OFDM clipper. This is the allowed future item; v5 stays as-is for now.
- *Why we think so:*
  - SSTVAE reaches 15.2 dB SINR at 3.4 dB PAPR (+1 dB headroom) because its encoder was trained through its clipper, and "that does not carry over" (spec 2.2, line 115).
  - At the same +1 dB headroom, L's table gives 13.3 dB, a like-for-like gap of about 1.9 dB.
  - With the precoder and scrambler, L's distortion is already noise-like (spec 2.4), which makes it a weaker lever for training than in OFDM.
  - SSTVAE's own fine-tune through its clipper measured +0.096 dB PSNR, confounded with 30 extra training epochs (CLAUDE.md, codec v5).
- *Gain:*
  - From the clipper alone: about +0.05 to +0.1 dB PSNR at the single-pass good level, and about 0 from two passes on (computed at W = +2.2 dB).
  - Spec 12 expects any larger gain to come from the single-pass fading loss and from partial-pass decoding, unquantified.
- *Cost:* Large: a stage-2 fine-tune with a differentiable L clipper and CE modulator, new artifacts, and a codec ID change.
- *Risk / constraints / compatibility:* Not compatible. Both ends need the same checkpoint (codec ID d1d8 today), so fielded listeners need the new model, and the C beacon's beacon files must be re-encoded.

## Live listener and operations

Ordered roughly as: the lock and CPU first, then transmit operation, then correctness, then coverage and display.

**O1. Associate incrementally, cache accumulators, and do it outside the listener lock**
- *What:* On every header-decoded pass, resolve_provisional tests every provisional cluster against every open accumulator. It reloads each accumulator's npz file each time, inside _finish_apply, under self.lock, which every refresh and write_state also take (associate.py:152-164, 376-392; live.py:1298-1345). Three changes:
  - (a) Re-test only what changed, using a dirty set that cascades. Rule-3 and rule-4 attaches, and expiry, change sums without calling resolve_provisional. Within one resolve, an early attach changes the accumulator that later clusters are tested against.
  - (b) Keep accumulator sums in memory, invalidated on attach and on expiry.
  - (c) Run associate in the finish child or a worker, and take the lock only to update tiles. _identify_provisional also loads accumulators under the lock (live.py:1350-1377).
- *Why we think so:* Measured on a synthetic store of 30 accumulators:
  - The readers measured 14.16 s per header-decoded pass at 59 provisional clusters (opt/live-ops/m3_store_scale.py).
  - The evidence check re-measured 16.31 s at 58 clusters, about 9.4 ms per pair. Within that, loading one accumulator took 23.1 ms, one header decode 14.6 ms, and a weak headerless pass 0.46 s.
  - The store keeps passes for 7 days (store.py:626).
- *Gain:*
  - Measured: today the lock is held for 14 to 16 s per decoded header at 30 accumulators, and the live tiles freeze meanwhile.
  - For a week-long store (about 200 accumulators and 500 clusters), the quadratic bound is about 13 minutes (inferred). That is a worst case, needing many stations or much junk.
  - Incremental resolution makes the cost linear.
- *Cost:* Small to moderate, in associate.py and store.py, plus a test that the old and new resolution agree on a synthetic store.
- *Risk / constraints:*
  - A cache-invalidation bug could correlate against stale sums.
  - CE results are unchanged as long as the equality test holds.
  - Do this before R7.

**O2. Never gate a candidate inside an accepted signal's own band (CE sidebands, L copies)**
- *What:* Every CE signal at -15 dB or stronger produces A3 hits about 11.5 Hz either side of its carrier.
  - The lower hit is gated as CE. The upper hit, which is L-shaped, is gated as L.
  - The carrier itself can be gated again as an "L copy". _detect_joint skips only candidates within MERGE_HZ (5 Hz) of a found centre, and an L copy's centre sits 9.5 Hz away (receiver.py:583-585).
  - The rule: skip any blind or hint candidate that falls inside an accepted detection's occupied band (CE ±20 Hz, which is GHOST_HZ; L from 22 Hz below to 3 Hz above the pilot).
  - Also skip an ambiguous hit's L copy once its CE gate has passed D3, and the reverse.
  - Extend drop_band_owned (acquire.py:1498-1519) and drop_symbol_rate_aliases rather than adding a third geometric rule.
- *Why we think so:* Chain runs on CE SHORT at -12 dB (lrx/scratch_chain/all1.txt, extra.txt):
  - **Lone CE signal.** The CE preamble gate took 0.89 to 0.99 s, a refused CE A3 hit at 1487.95 Hz took 3.8 to 4.9 s, and a refused L hit at 1510.7 Hz took 2.13 s.
  - **With an L neighbour.** A CE hit at 1487.92 Hz took 4.07 to 4.41 s, and the CE carrier tried as L took 2.47 to 2.58 s, out of 8.0 to 8.6 s of gating.
  - **Sideband spacing.** The sidebands were measured at ±11.5 Hz (wfC_parts.txt:379).
  - **On FULL.** A refused blind candidate costs 60 to 80 s (waveform-l.md:246) against the 180 s finish budget.
- *Gain:*
  - Measured on SHORT at -12 dB: gating drops from 4.9-6.8 s to about 1 s for a lone CE signal, and from 8.0-8.6 s to about 1.5 s with an L neighbour. That is most of joint listening's extra cost (12.6 s against 7.7 s with --just-ce, wfC:440).
  - On FULL, inferred: the two or three refused 60 to 80 s gates per strong CE signal stop eating the finish budget.
- *Cost:* Small.
- *Risk / constraints:*
  - A real signal within about 20 Hz of a stronger one is lost, but such a pair overlaps in spectrum anyway.
  - CE passes found in joint mode are unchanged.
  - On the CE-only path it goes behind a flag. There, output differs only when the budget binds.

**O3. Stop each live refresh at the heard horizon instead of tracking the whole frame**
- *What:* A live refresh builds the capture over the whole frame and fills the not-yet-heard future with noise. It runs gate V and round A over all of it, then erases the future afterwards (live.py:1225-1262; receiver.py:277).
  - Instead, mark unheard positions as unmeasured (R = inf, which _kf_forward already handles, track.py:781).
  - End the grid a margin past the heard end.
  - The end-of-slot finish is unchanged.
- *Why we think so:* Measured on one CE FULL signal at -12 dB (opt/live-ops/m1_refresh_horizon.py), in CPU seconds per refresh:
  - At 5% heard: capture 2.5, gate V 8.6, receive_slot 18.7, total 29.8.
  - At 95% heard: capture 6.4, gate V 9.8, receive_slot 13.8, total 30.0.
- *Gain:*
  - Measured: about 30 s of CPU per signal per refresh, whatever the progress.
  - Inferred: about half the live CPU per signal over a FULL slot, roughly 330 s down to 170 s (gate V stops after 300 s).
  - First tiles appear about 10 times sooner, and the gain estimate at the heard edge is cleaner.
- *Cost:* Moderate: a horizon argument through capture_prepare, track.py and ltrack.py, plus a test that tiles at 100% heard are unchanged.
- *Risk / constraints:*
  - Live tile values at partial progress shift slightly; stored passes do not change.
  - Live CE tiles change, so it goes behind a flag.

**O8. Move the Kalman forward and backward passes into compiled code, bit for bit**
- *What:* _kf_forward (track.py:777-836) and kalman_rts's backward loop (track.py:838-885) are pure-Python scalar loops over every tracker position. select_q runs the forward pass for each of 13 Q_GRID values (track.py:141, 891), several times per track. Write the same recursions in C, with a silent fallback to Python.
- *Why we think so:* cProfile on CE SHORT at -12 dB (opt/rxcode/m3_edge_cpu.py):
  - CE-only: _kf_forward took 712 calls and 5.47 s of 19.2 CPU s.
  - Joint: 460 calls and 3.63 s of 13.2 s, about 0.74 µs per position.
  - L SHORT joint: 170 calls and 0.54 s of 5.5 s.
  - The two CE runs did different amounts of work. There is no numba in the venv.
- *Gain:*
  - Measured share: 28 to 30% of CE receive CPU and 10% of L's.
  - Inferred: a loop 50 to 100 times faster takes about 25% off CE receive time, on every refresh and finish.
- *Cost:* About 80 to 150 lines of C, plus an exact-equality test. The app runs the listener from a repo checkout's venv (native/gui/qrss_window.cpp:520-538), not from the five app packages. Build it into the operator's Python environment, or beside native/bindings through tools/build_native.sh.
- *Risk / constraints:*
  - Bit identity is feasible. _kf_forward uses only float and complex adds, float times complex, and math.log. CPython's float times complex equals the C product except for signed zeros, so spell that out, and build with -ffp-contract=off and no fast-math.
  - CE results unchanged, held by the exact-equality test.

**O9. Skip re-gating already-detected signals in refreshes before detect_until_s**
- *What:* Until detect_until_s (300 s), every refresh calls detect on the whole candidate list (live.py:1248-1250), so each known signal runs gate V again. After 300 s, the recheck branch already passes skip_hz=known (live.py:1251-1256). Apply that rule as soon as the candidate cache is final, and re-gate only new candidates.
- *Why we think so:*
  - Candidates are cached as final once 29.5 s has been heard (live_end_s() is 28.5 s; live.py:1185, acquire.py:1537-1540). The refreshes at 145 s and 265 s therefore re-gate the same lines; the 25 s refresh is not redundant.
  - Gate cost per CE signal is 8.6 to 9.8 s (opt/live-ops/m1_refresh_horizon.py). Gate V costs 10.9 s for L and 13.3 s for CE (wfB_parts.txt).
- *Gain:* About 18 to 26 s of CPU per known signal per slot (two refreshes at 9 to 13 s each), on the worker that also serves every other slot.
- *Cost:* Small: about ten lines and a test.
- *Risk / constraints:*
  - A later gate sees more of the preamble and can refine frequency and timing slightly.
  - When less than half the slot was heard, the finish uses these detections directly, so stored CE results can change slightly.
  - Behind a flag.

**O10. Reuse the live detections at the finish when most of the slot was heard**
- *What:* When at least half the slot was heard, finish_receive reruns the whole acquisition and ignores the live detections (live.py:341-344). Every tracked signal is found and gated again, inside the budget of 12 candidates and 180 s.
  - Hand the live detections in as verified, or run a cheap fit=False verify.
  - Spend the budget on other candidates with skip_hz=known, as the branch for less than half heard already does (live.py:345-357).
  - Keep a fresh gate when a detection's Z_ref is near Z_ACCEPT.
- *Why we think so:* Gate V costs 10.9 s for L and 13.3 s for CE (wfB_parts.txt).
- *Gain:* About 11 to 13 s of the finish budget per known signal per slot, which goes to weak candidates instead (inferred).
- *Cost:* Small.
- *Risk / constraints:* A whole-slot gate can give round A a better starting point, so CE results can change slightly. Behind a flag.

**O15. Compute A2's p-value from one gammaincc per scale (fast path, bit-identical)**
- *What:* preamble_p evaluates gammaincc on every frequency-timing cell of all four A2 scales (acquire.py:779-790), but its one live caller uses only the minimum p and where it occurs (acquire.py:852-857).
  - Within a scale, the Gamma shape k = nc // g is fixed (acquire.py:772), so p falls as Lambda rises.
  - The minimum is therefore gammaincc at each scale's Lambda maximum, followed by the same Bonferroni correction for the number of cells searched.
  - Keep preamble_p, which tests use as a full map (tests/test_qrss_acquire.py:199, 314, 385-386; tests/test_qrss_lacquire.py:180). Add the fast path inside preamble_search.
- *Why we think so:* Measured this round on shared CPUs (opt/verify_feas/m2_a2.py):
  - preamble_search takes 101 ms (CE) and 81 ms (L) per line, including channelisation.
  - preamble_p is 46 ms and 35 ms of that, and gammaincc alone 23 ms and 20 ms.
  - The shortcut takes 0.06 to 0.09 ms and returned the same minimum p.
- *Gain:* Measured: 20 to 45 ms per A2 line, about 25 to 45% of each A2 run. That is roughly 1 s per joint acquisition at 16 CE lines plus pilots, and more for L7's extra lines.
- *Cost:* Very small, plus an equality test against the old function.
- *Risk / constraints:*
  - Two tie cases must match. preamble_p clamps p at 1.0 (acquire.py:787-789), so when every cell is clamped, np.argmin returns cell 0 rather than the Lambda peak. And equal floating-point p values can occur at different Lambda.
  - CE results are unchanged once the test holds.

**O17. Reuse gate V's winning track for the D3 check**
- *What:* After track.verify accepts a CE candidate, _gate runs verify_track(fit=False) again only to compute R (receiver.py:609-612). verify had already built that track with the same path and timing (track.py:1360-1406). Return the winning TrackResult and pass it to _ce_resid_ratio.
- *Why we think so:* verify_track(fit=True) runs fit_timing and then track(refine=False, outer=0), so the re-run reproduces the same track by construction (track.py:1575-1603).
- *Gain:*
  - One tracker run saved per CE accept in joint mode: about 0.09 s on TINY (wfC_parts.txt:439), and about 1 s on FULL (inferred).
  - R is unchanged.
  - It makes C1 free.
- *Cost:* About 10 lines, plus an equality test on R.
- *Risk / constraints:* Low. CE results unchanged.

**O16. A data-band matched filter by convolution and interpolation (L first, CE behind a flag)**
- *What:*
  - L's pilot filter is one oaconvolve plus linear interpolation (ltrack.py:161-170).
  - The data-band matched filter (ltrack.data_mf, ltrack.py:192-216), CE's symbol_mf and ce.matched_filter all use a closed-form gather at each position.
  - Build the data matched filter the same way as the pilot filter. It must handle the timing model's ppm and gamma terms, and the fractional trial timings that refine_timing probes.
- *Why we think so:*
  - The pilot filter is exact to about -55 dB, agrees with symbol_mf to -71 dB on signal, and is about 12 times faster: 0.04 s against 0.47 s on l-short (wfB_parts.txt:8).
  - In the profile, symbol_mf is the top item on L SHORT joint (1.47 s of 5.5 CPU s). On CE SHORT, symbol_mf takes 2.88 s and ce.matched_filter 1.88 s of 19.2 s.
- *Gain:* Inferred:
  - About 25% of an L pass, about 1.3 s of 5.5 s.
  - For CE behind a flag, up to about 4 s of 19 s, and more on refused blind candidates.
- *Cost:* About 40 lines for L; more for CE's timing model.
- *Risk / constraints:*
  - The data band is 19 Hz wide, against the pilot's ±4.7 Hz, so the interpolation error is larger. Check that it stays below about -45 dB, and below the clipper and phase-modulator distortion floors (CE's D_REF is 0.009).
  - Not bit-identical, so CE needs a flag, and L tests' pinned numbers may move.

**O18. Replace the end-of-frame whole-frame live refresh with an early result from the finish child**
- *What:* When a frame ends, step() queues one more whole-frame live refresh beside the finish (live.py:1014-1028). It runs on the same CPUs just as the finish child starts. Instead, have the child send its first round-A result for each known detection over the existing pipe.
- *Why we think so:* A refresh at 95% heard costs 30.0 s of CPU per signal (opt/live-ops/m1_refresh_horizon.py).
- *Gain:* About 30 s of CPU per signal per slot, saved at the moment of peak contention. Tiles also show the finish's own numbers sooner (inferred).
- *Cost:* Moderate: a progress message from _FinishProcess.
- *Risk / constraints:* Tiles only; stored passes are unchanged.

**O4. Separate transmit levels for SSTVAE, QRSS CE and QRSS L**
- *What:* The QRSS transmit request reuses SSTVAE's single level (native/gui/tx_panel.cpp:1585), and a plain peak scale sets each pass to it (qrss_tx.cpp:346-352; native/core/tx/engine.cpp:77-85). Add a level per waveform, defaulting to today's value, and show each one's average power as a fraction of its peak.
- *Why we think so:*
  - CE is constant envelope, so its average power equals its peak for 30 minutes of key-down.
  - L's pass peak is 3.20 to 3.24 dB above its data average (opt/transmit/m2_peaks.py).
  - Spec 2.5, line 204: per watt of PEP, CE's per-latent SNR is 0.2 dB below L's. Per watt of average power it is 3.4 dB below.
- *Gain:*
  - 0 on bands limited by peak power, such as US 60 m (9.15 W ERP, qrss-spec/60m-rules.md), where L and CE tie.
  - Up to 3.2 dB of average power, but only for heat- or duty-limited stations that alternate CE with L or SSTVAE.
  - 0 for a station sending only L, which can set its one level for L today.
  - The stronger reason is safety: today CE runs 30 minutes of key-down at SSTVAE's level.
- *Cost:* Small: two config keys and two controls, added to the tests/test_native_settings.py fixture where no field holds its default, and to test_settings_dialog.cpp's round trip.
- *Risk / constraints:*
  - An L level set too high trips the rig's ALC (O20).
  - It changes amplitude only, not the format. Fielded listeners and the C beacon are unaffected.

**O20. A drive-setting rule, or a "Tune L" button, using the lead-in tone**
- *What:* L's lead-in and callsign windows are a steady tone at the data's average power, and the pass's peak is about 3.2 dB above it.
  - Document the rule: set drive so that the steady tone reads about 47% of rated PEP (-3.3 dB), with the ALC idle.
  - Optionally add a Tune button that keys that tone at the L level for 10 s.
- *Why we think so:*
  - Today qrss_transmit.py:19-21 says only to set drive "by its peaks", which most meters cannot show for a 16 Bd signal.
  - Measured (opt/transmit/m2_peaks.py, L_FULL, two seeds): the windows sit 0.01 dB below the data average. The pass peak is 3.20 to 3.24 dB above the data RMS at both 250 Hz and 8 kHz, and no 8 kHz sample in 13 million exceeds 3.5 dB.
  - Spec 2.2 already asks for the ALC not to be engaged (line 133).
- *Gain:* Not quantified. It keeps the rig's ALC out of L. The ALC's own compression and AM-to-PM distortion are not something the scrambler can average away within a pass (inferred).
- *Cost:* A docs line, and a small amount of app work for the button.
- *Risk / constraints:*
  - Wattmeters read average or peak inconsistently, so state the rule relative to the rig's rating rather than as a meter reading.
  - Operations only.

**O5. At the finish, choose a tile's waveform among the finish's own passes**
- *What:* _takes lets a pass of the other waveform update a tile only if its Z_ref beats the tile's (live.py:1143-1158). But at the finish, the tile's Z_ref came from a live refresh on partial audio.
  - A finish pass of the other waveform can therefore be dropped, and the tile ends "lost" (live.py:1325-1330, 1344-1346), even though the finish receiver chose that waveform on the whole slot.
  - Reset the tile's Z_ref before applying the finish results.
- *Why we think so:*
  - wfC_parts.txt:303-306: "a weaker pass on the other waveform is not stored, and the tile ends lost".
  - The joint receiver already merges CE and L results within MERGE_HZ by the larger Z_ref (receiver.py:66-79).
- *Gain:*
  - No stored passes lost to a stale live comparison in mixed listening (inferred).
  - The finish usually has more audio and a larger Z_ref, so this needs a pass that reads weaker at the finish. How often that happens is unmeasured.
- *Cost:* Small.
- *Risk / constraints:* Affects only a listener hearing both waveforms. With one waveform selected, _takes always returns true.

**O6. Key finished and recovered slots by (slot, waveform); fix state.json's frame under --just-l**
- *What:* recover() skips any slot that is in `finished` or has any stored pass, whatever its waveform (live.py:735, 798-801, 942-990). Two cases go wrong: a slot finished under --just-ce, or one whose finish stored the CE pass and then died, is never received for L after a restart listening for both. The fix:
  - Key both records by (slot, waveform), and read old bare entries as "all waveforms done".
  - Re-receive only the missing waveform, with the finish restricted to that waveform's frame. A joint finish would file a second copy of the stored pass.
  - Write the right top-level "frame" in state.json under --just-l.
  - The build records' "compute live_end_s per preamble" item is not needed; see D6.
- *Why we think so:* waveform-l.md:374-377; wfC_parts.txt:68-69 and 303-306.
- *Gain:* Correctness: no lost passes after a change of waveform selection, a restart or a partial finish.
- *Cost:* Small.
- *Risk / constraints:*
  - Old state files must still load. The app reads state.json (native/gui/qrss_page.cpp).
  - CE results unchanged.

**O13. Requeue a finish that threw, once, from the passband store**
- *What:* If the end-of-slot job raises an exception, the slot is never retried, although the passband store holds its audio for 48 h:
  - The exception can come from the spawned child dying, for example (live.py:385-389).
  - The worker sets s.done (live.py:1067-1078).
  - step() moves the tiles to done_tiles, still marked "finishing", and adds the slot to finished (live.py:1042-1046).
  - recover() then skips it (live.py:971).
  - The fix: mark the tiles failed, and queue the slot once more through the recover path, with a retry count.
- *Why we think so:* Only _finish_apply sets a tile "complete" or "lost" (live.py:1316-1347).
- *Gain:* A transient failure, such as an out-of-memory kill of the child, no longer drops a slot's passes from the multi-pass store (inferred).
- *Cost:* Small.
- *Risk / constraints:*
  - A deterministic exception repeats once, bounded by the retry count.
  - CE results unchanged.

**O7. Before L goes on the air widely, tell operators to update their listeners**
- *What:* Put "run the joint listener before L appears on the band" in the release notes, and offer C1's flag for --just-ce.
- *Why we think so:*
  - CE's gate V passes L signals at Z 6 to 11, and only the joint receiver's D3 check refuses them (receiver.py:66-79).
  - Under --just-ce, L produces a live TINY tile at z 7.3 with latent SNR of -8.0 dB, a live SHORT tile at z 7.2, and a suspect WAV pass at Z 6.5 (wfC_parts.txt:301, 545).
- *Gain:* No junk tiles or junk provisional passes at updated stations, which also limits association cost (O1).
- *Cost:* Trivial.
- *Risk / constraints:*
  - Stations that do not upgrade will see junk tiles for L, but keep decoding CE.
  - No transmit change; the C beacon is unaffected.

**O19. Keep carriers about 100 to 120 Hz inside 300 to 2700 Hz (UI hint, docs, L pilot minimum)**
- *What:*
  - Recommend about 420 to 2580 Hz in the app's frequency-picker hint (native/gui/qrss_tx.hpp:52-53 accepts 300 to 2700 Hz) and in the docs, including docs/qrss/60m.md:17-18, which recommends audio offsets of 300 to 1900 Hz.
  - Raising tx.check_pilot's minimum (an L pilot is accepted down to 322.07 Hz, tx.py:159-170) is a policy question for the spec owner.
- *Why we think so:*
  - The 6.5 dB edge case under R1 (wfC_parts.txt:302).
  - Beyond the noise estimate, a CE carrier named at 300 Hz has its 99% band (±25.75 Hz) reaching down to 274 Hz. There the front end is about -2 dB (computed from the measured response), so part of the signal itself is cut. A rig's SSB filter is several dB down there too.
- *Gain:*
  - Avoids the measured 6.5 dB case until R1 lands.
  - After R1, still avoids about 1 to 2 dB of CE signal loss at the extreme edge (inferred), plus whatever the rig's filter costs.
- *Cost:* Trivial: constants and a docs line.
- *Risk / constraints:*
  - About 8 to 10% less audio range for stations packing the band.
  - Only a sender's choice changes; receivers still search the whole band.

**O11. Spend idle CPU on candidates the 12-candidate, 180 s finish cannot reach**
- *What:* Two parts:
  - (a) Set the finish budget from the time left before the next finish is due: up to about 600 s when nothing is queued, back to 180 s when backlogged. Scale max_candidates with it.
  - (b) At idle, run a second pass over the 48 h passband store that verifies the candidates ranked 13th and below. Start with those near remembered station frequencies (R3).
- *Why we think so:*
  - The budget is 12 candidates and 180 s, with "a false one costs ~1 min" (live.py:106-108). Both waveforms now share it (live.py:320-333).
  - A slot comes every 900 s, and the finish takes 3 to 5 minutes (live.py:1021).
  - Joint acquisition costs about twice as much as single-waveform acquisition (wfB_parts.txt), and refused hints eat the same budget (R9).
- *Gain:* Inferred and unmeasured: restores the search depth each waveform had before they shared a budget. It matters only on busy bands, where the budget binds.
- *Cost:* Small for (a); moderate for (b).
- *Risk / constraints:*
  - A larger budget can delay the next finish, so bound it by the queue.
  - It adds stored passes and changes the CE-only search, so CE goes behind a flag.

**O12. Search back for a picture's missed earlier segments once a later segment's header decodes**
- *What:* When segment k's header decodes, queue an idle-time search for the earlier segments in the 48 h passband store, with a CPU cap.
  - The search uses the known waveform: em.template_search or retro_detect for CE, and L2 for L.
  - Today due() looks only forward, and these searches run only by hand (qrss_decode --em or --retro).
- *Why we think so:*
  - store.py:393-411.
  - The CE search finds a -40 dB quiet FULL pass against a +5 dB accumulator (tests/test_qrss_em.py:625-636, Z 7.7, one seed). That compares with -31 dB for live detection and -34 dB for whole-slot detection (spec 6.5).
  - The slot arithmetic (two slots apart, same frequency) is store.due's assumption, not the format's. Spec 3 lets segments be spread out, and spec 10 moves frequency every pass. The app's scheduler, by contrast, sends passes half an hour apart on one frequency (native/gui/qrss_schedule.hpp:80, 91).
- *Gain:* Inferred:
  - For senders who send segments back to back on one frequency, a missed group-0 segment becomes recoverable to about -35 to -40 dB on CE. Group 0 is the coarse picture that segments B and C refine.
  - The same for L once L2 exists.
  - Other senders would need a search of the whole band in each candidate slot.
- *Cost:* Moderate for CE (existing functions, an idle queue and a CPU cap); large for L.
- *Risk / constraints:*
  - Deep template search has its own false-accept rate, so gate it, for example by correlation against the accumulator.
  - It adds stored passes, so CE goes behind a flag.

**O14. After a restart, recover the passband store's full 48 h at idle (mostly there already)**
- *What already works:* qrss_listen.py has --recover-hours (default 3 h, lines 90-93). The recover queue already runs newest first, one slot at a time, while the finish is idle (live.py:1037-1041). "--recover-hours 48" therefore does what this item asks today.
- *What is left:*
  - The default, and the listener command the app runs.
  - recover() runs synchronously at start. It reads each candidate slot's audio from the FLAC passband store to check coverage (live.py:980-983), about 190 slot reads at 48 h before listening begins. Make that check lazy, at dispatch time.
- *Gain:* No lost passes after outages of up to 48 h, at about 3 to 5 minutes of background CPU per slot (inferred).
- *Cost:* Small.
- *Risk / constraints:*
  - CPU on a laptop or phone, hence a cap and a setting.
  - CE results unchanged.

**O21. Show weak live tiles matched against the store during the pass (display only)**
- *What:* Today a weak live tile with no header of its own is drawn on its own: segment 0, mode A, not combined with the store (live.py:1476).
  - Once about half the frame is heard, run association rules 2 and 3 read-only, off the lock. They use the tile's partial z, w and LLRs against open accumulators.
  - On a match, show the combined picture and the callsign, marked tentative.
  - Filing in the store still happens only at the finish.
- *Why we think so:*
  - Rules 2 and 3 accept any z and w (associate.py:331-352).
  - Associating one weak headerless pass takes 0.46 s with 30 accumulators and 58 clusters (measured this round).
  - 8 passes at -20 dB combine to W of +5.6 dB (e2e-results.md), so a pass invisible alone shows once combined.
- *Gain:* On a repeat pass, the operator sees the combined picture and the callsign 15 to 25 minutes earlier (inferred). Stored results are unchanged.
- *Cost:* Small to moderate.
- *Risk / constraints:*
  - A wrong early match would show the wrong picture, hence "tentative".
  - corr_accept's test scales with the evidence, so a partial pass gets a weaker test, not a looser one.

**O22. Warn when an own send would blank a due segment of a picture being received**
- *What:* During a QRSS send the app stops feeding capture (main_window.cpp:154-155, rx_panel.cpp:379-380, qrss_window.cpp:673-674). Overlapping slots are lost from both the ring and the passband store.
  - The transmit scheduler could read the listener's due list, and warn when a send would blank a B or C segment of a picture that is partly received.
  - The listener could mark those slots "own send" instead of "lost".
- *Why we think so:*
  - live.py:845-846: "N s of audio missing; the stream restarts there".
  - store.py:393-402.
  - Whether the QRSS window should keep listening during an own pass is still open with Jeff (ui-handoff.md:137-138).
- *Gain:* No refinement segment of a partly received picture lost to our own send (inferred).
- *Cost:* Small: put the due slots in state.json, and add a warning in the schedule dialog.
- *Risk / constraints:* UI only.

## Docs and tests

D10, D11 and D12 were added from the verifiers' findings. D4 was dropped (see the closing section).

**D10. Measure L multi-pass end to end before ranking the L EM and tracking work**
- *What:* Run 8 to 16 L_FULL passes at -26 to -30 dB on quiet fading through the real listener finish: blind A3, header association, and the accumulator. Compare the accumulated W with spec 6.3. Offline, about 16 passes at about 60 s each, not in CI.
- *Why we think so:*
  - L's 16-pass thresholds (-29.6 dB for a good picture) come from the spec model (sims/l_spread.py; spec 6.3).
  - That model iterates EM-aided tracking, in which the data-aided reference rises from 20% to 66% of the power (spec 6.4). L's receiver has no EM, and EM measured small wherever it was tried (L5).
  - The one end-to-end multi-pass run (e2e-results.md, 2026-10-09) is CE only.
  - CE's -26.4 dB was confirmed with the nominal frequency and timing handed to the receiver (README.md:805-809).
- *Gain:* No dB directly. It decides whether L's 16-pass figure holds (probably optimistic, inferred), and how to rank L2, L5 and L8.
- *Cost:* Under an hour of offline CPU.
- *Risk / constraints:* None.

**D1. Measure L's header thresholds: block only, spread copy only, and both**
- *What:* Spec 5 gives L's header as an estimate of about -27 dB and says "neither is measured yet".
  - Measure it as was done for CE, on L_MEDIUM or L_FULL (l-short has too few data symbols), from the stored llr_block and llr_spread.
  - Cover steady, quiet and moderate paths, with at least 10 seeds per point.
  - Add one slow-tier test of block-only, spread-only and combined decodes. It also gives the rate at which the pair fallback rescues a header, which nobody has measured.
- *Why we think so:*
  - Spec line 384; qrss-build/l-spread-header.md; waveform-l.md:166-168.
  - CE's estimate was -23.5 dB, and it measured -28.7 dB from the block, -30.3 dB with both copies and -26.9 dB from the copy alone, on steady paths.
  - L's header symbols pass the clipper at gain 1.006 and 16.9 dB SINR (opt/transmit/m2_peaks.py), and spec line 384 calls L's estimate "probably pessimistic too".
  - On fading, CE's header is limited by tracking (-29.0 dB quiet, -26.9 dB moderate), and L's tracking is weaker.
- *Gain:* No dB directly. Inferred: L's steady-path header is likely nearer -30 to -32 dB than -27 dB, and its fading header probably no better than CE's. The result decides F3 and the F2 batch.
- *Cost:* A header-only sweep: about 1,320 symbols plus the copy, with no codec. Minutes per SNR point, offline.
- *Risk / constraints:* None. The spec owner edits the spec text.

**D11. Replay the real recordings as the acceptance test for acquisition and noise-floor changes**
- *What:* Before merging R1, R2, R4, L1, L3, L7 or L11, replay the KC2G recordings from 04:15Z and 03:45Z, and the one with AG7EW beside KC2G. Require no regression.
- *Why we think so:*
  - Every gain in this list was measured on white noise with ideal tones.
  - The one field case contradicted simulation twice: a neighbour in the noise floor (README.md:347-357), and a 0.6 Hz error in the reported frequency (opt/rxcode/m1_kc2g_freq.py).
  - A rig's SSB filter skirt never appears in the simulator.
- *Gain:* No dB. It catches wins that exist only in simulation.
- *Cost:* Small. The recordings and the replay path exist; it can run in the slow tier or as a manual script.
- *Risk / constraints:* Two recordings are a thin sample. They guard against regressions rather than measure gains.

**D12. Measure L's gate V at -31 to -34 dB on quiet fading before building L2 or L3**
- *What:* Measure L gate V's Z_ref at the true timing on L_FULL, at -31 to -34 dB, on quiet and steady paths.
- *Why we think so:*
  - For CE, gate V already binds at depth: at quiet -34 dB, Z_ref is about 4 at the true timing, and 0 of 10 passes are found (README.md:866-869).
  - If L's gate V binds at a similar depth, the deeper candidates that L2, L3 or L11 produce would be refused at 60 to 80 s each, out of the 180 s budget.
- *Gain:* No dB directly. It bounds what L2, L3 and L11 can deliver.
- *Cost:* A few FULL tracks per point, offline, at 60 to 80 s each.
- *Risk / constraints:* None.

**D2. Measure gate V's noise tail at Z_ACCEPT for each waveform, blind and timed, offline**
- *What:* Gate V's false-accept rate is asserted from Z_ref being N(0,1) on noise, about 1e-9 to 1e-8 per candidate (constants.py:64, track.py:1386-1392). The recorded distributions disagree.
  - Run offline noise-only campaigns per waveform, with fit=True and fit=False.
  - Include the real false population: mis-timed real signals, CE sideband ghosts, the L -1/T_L alias and other stations.
  - Start with TINY frames, and confirm on a smaller FULL sample.
  - Then keep Z_ACCEPT at 6, raise it for blind L, or lower it for CE (with a flag or Jeff's OK).
- *Why we think so:*
  - **Blind L on noise.** The builder measured a mean of 0.94, against CE's 0.19 (wfB_parts.txt:109). The reviewer's 20 seeds gave mean 0.52, sd 1.16 and max 3.17 (wfB:178).
  - **Timed L.** The builder measured sd 0.62. The reviewer's 60 seeds gave mean 0.02, sd 0.78 and max 1.86 (wfB:110, 148).
  - **Implied rate.** A Gaussian with the builder's blind mean puts 6 at 4.4 sigma. That is about 6e-6 per candidate, a thousand times the design figure (inferred).
- *Gain:*
  - For CE, about 1.5 dB of blind depth at a threshold of 5, if the real-band tail allows it (inferred).
  - For L about 0 until A3 improves, since A3 binds before gate V for blind passes. The measurement may instead show that blind L needs a higher threshold.
- *Cost:*
  - TINY first: blind fit_timing_l takes 0.06 s, so about 5,000 channels take 10 to 15 minutes in 3 or 4 runs.
  - FULL: 60 to 80 s per gate, so 1,000 gates take about 20 CPU-hours.
  - Offline only.
- *Risk / constraints:* A calibration on white noise alone would be optimistic, hence the real false population.

**D3. Measure the L ghost rule, and reconcile GHOST_HZ (20 Hz) with L's 22.07 Hz band**
- *What:* _ghost_of applies CE's ghost rule to L, and its docstring says this is unmeasured. The docstring also claims GHOST_HZ covers an L signal's data band, 3 to 22 Hz below its pilot. But GHOST_HZ is 20 Hz and the band reaches 22.07 Hz (receiver.py:100, 430-440; design.md:926). The plan:
  - Simulate a strong L signal, with gate V at the reference-period offsets (k times 1.03 Hz) and at hits in the 20 to 22 Hz strip below the pilot.
  - Add one test of a FULL L pass bleeding into the next slot.
  - Fix the docstring either way.
- *Why we think so:* The gap is narrower than it looks.
  - drop_band_owned (acquire.py:182-188, 1498-1519) already drops L preamble hits 3.1 to 22.1 Hz below a stronger hit.
  - That includes the -1/T_L alias, which passes A2-L in 3 of 12 trials at -12 dB and 12 of 12 at -6 and 0 dB, with timing up to 1.9 T_L off.
  - What is left: A3, hint and tbd candidates in the 20 to 22.07 Hz strip, and the rule applied after tracking (live.py:1399-1433).
- *Gain:* Prevents ghost or junk L passes beside strong L signals, which would also add to association cost (inferred).
- *Cost:* A handful of slow-tier simulations and a docstring fix.
- *Risk / constraints:*
  - Raising GHOST_HZ to about 23 Hz for L could swallow a real neighbour 20 to 23 Hz away, so apply it only when the timing also matches (GHOST_T).
  - CE unchanged.

**D5. Fix stale and contradictory statements in the QRSS docs and build records**
- *(1) README.* docs/qrss/README.md's comparison table is titled "waveform CE" and has no L column. Its Status row says "Simulation only" for CE (README.md:22, 31), though CE is on the air and the README itself describes a KC2G replay (README.md:352-357). Keep "L: simulation only".
- *(2) design.md, spec revision.* It still says spec rev 9 at lines 5 and 28. Line 24 already names rev 13 for L.
- *(2) design.md, A1.* It describes A1 as plain Gamma(M) (design.md:607), against the moment-matched Gamma in acquire.py:357-376.
- *(2) design.md, A2.* It describes A2 as ten 2 s chunks with Gamma(10) (design.md:617-619), against A2_SCALES (1, 2, 5, 10) at acquire.py:178.
- *(2) design.md, spline knots.* It gives 60 s spline knots (design.md:670, 724), against KNOTS_S of 10.0 s (track.py:104).
- *(2) design.md, header version.* It puts D10 "Version 1" beside D9's version 2 (design.md:1310-1311).
- *(2) design.md, test time.* It keeps "must stay under 90 s" (design.md:1090) beside its own record of 591 tests in 157.4 s (design.md:1227).
- *(3)* qrss-build/spec-notes-from-build.md is behind section 14 of design.md. Its owner is the spec thread.
- *(4)* The build records' "L preamble is 6 ms longer" (wfC_parts.txt:304, 315, 549) is wrong. Both preambles last 20.00625 s: 660 x 0.0303125 s and 330 x 0.060625 s (constants.py:38, 74).
- *Gain:* A correct record, for example not telling readers that CE has never met a real band.
- *Cost:* Small text edits, best made by each document's owner.
- *Risk / constraints:* None to the code. On dates: CE has been on the air since 2026-10-09 (waveform-l.md:6), and in format v2 since 2026-10-10 (constants.py:39).

**D6. Small correctness checks: the refresh log line, the L_KEYED_POWER fallback, equal preamble lengths**
- *(1) Log line.* The refresh log line "N signal(s) at X%" sits inside the `for p in passes` loop, after the _takes check (live.py:1274-1276). It prints once per kept pass, and not at all when none is kept. Move it after the loop. It predates L and affects logs only.
- *(2) Missing preset.* L_KEYED_POWER.get(name, 1.0) falls back to 1.0 for a new L preset. That is deliberate, per report_l's docstring, but would misreport SNR by up to 0.24 dB. Make a missing key a test failure, without changing the code (ltrack.py:121, 864-872).
- *(3) Preamble length.* Add a fast test that the CE and L preambles last the same time. This is a stated design rule (constants.py:56-58) that acquire.live_end_s() relies on (acquire.py:1537-1540). If it broke, the live candidate cache (live.py:1185) could freeze before an L preamble ends.
- *Gain:* Correct logs and SNR reports, and a guard against a silent live-acquisition regression.
- *Cost:* Minutes each; fast-tier tests.
- *Risk / constraints:* None. CE results unchanged.

**D7. Pin L's acquisition rates in tests**
- *What:* The claim that live L acquisition stops near -26 dB because of A1 is checked by nothing (waveform-l.md:360-366, from 8 seeds). Three tests:
  - Loose-bound rate checks on about 20 captures of the l-short preamble at -24 and -28 dB, in the slow tier.
  - One pinned-seed rank check in the fast tier.
  - A3's L_FULL cliff, pinned in the slow tier at about 10 s per pass.
- *Why we think so:*
  - The fast tier is already over budget: test_qrss_lacquire runs 7.4 to 13.5 s against a 5 s target, and 20 seeds at two SNRs take about 10 s.
  - Measured this round: A3 finds L_FULL in 10 of 10 trials at -30 dB and 0 of 5 at -32 dB (opt/verify-gain/m1_a3_lfull.py).
- *Gain:* A regression guard, and a baseline for L1, L3, L6, L7 and L11.
- *Cost:* Small.
- *Risk / constraints:* Rates near threshold are noisy with few seeds, so use 20 or more and loose bounds.

**D8. Pin L's 8 kHz peak-to-average ratio, which the drive advice depends on**
- *What:* Add a fast l-tiny test that transmit_audio's 8 kHz peak stays under about 3.5 dB above the data RMS, and its 99.99% point under about 3.25 dB.
- *Why we think so:*
  - qrss_transmit.py:19-21 tells operators that L is "3.2 dB peak to average", and O4 and O20 depend on that figure.
  - Measured: peak 3.20 to 3.24 dB, 99.99% point 3.15 dB, and no sample in 13 million above 3.5 dB (opt/transmit/m2_peaks.py).
  - Spec 2.2 gives 3.12 to 3.18 dB on real latents.
- *Gain:* Guards the operator guidance against a resampler or clipper change.
- *Cost:* One test of a few seconds.
- *Risk / constraints:* The thresholds must allow for variation between seeds.

**D9. Spec: state that the signal is USB (audio frequency equals RF minus dial)**
- *What:* Spec rev 13 never says which sideband to use. docs/qrss/README.md:273 and docs/qrss/60m.md:15 assume USB.
  - Add one sentence to the spec (spec owner).
  - Optionally, behind a flag, retry a failed candidate on the complex conjugate of the front-end signal.
- *Why we think so:* A grep of qrsstvae-options.md finds no sideband statement.
  - A listener on LSB hears a mirror image. WebSDRs and KiwiSDRs commonly default to LSB below 10 MHz.
  - For L, the data then sits above the pilot, and A2-L never matches.
  - For CE, the linear phase term flips sign (demod.py:83), and gate V refuses the pass. This analysis is inferred, not run.
- *Gain:* Turns a total loss into a normal reception for a listener on the wrong sideband (inferred).
- *Cost:* One sentence. A conjugate retry doubles A2 and A3 work, for failed candidates only.
- *Risk / constraints:*
  - The retry doubles the chance of false candidates, hence the flag.
  - No on-air change. Fielded listeners and the C beacon are unaffected.

## How these were checked

**Readers.** Five readers each took one view of the work: the build records, the docs and spec, the receiver code, the live listener, and the transmitter. Each read the code and records and ran up to three timed measurements. Their ideas were merged into one ranked list.

**Verifiers.** Three verifiers then checked every idea:
- **Evidence:** re-read each cited line and record.
- **Feasibility:** checked the code paths, the cost and the project constraints.
- **Gain realism:** checked each gain against the operating point where it would actually be used.

**Rules applied.** An idea was dropped if two verifiers dropped it, or if the evidence check found its premise false. Otherwise every correction was folded in and the most conservative credible gain kept. The verifiers' findings also added R9, R10, L11, D10, D11 and D12.
- **D4 was dropped.** It proposed unit tests for store and associate, but tests/test_qrss_multipass.py already holds about 30 such tests. The headerless-filing bug has its own regression test (tests/test_qrss_lreceiver.py:457), and the twin-by-centre stub exists (tests/test_qrss_live_l.py:319). What remains is a cross-waveform test of store.twin and due, and one FULL-length run of the twin call.
- **F4 was kept.** It drew one drop, and is marked not recommended.

**Measured in this round,** each run under 5 minutes:
- **By the readers:**
  - Band-edge SNR bias and a CE and L receive profile (opt/rxcode/m3_edge_cpu.py).
  - KC2G's frequency path and a re-track of it (opt/rxcode/m1_kc2g_freq.py, m2_kc2g_track.py).
  - Round A and round B gain slopes, and the errors in the reported frequency (opt/buildrec/m_rounds.py).
  - Lead-in A1 rates (opt/buildrec/m_a1lead.py, opt/transmit/m3_leadin_a1.py).
  - L section gains, peaks, and the fourth clip pass (opt/transmit/m2_peaks.py).
  - A1 pilot ranks (opt/docs-spec/a1_rank.py, a1_rank2.py).
  - Live refresh cost against the fraction heard (opt/live-ops/m1_refresh_horizon.py).
  - The L EM data prior (opt/live-ops/m2_l_em.py).
  - Store association cost (opt/live-ops/m3_store_scale.py).
- **By the verifiers:**
  - Evidence: the front-end response, a re-run of the KC2G frequency path, and a re-run of the store scale.
  - Feasibility: decoder list size 8 against 32, and A2 timing with the p-value shortcut.
  - Gain realism: A3 on L_FULL, AGC cross-modulation, and CE's refine_timing from an offset start.

**Everything else** comes from the build records and the spec (rev 13), or is marked inferred. The build records are lrx/wfB_parts.txt, lrx/wfC_parts.txt, lrx/DESIGN.md and lrx/scratch_*.

Paths starting opt/ (the measurement scripts) and lrx/ (the build records from the L work) are relative to /mnt/project-files/qrss-build/optimization/, where copies are kept. They are not files in the repository. The scripts expect the waveform_l checkout and its .venv.