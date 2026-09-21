# CLAUDE.md

SSTVAE: image transmission over HF radio by sending convolutional
autoencoder latents as analog values on OFDM carriers (RADE-style).
README.md has the waveform table and usage; most other prose is in the
GitHub wiki (see "The wiki").

This file was condensed on 2026-09-20. The long form — every measurement
and the story behind each rule — is `git show 05d96b5:CLAUDE.md`, and
most of those stories are also in the source comment or doc beside the
code they describe. Read the doc for an area before changing it.

## Commands

- `pytest` — fast suite (~10 s), includes full modem end-to-end tests.
- `pytest -m slow` (~2 min) — the listener state machine and the app's
  TX→RX loopback. Run it after touching `sstvae/rx/`.
- `tools/build_native.sh --test` — builds `native/`, runs `ctest` and
  `pytest --native`. **`--no-codec` is sticky** in the CMake cache and
  silently switches the GUI off with it, so ctest just runs fewer tests;
  undo with `cmake -DSSTVAE_BUILD_CODEC=ON` in `native/build`.
  `--sanitize` is ASan + UBSan (`SSTVAE_SANITIZE`, which also sets `-O2
  -fno-omit-frame-pointer`). `-m 'not codec'` runs the suite without
  the codec's downloaded artifacts.
- The app: `tools/build_native.sh`, then `native/build/sstvae-gui`. It is
  C++; there is no Python GUI.
- `python scripts/train.py --smoke --out /tmp/smoke` — smoke-train.
- `sstvae_encode.py` → `sstvae_simulate.py` → `sstvae_decode.py` — full
  pipeline check.
- CI gates you can run locally: `tools/check_layering.py`,
  `tools/check_includes.py`, `tools/check_android_java.sh`,
  `tools/gen_config_header.py --check`, `tools/gen_golden_vectors.py
  --check`.

## Testing the live paths without hardware

The audio and rig bugs found so far were all invisible to unit tests;
these exercise the real code paths.

- **Rig:** rig model **1** (Hamlib's dummy) drives PTT, frequency
  readback and the whole `RigController` threading model for real.
  Model **2** (NET rigctl) against `rigctld -m 1 -t <port>` exercises
  the shared-radio path.
- **Audio loopback:** a null sink plus a *remapped* monitor, because Qt
  does not enumerate monitor sources:

  ```sh
  pactl load-module module-null-sink sink_name=null-sink
  pactl load-module module-remap-source source_name=sstvae_loop \
      master=null-sink.monitor channels=1 \
      source_properties=device.description=SSTVAE-Loopback
  ```

  Play into `null-sink`, capture `SSTVAE-Loopback`, then
  `pactl unload-module N`. **Pre-resample the file to the sink's rate**
  — `pw-play` converting 44.1k→48k on the fly cost ~4 dB of apparent
  SNR. `sstvae-audio-check --loopback` is the soundcard path's only
  real test (mode A: 220/220 frames, 27–29 dB, with the capture
  resampler in the path).
- **Anything Qt with an event loop runs under `timeout`**
  (`timeout 120 uv run python ...`) — a headless `QApplication` quit
  from a worker thread has hung runs. Never put event-loop tests in the
  pytest suites.

## Rules that hold everywhere

1. **Python is the normative definition of the on-air format.** When
   the C++ port disagrees, Python is right until proven otherwise —
   that is what keeps "compatible implementation" checkable.
2. **`sstvae/config.py` is the single source of every waveform/latent
   constant.** `native/core/config.hpp` is generated from it; never
   hand-edit it.
3. **Format constants are frozen data, never re-derived** (see "Parity").
4. **Reduce phase arguments exactly before any transcendental**, in
   both implementations and in any new DSP (see "Parity").
5. **The latent unit-RMS normalization is the on-air contract** between
   encoder, modem and training. Never renormalize anywhere else.
6. **PTT always comes back down**: a scope guard *and* an independent
   watchdog thread, in both implementations.
7. **The composer preview is `overlay::render()`'s output**, never a
   toolkit-drawn imitation, so what is arranged is what goes on the air.
8. **Half duplex:** transmitting suspends receive, and receive resumes
   on a *fresh* ring buffer so our own tail is not decoded back.
9. **Torch is training-only.** The runtime codec is onnxruntime; nothing
   in the send/receive path imports torch, and the app has no GPU path.
10. **No Qt stylesheets, anywhere; use `QPalette`** (see "Desktop GUI").
11. **Score quality end to end** — PSNR through the real modem, scored on
    `latents × weights`, never bare latents. Latent-domain objectives
    and proxies flatter themselves 2–2.5×.
12. **A test that passes while testing nothing is worse than none.**
    Missing prerequisites fail loudly (`--native` errors,
    `SSTVAE_REQUIRE_CODEC=1` turns skips into failures), and a new
    assertion is mutation-checked before it is trusted.

## Python reference (`sstvae/`)

### The modem (`sstvae/modem/`, NumPy, no torch)

- `ofdm.py` — DFT-matrix mod/demod, 24 carriers × 50 Hz at 950–2100 Hz,
  on integer multiples of 50 Hz so the CP is truly cyclic
  (`docs/cyclic-prefix.md`). The pilot is described under "Waveform
  decisions".
- `sync.py` — preamble detection (lag-160 autocorrelation over a
  480-sample window, energy-floored metric), fractional + integer-bin
  CFO, template timing. `acquire_blind()` is the preamble-free path:
  matched filter against the bare pilot at lag `FRAME_SAMPLES`, energy
  folded into 1152 phase bins, CFO searched directly.
  **`BlindAccumulator` folds in absolute coordinates — always pass the
  buffer's absolute start to `result(origin=...)`.** The two agree only
  while the buffer starts at 0 mod `FRAME_SAMPLES`, which stops holding
  once a session outlives the ring: blind acquisition then locked with
  a healthy score and a demod grid 0..1151 samples off (field bug,
  2026-08-24, found through the `blind_locked`/`blind_score` status
  fields, which exist for that kind of hunt).
- `framing.py` — per-group interleaver and Golay-coded header.
  `_TX_PERMS` truncates each permutation to the transmittable budget;
  `interleave`/`deinterleave` span a whole mode's frame range;
  `slot_range_for_frame(abs_frame)` maps a frame to its latents without
  a known mode (blind decode); `frame_of_latent()` is its inverse.
- `modem.py` — `Modem.modulate/demodulate`: pilot EQ with Catmull-Rom
  interpolation, EMA-smoothed clock-drift tracking, per-latent
  confidence weights. `demodulate_blind()` has no header, so output is
  sized for mode C (every mode is a prefix of it), frames are placed by
  the beacon's absolute counter, and there is no drift tracking. Since
  `PROTOCOL_VERSION` 4 the beacon's mode clips frames past the real end
  instead of placing noise; an unknown mode falls back to mode C.
- `beacon.py` — the resync/callsign side-channel on `BEACON_CARRIER`: a
  repeating Golay(24,12) superframe of Barker-13 sync, **absolute**
  frame counter (one decoded copy anywhere gives exact position),
  8-char callsign, 2-bit mode, 8 reserved bits pinned to
  `BEACON_RESERVED_VALUE` (0xAA) and CRC-16. That layout — CRC last,
  reserved pinned — is load-bearing for `_decode_combined`, keeping the
  last two chunks predictable (~6e-8 bit-exact verification). Reserved
  bits are ignored on single-shot decode (forward compatibility); only
  the combining fallback predicts them. **An unknown mode index (3)
  means assume mode C, never reject the packet.** `MIN_FRAMES_FOR_SYNC`
  (~73 frames, ~10.5 s) guarantees one full copy.
- `golay.py` — Golay(24,12), brute-force soft ML decode.

### Everything else

- `config.py` — one carrier (`BEACON_CARRIER`) is reserved, so
  `LATENTS_PER_FRAME` (23-carrier capacity) no longer divides
  `GROUP_LATENTS` (132 ch). `FRAMES_PER_GROUP` is pinned to the
  pre-beacon 24-carrier capacity (mode durations unchanged) and
  `DROPPED_LATENTS_PER_GROUP` (~4.2%) is a permanent erasure.
- `hfchannel.py` — AWGN in the `SNR_REF_BW_HZ` convention, Watterson
  mpg/mpp/mpd fading, frequency/clock offset.
- `models/autoencoder.py` — encoder (unit-RMS tanh latents, 132 ch in 3
  ordered groups of 44) and decoder (latents × weights + weight planes;
  handles erasures and truncation).
- `latent_channel.py` — stage-1 differentiable channel.
  `waveform_channel.py` — stage-2 differentiable modem replica (torch,
  fp32 outside autocast; >0.98 correlation with the NumPy modem). It
  mirrors the 23-carrier accounting but fills the beacon carrier with
  random BPSK. **Keep `_clip_filter` in step with the real clipper**
  (`Stage2Config.clip_overshoot`): the encoder is fine-tuned through it.
- `codec.py` — `load_codec(path, precision=, backend="auto")` /
  `reconstruct` / `pad_to_full`, always on CPU (`docs/onnx.md`). A `.pt`
  goes to `TorchCodec` (the reference;
  without torch it raises a pointed `SystemExit`), anything else to
  `OnnxCodec`. `reconstruct(codec, latents, weights)` keeps its exact
  signature. Encoder and decoder load **lazily and independently**, so
  a receive-only station fetches 9 MB, not 21. `--model` takes a
  directory, one `.onnx`, or a `.pt`. `OnnxCodec` cross-checks the two
  parts' `source_sha256` — mismatched checkpoints would decode a
  silently wrong picture. Precisions may differ; checkpoints may not.
- `latents.py` — numpy `latents_to_flat` / `flat_to_latents`. Must agree
  **exactly** with the torch statics on `SSTVAE` (`tests/test_latents.py`);
  the send/receive path imports this, never `models`.
- `images.py` — geometry (`IMG_W`/`IMG_H`), `fit_image`,
  `image_to_array`, font search. **Must import without torch**
  (`image_to_tensor` imports it lazily, for training). Import from `images`, never `data`
  (training-only; pulls in torchvision), anywhere in send/receive.
- `audio.py` — PortAudio enumeration and streams (the path
  `sstvae_listen.py` uses); imports `sounddevice` lazily.
- `overlay/` — `model.py` is the document, `render.py` draws it with PIL
  for the Python tests. Coordinates are normalized 0..1 and
  `ImageItem.source` is late-bound (`"last_rx"` or a path), so templates
  stay a UI-only change.
- `tx/engine.py` — encode → modulate → PTT → play → unkey; try/finally
  plus an independent `_PttWatchdog`. `condition_for_output` is a plain
  peak scale — `modulate` already clipped, and a second clip splatters.

`sstvae/gui/` and `sstvae/rig/` were deleted on 2026-08-01; the app is
`native/`. Nothing left in `sstvae/` may import Qt.
`tests/test_native_settings.py` drives the C++ settings reader from a
fixture in which **no field holds its default**, plus a guard
(`test_the_fixture_holds_no_default`) that fails when a setting exists
in C++ but not in the fixture.

### The receive state machine (`sstvae/rx/`, mirrored in `native/core/rx/`)

`sstvae_listen.py` is its CLI. `decode_loop` / `decode_loop_low_cpu`
are load-bearing — **run `pytest -m slow` after touching them.** The
seams are an `RxConfig` and a `sink`; **saving is the sink's job, not
the loop's**, because autosave may be off. `ringbuffer.py` adds `tail()`
(cheap, for the waterfall) and `clear()`. Every rule below was a field
bug:

- **Completion is judged on a reception retained across polls**
  (`_Pending` / `Pending`), and **a poll that decodes nothing counts
  against it.** Receptions stop decoding long before they end (audio
  scrolls out of the ring; the blind score decays), and testing only on
  polls that decoded caused both "stuck in receiving" and "autosave
  never fires" — one bug.
- The header path has **its own deadline** (its mode's duration past its
  start). `decode_loop_low_cpu`'s wait for audio is **bounded** (missing
  audio + `end_grace`) — unbounded, a dead capture hangs it forever.
- **Delivering and retiring are separate.** A stall (`end_grace` with no
  progress) *delivers* and leaves the reception **dormant** (`waiting`,
  `Status::Waiting`)
  until `complete`, its deadline, or another reception taking the slot:
  a fade longer than `end_grace` looks exactly like a transmitter that
  stopped, and retiring refused the rest of the picture. It is nearly
  free because every decode is retrospective over the 130 s ring, which
  outlives the longest (95 s) mode.
  - `Reception.saved_path` means "already delivered here — **replace**
    it" (one transmission, one file); `Reception.redelivery` carries
    "same reception again" separately, since a sink with autosave off
    returns no path.
  - A dormant reception is in `finished_starts` yet resumable on both
    paths: blind checks the tracked reception first; the header path
    aims one targeted demodulate at its own preamble.
  - Taking over the slot **delivers** what it replaces. Only an
    at-least-as-good decode (by `metric`) replaces the held picture.
  - `deadline_abs` has a wall-clock shadow, `deadline_wall`, anchored
    once: a dying capture freezes `total` below the buffer deadline.
- **The stall clock watches the confident-latent count, on both paths,
  and nothing else.** Nothing is delivered without confident latents —
  that, not the stall, is what keeps noise locks out
  (`test_noise_produces_nothing`). Buffer coverage (`received.sum()`)
  climbs for noise; anything positional misses retrospective fills
  behind the furthest frame. It is also what `metric` means on both
  paths: `pending.metric` and `delivered_metric` are compared across
  polls, and a reception can stall on one path and resume on the other.
- **Three numbers.** `frames_received` is the progress bar: on the blind
  path `_frames_elapsed`, counted from the transmission's own first
  frame (a late join starts part-way and still reaches 100%); on the
  header path `received.sum()`. `frames_decoded` is how many frames
  carried confident data. The confident-latent count feeds only the
  stall clock. `_decode_progress` returns the last two from one pass
  (guarding `frame_of_latent()`'s -1 slots); `complete` stays gated on
  `not pending.blind`. Pinned by `tests/test_rx_progress.py` and,
  with a stubbed modem, `tests/test_rx_watchdog.py`. In C++,
  `rx::decode_progress` and `rx::frames_elapsed` are public for tests.

## Waveform decisions (measured — re-measure before changing)

- **The preamble is four pilot repeats with a 480-sample correlation
  window** (`PROTOCOL_VERSION` 2). With two, the metric's noise floor
  sat inside the threshold — a false lock every few hours on a quiet
  band, with only the Golay header behind it (3 valid of 4096, which
  cannot be tightened). Four at `PREAMBLE_THRESHOLD` 0.42: no crossing
  in 3000 s of AWGN
  *and* better acquisition. **`PREAMBLE_SAMPLES` and
  `PREAMBLE_CORR_WINDOW` are one change**; `tests/test_preamble.py`
  catches a stale window through the metric's output length.
- **The pilot is three things** — the frame EQ reference, the preamble
  and the blind template — and is a minimized-crest-factor phase set at
  0.99 dB PAPR (`PROTOCOL_VERSION` 3; ~+2.5 dB latent SNR over the old
  7.9 dB draw). It is an exact rational turn
  (`PILOT_PHASE_NUM`/`PILOT_PHASE_DEN`), which is where any new format
  constant should aim.
  - **Never Zadoff-Chu**: a ZC frequency shift *is* a time shift, so CFO
    and timing become confusable (blind locked 55.7 Hz off). It is
    invisible at zero CFO, which is where the screen that passed it ran.
  - Pilot PAPR is a proxy; optimize latent SNR and acquisition directly.
  - The pilot, `CLIP_HEADROOM_DB` and `BLIND_SCORE_THRESHOLD` (9.0,
    between the highest false score 7.33 and the lowest true 10.19)
    move together. Four hardcoded copies of the old threshold existed.
- **The ~0.77 latent gain is deliberate; do not "fix" it.** The clipper
  compresses data symbols and leaves the flat pilot alone, so latents
  return small; the decoder consumes `latents × weights` and the
  weights already are a Wiener shrinkage. Currently 0.7721 ± 0.0009;
  re-measure whenever `CLIP_HEADROOM_DB` or `CLIP_OVERSHOOT` moves
  (`native/tests/test_modem_roundtrip.cpp` checks it).
- **The clipper runs three passes with an overshoot schedule**
  (`CLIP_OVERSHOOT` = (1.0, 1.5, 2.0), `CLIP_HEADROOM_DB` 1.0;
  +0.141 dB PSNR, 8/8 cells). The overshoot is written `scale ** k`,
  **never CESSB's additive `out = x + k*(clipped - x)`**, which
  phase-inverts samples at this clipper's ~40%-duty operating point. Most of the gain is the pass
  count (three passes converge where two did not) — don't quote the
  overshoot as the mechanism. Schedule and headroom are one change.
  Gate changes on `scripts/overclip_e2e.py`, never on the latent-SNR
  proxy `scripts/overclip_sweep.py` (overpredicted ~2.5×). The v5 codec
  is fine-tuned through this clipper.
- **Acquisition** (`docs/todo-done.md`): `ACQUIRE_MAX_BINS` 12
  (±625 Hz); `BLIND_BIN_STEP_HZ` 12.5 with `sync.refine_cfo` recovering
  the sub-bin peak; `TEMPLATE_SCORE_THRESHOLD` 0.40 as a second gate,
  because a wide search can lock deep inside a real transmission's own
  data. The wide blind range and drift tracking are settings
  (`RxConfig.blind_wide` / `drift_track`, `--blind-wide` /
  `--drift-track`, Settings > Receive). **These defaults live in `config.py`**
  — four hardcoded C++ copies were found once.
- **Open** (`docs/todo.md`): the drift loop pulls in only
  ±`CFO_PULL_HZ` of initial residual (`sstvae/modem/modem.py`), and on
  the blind path the estimate is mid-window, so past ~7 Hz of drift
  across the window it aliases and the loop is worse than off — pinned
  as a test; fix: anchor mid-window and run outward. A steady carrier reads 1.000 on the preamble metric —
  read that section before adding any transmit-side tone; it is why the
  Android VOX leader is a chirp (fix idea: top-K peaks arbitrated by the
  header). In-transmission drift has a ~±2 Hz total budget, and no
  fixed loop gains serve both drift and `mpd` fading.
- **Near threshold, single-digit trials per cell invent patterns**
  (acquisition succeeds 40–80% of the time there); an "acquisition
  costs 1 dB at large offset" finding was withdrawn for exactly that.

## The native port (`native/`, C++20)

Modem, headless core and GUI are complete (`docs/native-app.md`,
phases 0–3). The exit criterion was met by loopback on 2026-07-29:
native→native, native→Python and Python→native. That is loopback, not
RF — PTT timing against a physical radio is still untested.

### Parity

- **The codec's parity is exact, not toleranced**: the encoder is
  bit-identical to Python's and the decoder byte-identical
  (`tests/test_native_parity.py -m codec`). It rests on two things.
  onnxruntime is pinned to the Python version
  (`native/cmake/onnxruntime.cmake`, sha256 per archive) — bump it in
  step with `pyproject.toml`, never ahead. The one accepted exception is
  macOS x86_64 on 1.22.0 (no later build exists), labelled a lower
  compatibility tier. And the final `* 255` is done in **float32**
  (numpy's NEP 50 behaviour), rounded with `nearbyint`, never
  `std::round`.
- The codec is the only part that downloads anything, so it sits behind
  `-DSSTVAE_BUILD_CODEC`; the whole modem builds and tests offline. Its
  tests carry the `codec` marker, and CI sets `SSTVAE_REQUIRE_CODEC=1`.
- **`pytest --native` is the port's acceptance suite**: it substitutes
  the C++ into the reference modules by attribute assignment. Every
  binding keeps its Python counterpart's **exact signature, defaults
  included** — take defaults from `config`, never copy them by hand (a
  stale `threshold=0.5` once looked exactly like an implementation
  disagreement). `from x import y` sites must be listed in
  `NATIVE_SUBSTITUTIONS`, or they silently keep testing Python. Without
  the extension module the parity tests skip and `--native` errors,
  deliberately. `tests/test_native_parity.py` diffs both
  implementations in-process when `--native` fails. It cannot run on a
  cross build, so the macOS x86_64 slice is not parity-checked.
- **Generated and committed; CI fails if stale:**
  `native/core/config.hpp` ← `tools/gen_config_header.py`, and
  `native/tests/golden/` ← `tools/gen_golden_vectors.py` (its
  `manifest.json` of sha256s is the reviewable diff).
- **Frozen format constants**: the interleaver permutations are data
  (`sstvae/modem/interleaver_perms.npy`). If numpy ever changes its
  stream, keep sending the frozen values.
  `tools/freeze_format_constants.py --verify` exits 0 either way
  (deliberately not a gate); `tests/test_frozen_format.py` fails on any
  `default_rng` call in `sstvae/modem/`.
- **Exact phase reduction** (`ofdm._phasor`, `dsp._HET_TABLE`,
  `dsp.wrap_cycles`, C++ `carrier_phasor`): `sin`/`cos` of a large
  argument differ across libms and architectures, which had broken CI.
  `(n*f) % FS` is exact for integer Hz; `to_baseband` needs only 16
  phasors. `PHASOR_TOL` is 1e-14 against a measured 9.6e-16.
- **Above the modem, identical behaviour is not required** (native
  idioms welcome — e.g. `SharedState` exposes only `get`/`update`). The
  rx engine takes its decoder as a `std::function` seam, so the whole receive state machine builds and is tested with
  `--no-codec` (`test_rx_engine.cpp`, stub decoder); `pad_to_full` lives
  in `core/latents/` for the same reason. **Don't assert that noise
  decodes to nothing** — a spurious lock happens every few seed-minutes;
  assert that noise never *finishes* a reception.

### Build, CI and test hygiene

- **Layering** (`tools/check_layering.py`): QtWidgets only in `gui/`;
  nothing under `core/` includes QtWidgets; only `core/overlay/` may
  include QtGui; only `core/audio/qt/` includes Qt Multimedia; only
  `bindings/embed/` links libpython.
- `tools/check_includes.py` finds `std::` names used without their
  header — libstdc++ and libc++ transitively include what MSVC does
  not. It follows project headers.
- `tools/check_android_java.sh` compiles the app's Java against
  Robolectric's pinned `android-all` (the real API surface). Testing
  `javac | grep` under `pipefail` checks the wrong exit status.
- `SSTVAE_BUILD_GUI` / `_QTAUDIO` / `_OVERLAY` are AUTO/ON/OFF. The GUI
  needs the codec, Qt audio, rig control and the overlay renderer, and
  ON must name which one is missing. CI's compile-only jobs use ON.
- Optional-dependency CMake blocks precede `add_subdirectory(tests)`,
  and tests key off `if(TARGET ...)`: an `if()` on an unset variable is
  silently false (a rig test once went unbuilt with ctest green).
- A ThreadSanitizer job covers `rx_engine`, `tx_engine` and
  `ringbuffer`, the only concurrent code. **Sanitizer builds are `-O2`**
  (670 s → 90 s, still symbolized); at `-O0` a latency assertion
  disguised as a watchdog timed out.
- **Watchdogs sit at many times the measured worst case**, and the hard
  bound on a wedged test is a ctest `TIMEOUT`. A printf is no hang
  diagnostic (ctest holds output until exit); `check::Watchdog` +
  `check::current_step` names the stuck step, then `std::_Exit`s.
  Watchdog fired ⇒ that step is stuck; `TIMEOUT` with `ok:` in the
  output ⇒ wedged in teardown; `TIMEOUT` with nothing ⇒ never reached
  `main`.
- Hamlib's trace is on for the rig tests (`SSTVAE_HAMLIB_DEBUG`); ctest
  discards passing output, so it costs nothing until something fails.

### Windows (each of these failed in CI only)

- Never include `<windows.h>` from a widely-included header: its
  `min`/`max` macros break `std::max`, and `NOMINMAX` only helps if
  nothing includes it first. Declare the one function by hand. A
  mingw-w64 probe checks this locally
  (`x86_64-w64-mingw32-g++ -std=c++20 -I native/tests -c` over a file
  that includes `<windows.h>` first, plus `#ifdef max #error`), and Wine
  runs the binaries (`wine rigctl.exe -m 1 f`) — a pass is suggestive,
  a failure conclusive.
- A `.lib` under `lib/gcc` is a MinGW import library: MSVC links it
  "successfully" and the exe dies with `STATUS_DLL_NOT_FOUND`. Generate
  one from the `.def`: `lib.exe /def: /machine:x64 /name:libhamlib-4.dll`
  (`/name` is required — the `.def` has no `LIBRARY` statement).
- Load-time failures happen before `main` and look exactly like
  deadlocks, so the Windows job runs the rig test once outside ctest,
  asserts its exit code and prints `dumpbin /dependents`. DLLs — all of
  them, transitive ones included — go **beside the exe**
  (`sstvae_hamlib_copy_runtime`), never on `PATH`.
- `check.hpp`'s `report_crashes_instead_of_prompting()` stops WER and
  CRT dialogs turning crashes into apparent hangs.
- A CI python job dying with `0xc000001d` in torch is the heterogeneous
  runner fleet picking AVX-512 kernels; `ci.yml` pins
  `ATEN_CPU_CAPABILITY=avx2` and `DNNL_MAX_CPU_ISA=AVX2`. If it recurs
  with those set, it is a real bug.

### Desktop GUI (`native/gui/`)

- Widgets live in the `sstvae_gui` library (`sstvae-gui` is only
  `main.cpp`) so tests can drive them; slots like `Waterfall::tick()`
  let a test render one frame without a timer.
- Receive and transmit sit **side by side in a splitter** (composing in
  tabs meant composing blind). Tabs remain a *preference* (View >
  Layout, `ui.layout`), not a screen-size adaptation: side by side
  needs 766 × 467 now, and the older 1043/545 px figures are stale.
  `"auto"` is resolved **once at startup** (a live breakpoint cannot
  switch back down); choosing a layout by hand ends auto; the receive
  status is mirrored to the status bar while tabbed.
- `gui/pane_container.cpp` `set_mode`: build the new container *first*,
  then delete the old (detaching with `setParent(nullptr)` marks the
  panes explicitly hidden). Every `addWidget`/`addTab` hides what it
  reparents and nothing un-hides it on a switch, so the new container
  needs an explicit `show()` and a rebuilt splitter shows both panes by
  hand. `test_pane_container.cpp` caught both.
- **The two panes' control strips are locked to equal height**
  (`PaneContainer::equalise_strips`) so the two pictures match, and it
  only re-runs on resize. The strips are `FlowLayout`s, where a width
  change can move a wrap and so change the height. So **no control may
  change size when the operator selects something** — the colour button acquiring an icon
  on first selection broke it on Windows only. `test_tx_panel.cpp`
  guards this; assert on a control's laid-out size, not `sizeHint()`
  (a layout bounds the hint by min/max size).
- **Never `setFixedHeight` to pin an aspect ratio** — it is a hard
  minimum, a window-height ratchet (a 1400 px wide window demanded
  1405 px of height). Nor `setHeightForWidth`, which a `QTabWidget`
  propagates. `picture_box.cpp` and `OverlayEditor` keep 4:3 by
  geometry and letterboxing. Test on a *container's*
  `minimumHeightForWidth`, driving two passes (`test_picture_box.cpp`,
  `test_overlay_editor.cpp`, both mutation-tested).
- **Never set a Qt stylesheet; use `QPalette`.** Any stylesheet swaps
  in `QStyleSheetStyle` app-wide and zeroes the padding of every combo,
  spin box and line edit — far from the cause.
- **Errors have three tiers**: errors are sticky (`ErrorBanner`) and
  logged; state has its own indicator (PTT lamp, rig chip, CLIP
  marker); progress may overwrite freely. Never write an error to a
  one-line label — it gets overwritten, and it inflates the window's
  minimum width. `core/log/` is Qt-free (`StatusLog` + rotating
  `FileWriter`); `gui/log_pane.cpp` backfills from `snapshot()`.
- The last-reception card exists because the engine clears shared state
  two seconds after a reception; the half-duplex pause has to look
  deliberate, since a stopped waterfall otherwise reads as a wedged
  capture.
- Each settings tab lives in a `QScrollArea` (horizontal scrolling off):
  a short `QFormLayout` truncates its help text instead of compressing.
- `test_settings_dialog.cpp` round-trips a config in which **no field
  holds its default** — the real bug is a displayed field not written
  back.
- One rig mapping, `gui/rig_config.cpp`, shared by `AppState` and the
  dialog's Test CAT / Test PTT, which run on a worker thread.
- Probing a rendered widget means knowing what else is painted there
  (level meter, full-height dashed band markers): probe single pixels
  in columns nothing else touches (`test_waterfall.cpp`).
- `core/dsp/spectrum.cpp`'s `reduce_to_width` is peak-hold, not
  point-sampling — carriers are 1–2 bins wide, and sampling leaves a
  ragged comb that reads as a reception fault.
- `sstvae-gui-shot` renders windows to PNG headless: a tool, not a test,
  since "is this laid out well" has no oracle. `--panes` reports both
  axes; `--text-palette` shoots popups, which appear in no widget
  render. `MainWindow` is not a target (it loads a model and opens the
  rig).

**The overlay editor and document.**

- `OverlayEditor` is a painted `QWidget`, not a `QGraphicsView`:
  selection handles come from `overlay::item_bbox`, and drags write
  normalized coordinates. `test_overlay_editor.cpp` checks the
  arithmetic.
- **The overlay document is version 2** (`DOC_VERSION`; this fork,
  2026-09-19;
  `docs/transmit-text-palette.md`): weight, slant, underline,
  `font_family`, and a solid/linear/radial fill with `color2` and
  `fill_angle`. Every default reproduces a v1 document exactly; `color`
  is the from-stop in every mode. The bump is deliberate — `from_json`
  refuses a newer document, so an older build refuses a gradient rather
  than drawing it flat. `font` (a path) beats `font_family`. There is
  no stroke flag: `stroke_width` 0 is off.
  **`sstvae/overlay/model.py` must mirror every document change**
  (`tests/test_native_overlay.py` compares version and round trip);
  `render.py` is deliberately not extended.
- **Underline needs explicit geometry**: `QPainterPath::addText` adds
  glyph outlines only, so `setUnderline(true)` alone draws nothing;
  `text_path` appends a rect per line at
  `underlinePos()` into the same path, and `item_bbox` includes it.
- **The right-click palette** (`gui/text_palette.cpp`) is a `QMenu` of
  Format / Style / Layers submenus, each a `QWidgetAction` row of live
  controls — a popup because the strip is height-locked. The editor
  selects the clicked item *before* emitting `contextMenuRequested`.
  No `QComboBox` inside a `QWidgetAction` (its popup dismisses the menu
  on some styles; the font family is a nested `QMenu`). The item pointer
  is never stored — Layers rotates the vector — and `popup_for` refuses
  anything but the selection. A `QMenu` eats wheel events, so the size
  and rotation fields use an accepting event filter. Layer moves
  (`raise_selected` and friends) that cannot happen emit nothing:
  `documentChanged` restarts speculative optimization.
- **Sizes are shown in pixels of the 640×480 frame**, in the palette and
  the strip box alike; the document stores fractions.
  `gui/overlay_units.hpp` is the one conversion. Text size is against
  the canvas *height*, an inset's width against its *width*. The strip's
  size box is `setFixedWidth` at the widest number it can show: a
  `QSpinBox` sizes itself to its maximum, which differs by item kind,
  and a minimum width leaves the hint free to move.

### Core app

- **Transmit**: `core/tx/` keeps the PTT rule with a scope guard **and**
  a `PttWatchdog` thread, declared in the header so it is tested
  directly (timeout = lead + duration + tail + 15 s;
  `TxConfig::watchdog_margin_s` exists because a compiled-in constant
  cannot be patched by a test).
- **Audio is split at the device boundary.** `core/audio/audio.hpp` is
  Qt-free and holds all the logic — `resample_ratio`, `StreamResampler`,
  sample-format conversion, `match_device` — because every audio bug
  lived there and was found against a fake device. `core/audio/qt/`
  only enumerates and moves bytes, as its own library
  (`SSTVAE_BUILD_QTAUDIO`). Capture runs on its own thread and event
  loop. The C++ mixes multichannel down in double (numpy stays float32),
  so that one parity test is not held to 1e-12. The app defaults to
  QtMultimedia (`audio.backend`: `"qt"` | `"portaudio"`); PortAudio
  stays because Qt cannot see monitor sources.
- **Model artifacts**: plain HTTPS to the Hub and **our own cache**
  (`SSTVAE_MODEL_CACHE`, else platform cache dir + `sstvae/models`) —
  never write into `huggingface_hub`'s. `qt_fetcher` follows redirects
  by hand because the 302's `x-linked-etag` is the sha256, checked
  before the bytes are accepted; downloads land as `.part` and are
  renamed only after the check. Only the download needs the network
  (the `Fetcher` seam; `resolve_onnx` and the cache lookup are path
  arithmetic). The offline error message is a deliverable with its own
  test.
- **ORT C++ trap**: `GetInputTypeInfo()` returns a `TypeInfo` by value
  and `GetTensorTypeAndShapeInfo()` is a view into it — keep both bound.
- **Latent optimization** (`core/optimize/`,
  `docs/latent-optimization.md`): the loop is behind a `GradFn` seam
  (tested with `--no-codec`), the ORT half is
  `core/codec/grad_session.cpp`. `speculative.hpp` (tested with a stub
  `GradFactory` and a latch, not a clock) runs it after an edit
  debounce, with a **generation counter** so an edit invalidates a run
  in flight; Send waits at most a short second budget, polling on a
  `QTimer`, and commits to the composition as it was at the click.
  `OverlayEditor::documentChanged` drives it and is deliberately not
  emitted by `select()` (selection changes nothing transmitted). The
  latents enter through `TxEngine`'s existing `Encoder` seam — there is
  no second transmit path. `transmit.optimize` applies on OK and on
  model load; turning it off destroys the optimizer, which is how
  refined latents are discarded. `objective_gain_db` is progress, never
  "dB earned" (it overstates ~3×).

### Rig control (`native/core/rig/`)

- A re-derivation, not a port: libhamlib **in-process**. Sharing a radio
  with WSJT-X is Hamlib model 2 (a rigctld client). Crash isolation was
  given up knowingly (`docs/native-app.md`).
- **Nothing on the GUI thread blocks on the rig.** One backend on one
  worker; PTT is priority work (worst-case latency: one in-flight
  operation); polling suspends while transmitting; **`stop()` detaches,
  never joins** (the worker co-owns its session via `shared_ptr`).
  `wait_for_shutdown()` is for whatever ends the process: on Windows a
  worker inside `rig_close` can hang on the loader lock at teardown.
  `RigController` is tested against a backend that never answers.
- Hamlib is pinned and bundled (`native/cmake/hamlib.cmake`, 4.7.2,
  sha256 — its API moves between minor releases, e.g. `token_t` became
  `hamlib_token_t` in 4.6), dynamically linked
  (LGPL). `-DSSTVAE_HAMLIB_SYSTEM=ON` is for distro packagers (≥ 4.6).
  The source build re-stamps the tarball's generated files (shared
  mtimes re-trigger `aclocal`), and installs under
  `FETCHCONTENT_BASE_DIR`, which is what makes it cacheable.
- `poll_interval` = 0: Hamlib's own poll thread would talk to the port
  behind `RigController`'s back.
- **`rig_set_conf` token names are not guessable** — a misspelling is
  silently ignored. Read `src/serial_cfg_params.h` and `src/conf.c` in
  the pinned tarball; values are case-sensitive strings (`"XONXOFF"`,
  `"Hardware"`, `"ON"`/`"OFF"`; `ptt_type` `"RIG"`/`"DTR"`/`"RTS"`/`"None"`).
- The rig settings are `CONFIG_VERSION` 2, modelled on WSJT-X's Radio
  tab. `"default"` means *don't set the token*, so an unrecognized
  value falls back to Default rather than erroring. v1's dead keys are
  listed as known, `model` accepts the v1 string, and `device` is
  reused for the port — migration must be quiet.
- On Windows **never dereference a `RIG*`** (the MSVC pthread shim in
  `native/third_party/msvc-pthread/` has sizes that must not be
  load-bearing; only `struct rig_caps` is read, via
  `rig_get_caps_cptr`), and **never link a Hamlib data symbol** (use
  `rig_version()`, not `hamlib_version2`).

**Android rig control** (2026-08-22) is Hamlib over a loopback socket:
Hamlib treats any `host:port` as a network port for any model
(`parse_hoststr`). `core/rig/transport.hpp` is the byte pipe,
`bridge.*` presents it as `127.0.0.1:<ephemeral>`, `bridged.*` composes
the two — all in `sstvae_core`, tested with fakes in a `--no-rig`
build; the JNI half is
`core/rig/android/` over `SerialBridge.java`. USB serial, Bluetooth
RFCOMM and a network host are the three connection kinds.

- A transport's presence selects the bridge — never parse the device
  string (`usb:1a86:7523` looks like a hostname to Hamlib).
- DTR/RTS keying is driven on the transport with Hamlib's `ptt_type`
  "None"; RFCOMM has no modem lines, so those methods are not offered.
- `rig::serial_defaults(model)` supplies the per-rig defaults a network
  port never gets; Hamlib's three control-line conflict checks are
  re-derived; `Default` line states resolve to **asserted** (the PTT
  line low) in `resolve_serial_params`, applied before `setFlowControl`.
- Loopback bind only: the far end is an unauthenticated path to a
  transmitter.
- `rig::set_debug_sink` captures Hamlib's trace (a phone discards
  stderr). **Register the callback only while a sink exists** — it
  replaces stderr, silently. `core/rig/trace.hpp` logs bytes *after*
  they reach the transport, never before.
- A USB id's `@unit` (several devices sharing one VID:PID — an IC-9700
  has two) is a different axis from `#port` (one device's UARTs); both
  are omitted at zero.
- `bridge.cpp` / `bridged.cpp` are not built on Windows: Winsock does
  not wake a blocked `recv` on `shutdown`.
- `Session` owns the `RigController` and **never destroys it** — the TX
  engine's `Ptt` captures it. Reconnection is app-level
  (`RigControl::maybe_reconnect`, gated on `has_permission(device)`, so
  an absent device doesn't spend the backoff). A published frequency is
  the only proof the radio answered — `RigController::running()` is
  not.
- **Open:** an IC-9700 and an IC-7100 do not answer over USB while a K4
  does. The chip's configuration is ruled out; the blind CP210x
  `SET_FLOW` write is skipped anyway — the first vendored patch
  (`native/third_party/usb-serial-for-android/PATCHES.md`, every
  deviation marked `// SSTVAE PATCH`). What is ruled out and what to try
  next: `docs/todo.md` and `native/android-app/README.md`.
- The Hamlib NDK cross-build and the Android Java have never been
  compiled. The install step refuses a versioned SONAME (Android
  packages only `lib*.so`); `-DSSTVAE_ANDROID_RIG=OFF` drops CAT and
  keeps the app.

### Android app (`native/android-app/`)

`docs/android.md` is the design; `native/android-app/README.md` is the
working document — read it before touching the app.

- A Qt Quick front end over the same `native/core/` — a fourth build,
  no new parity surface. **Its UI is not a port of the desktop's.** The
  foreground service owns the engine and the UI is a detachable view;
  reception metadata is persisted beside the picture; the waterfall is
  the tuning instrument.
- Status: receive (Tier 0) and transmit (Tier 1, verified over RF
  2026-08-09) are done; CAT/PTT works over USB on hardware, including
  audio and serial over one composite device. Bluetooth is untested;
  the Icoms are open (above). Unmeasured: multi-hour battery, and the
  VOX leader against a real VOX circuit.
- **Build with `tools/build_android.sh`** (RelWithDebInfo + zipalign +
  debug-sign): the NDK's Debug build is `-O0` (6–15× slower). `--aab`
  makes the Play bundle (upload key under `~/.android-keys/`, outside
  the repo) and never falls
  back to an unsigned or debug-signed one; `--version-code` is an
  explicit input; 16 KB page alignment has to be right in the `.so`s.
  Switching testers from the debug key to the upload key forces an
  uninstall — warn them.
- The codec ships in the APK, in `assets/` (never a `.qrc` — it would
  ship once per ABI); the bytes are released once the session is built,
  and `use_ort_model_bytes_directly` is set to 0 explicitly. With
  `SSTVAE_ANDROID_BUNDLE_MODELS=OFF` it falls back to `ModelFetcher.java`
  (Qt for Android has no TLS backend).
- Audio is Java `AudioRecord` on a blocking reader thread, not
  AAudio/Oboe. An over runs through the service under a `mediaPlayback`
  foreground type so it cannot be truncated.
- The poll cost is DSP, not the codec: `decode_loop_low_cpu` is the
  battery lever, with `RxConfig::max_decode_duty` (0.5 on Android).
- `FindClass` cannot see application classes from a thread we created —
  `set_java_vm` caches a global reference. Back closes the viewer,
  backgrounds the app while a session or an over runs, and only
  otherwise exits; `main()` ends with `std::_Exit` (teardown SIGABRTs
  counted as crashes).
- No overlay on Android, and no callsign is required to send — the
  beacon and CW ID identify the station. The VOX leader is a swept tone
  (a steady one steals the preamble lock). The gallery copy
  (`Gallery.java`) is off by default.
- Instruments lie too: three "bugs" in the port were bugs in its
  measuring tools. Re-measure before quoting any Android number from
  before 2026-08-08 evening.

### Packaging, signing, release

- CI builds five packages: linux-x86_64, linux-aarch64, macos-arm64,
  macos-x86_64 and windows-x64. The Intel Mac slice is cross-compiled
  on Apple silicon and tested under Rosetta; because it pins ORT 1.22.0,
  anything reconstructing ORT's filename must use the **resolved**
  version, never `SSTVAE_ONNXRUNTIME_VERSION` (a glob fallback also lets
  an unpacked `SSTVAE_ONNXRUNTIME_DIR` work at any version). `CMAKE_OSX_ARCHITECTURES` means nothing to autotools (Hamlib
  needs `--host` and `-arch` in CFLAGS), and Hamlib cannot build fat,
  so macOS ships two downloads.
- `tools/package_app.sh` stages a runnable tree; `tools/make_installer.sh`
  wraps it in an AppImage, `.dmg` or NSIS setup. Not CPack. The
  installer step is not gated on a tag.
- Pin packaging tools (appimagetool, NSIS; sha256) — `makensis` is not
  preinstalled on `windows-latest`, and `choco install nsis` was
  declined (it makes the tool's version a property of a feed). Git Bash has no `unzip` and its
  `tar` cannot read zips: use `powershell Expand-Archive`.
  `wine makensis.exe` tests the installer from Linux.
- **Signing**: `tools/sign.sh <app|installer> <path>` — Developer ID +
  notarization on macOS, Azure Trusted Signing on Windows. A loud no-op
  without credentials (so forks still build); `SSTVAE_REQUIRE_SIGNING=1`
  makes that fail. PRs sign only with the `sign` label; master pushes,
  dispatches and releases sign, and `native-build.yml` asserts that a
  release implies signing. Traps (`docs/native-app.md`): `security
  import` sniffs the file extension, so the p12 needs a `.p12` name and
  `-f pkcs12`; the CLI's options are all
  `trusted-signing-*`; a notarization failure can be Apple's outage —
  check their status and `notarytool log` before editing anything.
- **Icon**: `native/packaging/sstvae.svg` is the only source;
  `tools/gen_icons.py` rasterizes each size from the vector, committed
  and deliberately not a CI gate. Windows reads a compiled resource,
  macOS `CFBundleIconFile`, Linux the `.desktop` file; the app also
  calls `setWindowIcon` and `setDesktopFileName`.
- **The icon is licensed artwork the repository's LICENSE does not
  cover**, and it is not sublicensed: a fork or redistributed package
  must replace it. `NOTICE` lists every restricted file, each with a
  REUSE `.license` sidecar (`LicenseRef-SSTVAE-Branding`) that
  `gen_icons.py` writes; packages ship
  `LICENSE` and `NOTICE`. A `SSTVAE_BRANDING` placeholder switch is
  specified in `docs/todo.md`, not implemented.

## Codec revisions

- The published codec is **v5** (2026-09-01): v4's lineage
  (`dists-0-lf`, epoch 568 → 598) fine-tuned through the three-pass
  clipper, +0.096 dB PSNR against its parent in
  30/30 cells (confounded by 30 extra epochs — it shows v5 is the better
  checkpoint to ship, not why). A codec revision is not a
  `PROTOCOL_VERSION`.
- **Bumping the revision** touches `sstvae/checkpoint.py`'s
  `DEFAULT_FILE`, `native/core/checkpoint/checkpoint.hpp`'s
  `DEFAULT_REVISION`, **`GRAD_REVISIONS` in both**, and
  `native/android-app/CMakeLists.txt` (which pins the bundled fp16
  artifacts by sha256). Nothing cross-checks the list — grep for the
  old revision string. A missing `GRAD_REVISIONS` entry reads as an
  unpublished artifact (the C++ one is a fixed-size `std::array`, so
  there a miss is a compile error).
- Artifacts are `vN-{encoder,decoder}-{fp32,fp16,int8}.onnx` plus
  `vN-decoder-grad-fp32.onnx`, fetched per part on first run. fp16 is
  the default. The gradient graph is fp32 whatever `--precision` says.
  Two name traps that end in a wrong picture rather than an error:
  `-decoder-` is a substring of `-decoder-grad-`, and a sibling must be
  rebuilt as `{stem}-{part}-{precision}`, not substituted into.
- Latent optimization (`sstvae_encode.py --optimize [SECONDS]`,
  `sstvae/latent_optim.py`, torch-free) must optimize **through the
  differentiable channel**, at a constant 5 dB objective SNR (flat from
  2.5 to 7.5 dB; err toward assuming a worse channel) — the clean
  objective is harmful. No L2 regularizer. The gain survives every
  decoder precision and is anti-correlated with encoder quality, so
  re-measure it on every revision. The 20 s default buys ~65% of what
  is achievable. Comparing the C++ and Python numerically needs a
  640×480 PNG (resampling and JPEG decoding differ above the modem).

## Gotchas learned the hard way

- `dsp.to_baseband` is deliberately **unfiltered**: a selective FIR
  smears past the 32-sample CP. The 160-sample demod correlation nulls
  the image; only sync filters.
- The timing tracker must be heavily smoothed: the raw pilot slope sees
  multipath group delay, while real clock drift is < 0.1 sample/frame.
- PAPR is envelope (PEP): clip the analytic-signal magnitude; measure
  with `dsp.papr_db`.
- The local GPU is ROCm (`torch.cuda.is_available()` is true); never
  add CUDA-only dependencies. Torch is for training only (installs
  ~263 MB instead of ~555 MB); `[tool.uv.sources]` pins `dev` to CPU
  torch because tests `importorskip` it as the reference, and the
  `conflicts` block in `pyproject.toml` is load-bearing.
- **SNR is quoted in a 2500 Hz bandwidth** — one constant,
  `config.SNR_REF_BW_HZ`, used by both `hfchannel.awgn` and
  `modem._estimate_snr_db`; never hardcode a bandwidth in either.
  Figures from before 2026-07-26 read 0.79 dB low. The noise in
  `latent_channel`/`waveform_channel` has no bandwidth and is
  deliberately untouched.
- **Never hold the `RingBuffer` lock across a bulk copy.** A blocked
  audio callback discards input: the old `snapshot()` tore growing holes
  that cost 5 dB while every frame still synced. `write()` publishes two
  integers under the lock; `tests/test_rx_ringbuffer.py` guards p95
  write latency.
- **A PortAudio callback written in Python needs the GIL** on the
  realtime thread, and JACK has no buffer to hide a stall — samples are
  lost silently, tracking `poll_interval`. QtMultimedia (pull-based)
  fixed it; PortAudio's blocking `stream.read()` corrupts the heap on
  JACK (`malloc(): invalid size`), so it is no alternative. `audio.warn_if_fragile_host` warns.
  Diagnose sample loss with two simultaneous captures of one playback
  (`scripts/diagnose_capture.py --out` beside the app's
  `receive.save_audio`): correlation 1.000 at a drifting lag means loss,
  and the interval between lag steps names the culprit.
- Capture opens at the device's own rate and resamples in our code (JACK
  cannot resample). `samplerate` in the audio API is the ring buffer's
  rate — always `FS` — not a device setting.
- **Capture resampling is stateful**: `audio.StreamResampler`, never a
  per-chunk `resample_poly` (edge transients and rounding cost 4.7 dB on
  real audio while reporting 440/440 frames).
- **Capture and playback need inverse ratios**: use
  `audio.resample_ratio(src, dst)`. A shared helper once sent a 32 s
  transmission as 0.9 s of noise. Only devices that reject 8 kHz take
  either resampling path (a K4 does; PulseAudio's default does not), so
  `tests/test_audio.py` fakes one.
- `wavio.read_wav` scales integer samples **before** the stereo mixdown.

## Docs

Read the doc before working in its area.

- `docs/native-app.md` — the C++/Qt design, phases, packaging and
  signing traps.
- `docs/android.md` — the Android design and implementation notes (see
  also `native/android-app/README.md`).
- `docs/onnx.md` — the ONNX runtime path and quantisation: int8 accuracy
  comes from leaving the worst layer at fp32 (`per_channel` is a silent
  no-op — `ConvInteger` is per-tensor only), and quantisation must be scored off-distribution.
- `docs/latent-optimization.md` — transmit-time latent optimization.
- `docs/cyclic-prefix.md` — what the CP is and why the carriers sit
  where they do.
- `docs/latent-mixer-results.md`, `docs/slot-domain-precoder.md` — PAPR
  experiments; the precoder is not implemented.
- `docs/gui-review.md` — the 2026-08-07 look-and-feel pass; §7 is this
  fork's palette addendum.
- `docs/todo.md` / `docs/todo-done.md` — open work with its reasoning,
  and the records of what closed.
- `docs/transmit-text-palette.md` — **this fork's own**: the text-palette
  plan. Its wiki `Home` entry is deliberately not written — a fork
  cannot push to upstream's wiki, and its own starts empty — and comes
  back if the branch is contributed upstream.

## The wiki

Most of what was in the README is in the GitHub wiki, a **separate git
repository** (`https://github.com/arodland/SSTVAE.wiki.git`) with no API
— `gh` and the REST tools cannot see it — and nothing checking it: the
project's only prose with no test behind it. Clone it to read or edit.
In a sandboxed session the push returns 403: commit in the clone and
say plainly that it needs pushing — never report it as published.
**Renaming a page breaks the README** (no redirects): change both in one
sitting.

What makes each page stale:

- **Command-line-tools** — encode/decode/listen flags, the
  `pyproject.toml` extras and their sizes, what `--model` accepts.
- **Channel-simulator** — `sstvae_simulate.py`'s options and
  `SNR_REF_BW_HZ`.
- **Performance** — regenerate with `scripts/snr_sweep.py` and
  `scripts/late_join_sweep.py`, never by hand; every codec revision
  moves the numbers.
- **How-it-works** — a waveform table **hand-copied from `config.py`**
  (24 × 50 Hz at 950–2100 Hz, 20 ms + 4 ms CP, 230 latents + 5 beacon
  chips per frame, 32/64/95 s); copy any change over by hand.
- **Comparison-with-other-modes** — embeds `docs/images/ota-vs-analog.png`
  by raw URL, so moving that file breaks it.
- **Training** — `scripts/train.py` flags, `scripts/export_onnx.py`'s
  artifacts, the Hub dataset.
- **Development** — test commands, `tools/build_native.sh`, the source
  layout, the Qt/CMake build, the generated artifacts. The most likely
  to rot.
- **Home** — the page index and the repo's `docs/*.md`.

Worth reading there: Performance's late-join table, and How-it-works on
"lost the preamble" (beacon rescues it, full quality) versus "tuned in
late" (complete picture, lower fidelity).

## Status

- **Training:** stage 1 is complete (Hub dataset
  `arodland/coco640-sstvae`, 640×480; 320×240 is the minimum input,
  upscaled; cloud jobs via `scripts/launch_job.sh`), stage 2 is
  implemented, and v5 is published. A stage-2 fine-tune starts from a
  good stage-1 checkpoint at `--lr 1e-4`; pre-beacon checkpoints remain
  architecture-compatible. Remaining: evaluation sweeps (PSNR/LPIPS vs
  SNR per mode) and on-air calibration.
- **Desktop app:** one implementation, `native/`. CI builds, signs and
  packages five platforms. Remaining: a real release (each artifact
  installed and launched on a clean machine; expect SmartScreen warnings
  at first — reputation accrues per certificate), an on-air shakedown of
  PTT timing and of latent optimization, and overlay templates (the
  document format is ready for them).
- **Android:** see "Android app". Next: UI work, Tier 2, or the Play
  internal test — a signed upload bundle already exists.
