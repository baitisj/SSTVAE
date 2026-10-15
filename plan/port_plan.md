# QRSSTVAE C++ port plan

## 0. Shape of the port

- **Python stays normative.** The C++ is a second implementation, checked against Python one stage at a time. Channel simulation (`channel.py`, `qrss_simulate.py`) and anything to do with training stay in Python. The simulator becomes the fixture generator for the C++ receiver tests.
- **Five stages:**
  - Stage 0: prerequisites (shared infrastructure, generators, parity harness).
  - Stage 1: transmitter.
  - Stage 2: per-pass receiver, split into 2a, 2b and 2c.
  - Stage 3: live listener, store and association.
  - Stage 4: EM and retro search, and `sstvae-qrss decode`. Required: the maintainer wants no Python in release builds (Jeff, 2026-10-13 05:00Z).
- **When the app stops needing Python:**
  - Sending, after stage 1.
  - Receiving, after stage 3.
  - EM, retro search and the decode tools, after stage 4. From then on a release build contains no Python at all; the Python stays in the repository as the reference and for the simulator and training.
- **Size:** about 23k lines of production C++ and 7k lines of tests and bindings. For comparison, the Qt-free `native/core/` is about 17.5k lines today. The receiver is about 60% of the work.
- **Churn is the main schedule risk.**
  - The transmit side is a format (`FORMAT_VERSION` 2), so it is stable.
  - The Python receiver is not: `acquire`, `track`, `receiver`, `frontend` and `ltrack` had 8 commits on 2026-10-11 and 10-12, and waveform L's receiver landed on 10-12.
  - So stage 2 either waits for a receiver freeze or pins each Python source by hash (§2, stage 2).

## 1. Library layout

### 1.1 Files

All new code is Qt-free and lives under `native/core/`.

| File | Mirrors | Stage |
|---|---|---|
| `util/sha256.{hpp,cpp}` | `qrss_beacon_c/sha256.c` (FIPS-tested), moved into `namespace sstvae::util` | 0 |
| `util/crc32.{hpp,cpp}` | `zlib.crc32` (reflected 0xEDB88320) | 0 |
| `util/half.{hpp,cpp}` | numpy `npy_double_to_half` / `npy_half_to_double`: binary16 round-half-even, taken directly from double | 0 |
| `util/rational.hpp` | `fractions.Fraction`: int64 num/den, `limit_denominator(1<<20)`, `"num/den"` parse and format | 0 |
| `util/numpy_sum.hpp` | numpy `pairwise_sum` (blocks of 128, 8 accumulators) | 0 |
| `io/npy.{hpp,cpp}`, `io/npz.{hpp,cpp}` | `np.save`/`np.savez`/`np.load`: dtypes `<f2 <f4 <f8 <c8 <c16 |i1 |u1 <i8 <u2 |b1`, 0-d arrays, ZIP_STORED with zip64 local extras. `testing/npy.hpp` becomes a thin wrapper. | 0 |
| `io/json_lenient.{hpp,cpp}` | `json.dumps`/`loads` with `NaN`/`Infinity`/`-Infinity` tokens (needed for PassResult meta) | 2a |
| `qrss/config.hpp` (generated) | `constants.py`, every frame preset, domain strings, `polar.INFO_SET`, `morse.MORSE`; from stage 2, the receiver calibration constants | 0, then 2 |
| `qrss/tables.cpp` (generated) | frozen float tables: `ce.PULSE_TABLE`[7761], `lwave.PULSE_TABLE`[15521], `_FE_TAPS`[255], the 321-tap Kaiser(10) decimator, the 481-tap Kaiser(12) interpolator, the 2001-tap notch, the CE/L MF taps [121]/[243] | 1, then 2 |
| `qrss/sequences.{hpp,cpp}` | `sequences.py` (bit streams) | 1 |
| `qrss/slot.{hpp,cpp}` | `sequences.quarter_hour_count`/`parse_utc`/`parse_slot`/`quarter_hour_start`, `live.utc_iso` | 1 |
| `qrss/frame.{hpp,cpp}` | `frame.py` + `lframe.py` as one `FrameSpec` with a `waveform` field, a `Layout`, `pair`/`unpair` | 1 |
| `qrss/precoder.{hpp,cpp}` | `precoder.py` | 1 |
| `qrss/morse.{hpp,cpp}` | `morse.py`. Its own validation; it does **not** share `dsp/morse.cpp`'s lenient table. | 1 |
| `qrss/polar.{hpp,cpp}` | `polar.py`: encoder in stage 1, CA-SCL decoder in stage 2a | 1, 2a |
| `qrss/header.{hpp,cpp}` | `header.py`, reusing `beacon::crc16` and the beacon alphabet | 1 |
| `qrss/picture.{hpp,cpp}` | `picture.py`, including `.qrsp` I/O | 1 |
| `qrss/beaconfile.{hpp,cpp}` | `beaconfile.py` | 1 |
| `qrss/pulse.{hpp,cpp}` | helpers shared by CE and L, parametrised by T in grid steps (485 or 970): `_rrc`, `_grid_sum`, `_turns_to_phasor`, `_hann_int`, `_ramp_envelope`, MF tables | 1, 2a |
| `qrss/ce.{hpp,cpp}` | `ce.py`: transmit subset in stage 1; `phase_at`, `var_grid`, `mean_template`, `matched_filter` in stage 2a | 1, 2a |
| `qrss/lwave.{hpp,cpp}` | `lwave.py`, without welch/quantile statistics | 1, 2b |
| `qrss/tx.{hpp,cpp}` | `tx.py`, as a streaming `PassAudio` | 1 |
| `qrss/types.{hpp,cpp}` | `types.py`, including `PassResult` npz save/load | 2a |
| `qrss/frontend.{hpp,cpp}`, `qrss/chan.{hpp,cpp}` | `frontend.py` without `PassbandStore`; `track.ChanCapture`/`make_chan` | 2a |
| `qrss/track_core.{hpp,cpp}` | the waveform-neutral half of `track.py`: PathFn, derotate, `symbol_mf`, `psi_at`, `rx_index`/`_tau_of_rx`, templates, `kalman_rts`/`select_q`, `_recenter`, `z_ref_stat`, `erase`, `report` | 2a |
| `qrss/demod`, `qrss/cwid`, `qrss/render` (`decoder_planes` only) | `demod.py`, `cwid.py`, `render.py` | 2a |
| `qrss/track.{hpp,cpp}` | CE orchestration: `fit_timing`, `refine_timing`, `refine_path`, `line_path`, `verify`, `verify_track`, `track` | 2b |
| `qrss/ltrack`, `qrss/ldemod` | `ltrack.py`, `ldemod.py` | 2b |
| `qrss/acquire`, `qrss/receiver` | `acquire.py`, `receiver.py` | 2c |
| `qrss/store`, `qrss/associate`, `qrss/passband_store`, `qrss/dial_log`, `qrss/live` | `store.py`, `associate.py`, `frontend.PassbandStore`, `live.DialLog`, `live.py` | 3 |
| `qrss/em` | `em.py` | 4 |
| `math/bspline_lsq` | scipy `make_lsq_spline(k=3)` using banded Givens QR. It must reproduce non-finite coefficients on spans with no data, because those trigger `PathFn`'s line fallback. | 2a |
| `math/cubic_spline` | scipy `CubicSpline`, not-a-knot, complex | 2c |
| `math/special` | Cephes `i0e`; integer-shape `gammaincc` in closed form; `norm.isf`; `gamma.logsf` (stage 4) | 2a, 2c |
| `math/np_stats` | numpy semantics for `median` (dtype-preserving mean of the two middle values), selection, `interp`, first-index `argmax` | 2a |
| `math/ndimage` | `median_filter(mode='reflect')`, `maximum_filter1d` | 2a, 2c |
| `dsp/upfirdn.{hpp,cpp}` | scipy `upfirdn`: real taps, real or complex input. Copy the loop order of `_upfirdn_apply.pyx`. | 1 |
| `dsp/dsp.hpp` additions | a public `firwin(numtaps, cutoff, fs, Window{hamming, hann, kaiser(beta)}, pass_zero)` exposing the existing `firwin_band`; `resample_poly` on complex input with a window argument; `oaconvolve_same`; `fftconvolve_same`; `freqz(h, worN, fs)` | 1, 2a |

### 1.2 Generated and committed artifacts

These follow the `config.hpp` rule: generated, committed, and gated in CI by `--check`.

- **`tools/gen_qrss_config_header.py`** writes `qrss/config.hpp` and `qrss/tables.cpp`.
  - It imports `tools/gen_qrss_tables.py`'s preset and domain rendering read-only, so the C beacon's header and the native one cannot disagree.
  - Values derived from `math.exp` (A0, K_LIN, CARRIER_FRAC, USEFUL_FRAC) are emitted as `float.hex` literals.
  - Rationals are emitted as num/den pairs: T 485/16000, 970/16000; ALPHA 3/20; BETA 4/5; L_PILOT_HZ 12207/970; L_EDGE_HZ 920/97; L_PILOT_FRAC 3/20.
  - The Python asserts become `static_assert`s: `T_DEN % FS == 0`, `FE_FS == CH_FS*CH_DECIM`, CW 8+176+8 == 192, `L_PILOT_HZ == 12207/970`.
  - The header also carries `INFO_SET` together with a C++ check of its SHA-256 (4e064eee…).
  - Presets cover PRESETS, LEGACY (v1) and PROTOTYPES, plus `l-full/medium/short/tiny`, with their names verbatim.
  - Stage 2 adds a "receiver calibrations, not format" section generated from the Python modules, never retyped: `D_PASS`/`D_REF`, the `KAPPA_*` constants, `KAPPA_SELF(_KNOWN)`, `Q_SPREADS_HZ`, `G_*_L`, `PILOT_GAIN_*`, `D_*_L`, `L_KEYED_POWER` by frame name, `TBD_GUMBEL_*`, and the A1 threshold for M=11 (28.296115129334837, frozen rather than computed by `gammainccinv`).
- **`tools/gen_qrss_golden.py`** writes `native/tests/golden_qrss/` (`.npy` plus `manifest.json`).
  - It reuses `gen_golden_vectors.Corpus` by import. It does **not** edit the SSTVAE corpus, because design.md rule 3 makes that off-limits.
  - Bitwise entries carry a SHA-256 in the manifest. Tolerance entries carry a fingerprint, as `PLATFORM_TOL` does today.
  - Golden *inputs* are stored rather than regenerated, because `synthetic_full_latents` uses PCG64.
- **Tables are frozen data, not computations.** `ce.PULSE_TABLE` is computed at import through libm `sin`/`cos`. Freezing it makes the C++ table bit-identical to Python's on the generating machine, which is the "format constants are frozen data" rule. The four tables come to about 27k doubles, or about 0.7 MB of source; for comparison, `interleaver_table.hpp` holds 151,800 entries.
- **Not frozen:** the MF tables at `MF_RES`=512 (63,489 / 125,953 / 249,857 entries). They are receive-only and tolerance-class, so they are computed at run time.

### 1.3 CMake

- **New option `SSTVAE_BUILD_QRSS`**, default ON, with no external dependencies. It exists so the Android build can stay out until it is wanted.
- **New library `sstvae_qrss`** (STATIC, PIC):
  - Sources: `core/qrss/*`, `core/math/*`, and the QRSS-only `dsp` additions.
  - Linkage: `target_link_libraries(sstvae_qrss PUBLIC sstvae_core)`.
  - The new `util/` and `io/` files go into `sstvae_core` itself, since `checkpoint` and future code can use them.
  - Compile flags: `target_compile_options(sstvae_qrss PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-ffp-contract=off>)`. Today the build has only `-fno-fast-math`. Clang on arm64 (macOS, Android) contracts `a*b+c` by default, which would break bitwise `assemble`, the Kalman/RTS recursion and the WHT. MSVC `/fp:precise` does not contract.
  - It must be declared **before** `add_subdirectory(tests)` (line 635). Tests key on `if(TARGET sstvae_qrss)`. This avoids the recorded "if() on unset var is silently false" trap.
- **Apps:**
  - `native/apps/sstvae_qrss.cpp` builds `sstvae-qrss` with subcommands `encode`, `beacon`, `transmit` (stage 1), `receive` (2c), `listen --wav` and `decode --key/--list` (3).
  - It links `sstvae_codec` when that exists. Without it, `encode` and rendering print "built without the codec" and exit 2.
- **Bindings:**
  - A `qrss` submodule in `bindings/module`, as a separate `qrss_bindings.cpp` compiled into `sstvae_native` when `TARGET sstvae_qrss` exists.
  - It exposes `__qrss_stage__` (1, 2 or 3).
  - `__sstvae_abi__` stays 1, because adding a submodule does not change existing ones.
- **ctests**, each with a `TIMEOUT` property: `qrss_golden`, `qrss_formats`, `qrss_tx`, `qrss_c_oracle`, later `qrss_frontend`, `qrss_track`, `qrss_receiver`, `qrss_store`, `qrss_live`.

### 1.4 Layering rules (added to `tools/check_layering.py`)

- `core/qrss/`, `core/math/` and `core/io/` include **no Qt header at all**, QtCore included. Today's regexes miss QtCore; a QtCore include would only surface as a link error, because `sstvae_core` links no Qt.
- **Format files** may not include `<random>` or call `rand`/`srand`/`mt19937`/`default_random_engine`. That covers `sequences`, `slot`, `frame`, `precoder`, `morse`, `polar`, `header`, `picture`, `beaconfile`, `pulse`, `ce`, `lwave` and `tx`. This is the C++ form of `test_qrss_frozen.py`'s AST rule.
- `core/qrss` may depend on `core/{dsp,framing,beacon,latents,images/types.hpp,rx/ringbuffer.hpp,util,io,math,log}`. It never includes `core/codec/`, and `core/codec` never includes `qrss`.
- The GENERATED-banner rule extends to `qrss/config.hpp` and `qrss/tables.cpp`.
- `check_includes.py` adds `<cctype>` (isspace/isdigit), `<numeric>` (gcd) and `<bit>` (bit_cast) to `SYMBOLS`. All three are used by this code and none is mapped today.

### 1.5 The codec, onnxruntime and `--no-codec`

- **Only two QRSS operations touch the codec.** Both are `std::function` seams, the same pattern as `rx::Decoder`:
  - `qrss::Encoder = std::function<std::vector<double>(const images::ImageArray&)>`: the picture becomes `.qrsp` latents.
  - `qrss::Renderer = rx::Decoder`: `decoder_planes(S, W, mode)` becomes a picture.
- **`OnnxCodec` gains `std::optional<std::string> source_sha256(std::string_view part)` and `std::optional<uint16_t> codec_id(part)`** (v5 is 0xD1D8). `codec.cpp` already reads `sstvae.source_sha256` internally. A model without the metadata still needs an explicit `--codec-id`, as `qrss_encode.py` requires.
- **What a `--no-codec` build still does.**
  - All of `sstvae_qrss` builds and all of its ctests run: transmit from `.qrsp`/`.bin`, receive to `PassResult`, store and association, and the listener with a stub renderer.
  - Codec-marked pytest parity skips unless `SSTVAE_REQUIRE_CODEC=1`. That covers the picture ID from a PNG and the byte-identical tile PNG.
- **The lazy session map in `OnnxCodec` is unsynchronised.** In stage 3 the listener renders on a worker while the transmitter may encode, so the codec gets a mutex or a second instance.

## 2. Stages

### Stage 0: prerequisites (about 1.3k C++, 0.9k Python)

**Modules**
- `util/sha256`, `util/crc32`, `util/half`, `util/rational`, `util/numpy_sum`, `io/npy`, `io/npz`.
- Both generators.
- The pybind `qrss` submodule skeleton.
- The test harness:
  - `tests/qrss_native.py` holds a `QRSS_NATIVE_SUBSTITUTIONS` table. Each row is `(module, attr, native_attr, stage)`, plus adapters.
  - A new pytest option `--native-qrss=tx|rx|live` substitutes every row up to that stage.
  - A row naming a native attribute that does not exist is an **error**, never a skip.
  - The rows are kept out of `NATIVE_SUBSTITUTIONS`, which rule 3 makes off-limits, so `--native` keeps its exact current meaning.

**Parity**
- SHA-256: FIPS vectors, including one million 'a' fed in uneven pieces, and `hashlib` at the 55/56/64-byte padding edges.
- Half: all 65,536 binary16 patterns round-tripped, plus about 1e6 doubles against `np.float16`: ties, subnormals, overflow to inf, and the values float32 would double-round.
- `numpy_sum`: bitwise against `np.sum` and `np.mean` on 1e4 random float64 vectors of length 50,600, plus every length from 1 to 300.
- npz:
  - Python writes a file and C++ reads it, and the reverse, for every dtype listed in 1.1.
  - The fixtures include numpy's `force_zip64` local headers (sizes 0xFFFFFFFF) and 0-d `<i8`.
  - `np.load(allow_pickle=False)` must accept C++ output.
- Rational: `limit_denominator(1<<20)` against `Fraction` on 1e5 doubles in 300–2700 Hz, plus 1500.3 → 15003/10 and 1234.5678 → 6172839/5000. int64 is enough: in that band a double's exact fraction has a denominator of at most 2^44.

**Environment (this container)**
- pybind11 is not installed. CI gets it from `pip install … pybind11` in `native-build.yml`, so install it into `.venv` the same way.
- A fresh configure otherwise fails at `find_package(pybind11 CONFIG REQUIRED)`. `build_native.sh` only warns.
- `.venv` has onnxruntime 1.31.0 against the 1.28.0 pin in `uv.lock` and `onnxruntime.cmake`. Run `uv sync` before trusting any codec byte-identity result.

**Exit:** the generators' `--check` gates are green, the infrastructure ctests are green, and `pytest --native-qrss=tx` errors cleanly when the module is unbuilt.

### Stage 1: transmitter (about 4.0k production C++, 1.5k tests, bindings and CLI)

**Modules:** `sequences`, `slot`, `frame` (CE and L), `precoder`, `morse`, the polar encoder, `header` (encode side), `picture`, `beaconfile`, `pulse` and `ce` (transmit subset), `lwave` (transmit subset), `tx`, `dsp::upfirdn`, and the public `firwin`.

**Parity classes**
- **Bitwise:** sequences, layout, `assemble` (CE float64 and L complex128, same operation order), precoder, keying, polar encode, header bits, fp16 segments, `picture_id`, int8, beacon-file bytes, `slot_symbols`, carrier turns.
- **Tolerance:**
  - CE phase and audio: 1e-12. `upfirdn`'s summation order and numpy's SVML `cos`/`sin` rule out bitwise.
  - L clipped baseband and audio: about 1e-9, provided `n0 = floor((t_lo − 4 s)·250)`, `nfft = next_fast_len(len + 4·fs)` and the pass order are reproduced exactly. A different FFT padding alone moved the output by 2e-6.
- **Semantic, L only:** C++ makes the signal and Python's `lwave.loopback_stats` measures it. Targets: data_vs_ref 0.880 ± 0.005, SINR 11.85 ± 0.1 dB, PAPR 3.2 ± 0.1 dB, pilot share 0.138.
- **Independent oracle:** a test-only target compiles `qrss_beacon_c/{qrss_ce.c,sha256.c}` as C++. They compile cleanly as C++20; the header has no `extern "C"`, so compile consistently as C++. `qrss_c_oracle` then checks C++ `slot_symbols` against `qrss_ce_symbol` (exact) and `phase_grid` against `qrss_ce_phase` (C2's 2e-4 rad rms and 1e-3 rad max). This works with no Python at all.

**Library API (in-process)**

```cpp
namespace sstvae::qrss {
enum class Waveform : uint8_t { CE = 0, L = 1 };
const FrameSpec& frame(std::string_view name);            // "full", "l-full", "full-v1", ...
struct StoredPicture { int mode; uint16_t codec_id; std::vector<uint16_t> segs; uint32_t picture_id; };
StoredPicture stored_picture(std::span<const double> latents, uint16_t codec_id, int mode);
StoredPicture encode_picture(const images::ImageArray&, int mode, uint16_t codec_id, const Encoder&);
void save_qrsp(const std::string&, const StoredPicture&); StoredPicture load_qrsp(const std::string&);
using Source = std::variant<StoredPicture, BeaconFile>;
struct PassSpec { uint64_t q; std::optional<int> segment; util::Rational carrier_hz; const FrameSpec* frame;
                  std::optional<HeaderFields> header; double lead_in_s = 0; std::optional<bool> ook;
                  double pre_s = 12, post_s = 3, amplitude = 0.5; };
class PassAudio {            // CE: any block from its closed form; L: builds the 250 Hz clipped frame in the ctor
 public: PassAudio(const Source&, const PassSpec&); size_t size() const;   // == tx.slot_samples
         void render(size_t i0, std::span<double> out) const; };
std::vector<double> transmit_audio(const Source&, const PassSpec&);
struct Refusal : std::invalid_argument { using invalid_argument::invalid_argument; };  // Python's ValueError text
}
```

**App swap**
- `qrss_tx::run`'s `Runner` (a `QStringList` argv plus exit code) becomes a typed `Backend { encode(Picture, mode) -> StoredPicture; make_pass(StoredPicture, Pass, Request) -> std::vector<double>; }`.
- `native_backend()` is the default. `python_backend()` keeps today's QProcess path behind `SSTVAE_QRSS_BACKEND=python` for A/B (see question 5).
- `on_error(std::string)`, `problem()`, the `Chain` logic and `TxEngine::transmit_wave` are unchanged.
- `find_tools()` gating becomes "codec loaded".
- In-process audio skips the float32 WAV round trip. It therefore differs from today's app output by float32 rounding (about 6e-8 relative), which strictly improves on it.
- The hand-copied `PASS_S`=1782.7 (actually 1782.678125), `AUDIO_LEAD_S`, `AUDIO_TAIL_S`, `T0_OFFSET_S` and the 300–2700 Hz band come from `qrss/config.hpp`. So do `MARK_L_BELOW_HZ` and `ABOVE_HZ` in the spectrogram.

**CLI:** `sstvae-qrss encode|beacon|transmit` with Python's argv, stdout lines, refusal strings and exit codes. `test_qrss_beacon.py` and `test_qrss_cli.py` then parametrise `run()` over {script, native subcommand}.

**Exit criteria**
1. Every golden vector is equal.
2. `pytest --native-qrss=tx` is green on frozen, format, header (encode tests), spread (transmit tests), ce, lwave, beacon, c_ref, channel, lchannel and smoke.
3. Mixed chain: C++ transmit, then Python `simulate_audio` at −6 dB, then Python `receive_slot`, decodes the header and matches the callsign window (Z > 20) on SHORT, for both CE and L.
4. `test_qrss_tx.cpp` is rewritten on the typed seams and green.
5. The app sends with `.venv` renamed away.
6. Loopback through the null-sink recipe: `sstvae-qrss transmit --frame tiny` played and captured, then `qrss_receive.py --frame tiny`, decodes. The pass is 53 s. The FULL on-air pass is a manual check by the owner.

### Stage 2: per-pass receiver (about 12.3k production C++, 2k tests and bindings)

**Staleness pin.** Every Python module that stage 2 mirrors is hashed (SHA-256 of the source) into `native/tests/golden_qrss/rx_pins.json`. A fast test fails with "Python receiver changed; regenerate references" when a hash moves. That is cheap staleness detection without a long rerun.

**2a: foundations, tested with Python's intermediates injected (about 5.9k)**
- Modules:
  - Polar CA-SCL list-8 decoder and `header.decode` (version fallback).
  - `types` and `PassResult`.
  - `frontend`: `audio_to_fe` in streaming form (ALIGN 16, HALO 128, as `live.StreamFE`), `blank`, `normalise`, `channelise`/`channelise_keep` on the 1/64 Hz grid with floor-mod n0, and `noise_psd` with the D43 flat/skirt masks.
  - `chan` (`make_chan`).
  - The receive part of `ce`.
  - `track_core`, `demod`, `cwid`, `render.decoder_planes`.
  - Math: B-spline, `i0e`, numpy stats, ndimage, complex `resample_poly`, `oaconvolve_same`, `freqz`.
- Parity:
  - `audio_to_fe` and `StreamFE`: complex64, with at most 1e-5 of samples allowed to differ by 1 ulp. The measured rate is 1 in 240k.
  - `blank`, `normalise` and `running_median`: bit-equal. This needs numpy's complex/float32 division, which is multiply by the reciprocal; per-part division differs in 43% of samples.
  - `channelise`: complex64 to rtol 1e-6, with `f_mix` exactly rational.
  - `noise_psd` masks: exactly equal over every 1/64 Hz position in 300–440 and 2560–2700 Hz. The PSD itself to rtol 1e-9.
  - Polar decoder: on 2,000 noisy LLR vectors, the decoded info bits are equal and the path metrics agree to 1e-9 relative. Ties go to the decision-0 candidates first, by stable sort.
  - Tracking stages fed Python's inputs: `kalman_rts` bitwise given identical (mu, R, q, Pu); `select_q` picks the same index; `symbol_mf`, `templates` and u/P to 1e-10.
  - `demod.extract`, `ldemod` and `block_estimate`: float32 within 1 ulp, and ≥99.9% bit-identical.
  - `cwid` on 10k synthetic LLR stacks: `read_ml` text equal, and margin within 1e-9.
  - `decoder_planes`: float32 bit patterns equal.
  - `PassResult`: files written by each side load in the other, and every field is equal. `uid` is never compared across implementations.
- Fixtures:
  - Simulated passes generated in pytest (`channel.simulate`, about 1 s for SHORT) and handed over by pybind.
  - A small committed TINY set, PassResult npz of about 150 KB each, for ctest.

**2b: timing and waveform L (about 2.9k)**
- Modules: `fit_timing`, `refine_timing`, `refine_path`, `line_path`, `verify` and `verify_track`; `ltrack`; `ldemod`.
- `fit_timing(_l)` uses the **direct sparse-gather correlation**. This is Python's own `nlag ≤ 64` path, applied to all 1,255 lags. It is about 10–20× cheaper than Python's 588 FFT pairs of 2^19 on FULL, and equal to the FFT path up to rounding.
- Parity: timing within 1e-6 CH samples, the same argmax cell and runner-up set, the same Newton branch and `None` outcomes, z_ref and resid_ratio to 1e-8. Tests avoid near-threshold scenarios.

**2c: acquisition and orchestration (about 3.5k, plus the CLI)**
- Modules:
  - `acquire`:
    - A1: 16000-point Hann spectrogram with float32 CFAR and the frozen threshold.
    - A2: einsum search, CubicSpline and phase-line refinement, and the integer-k Gamma tail.
    - A3: per-channel float32 Viterbi, so Android does not need 200 MB; shape tests; L pilot re-anchoring.
    - Arbitration.
  - `receiver`: `prepare`, `detect`, the joint detect, `receive_pass` rounds A and B, `decode_header`, merge/ghost/alias logic, `confidence_db`, `capture_from_wav`.
  - The receiver keeps `std::function` seams for `_gate`, `verify`, `_ce_resid_ratio`, `receive_pass` and `prepare`, so `test_qrss_lreceiver.py`'s stubbed scenarios run against C++.
  - The CE-only path and the joint path stay separate code. Budget accounting is copied exactly, including `taken += 1` before the merge skip.
- Parity:
  - `CarrierStats.stat`: float32, with a few 1-ulp flips allowed.
  - `carrier_lines`: bins and drifts exact.
  - A2: Λ grid to rtol 1e-9; same cell; `f_hz` to 1e-7 Hz; `tau0_s` to 1e-6 s.
  - Viterbi paths exact; `detections()` in the same order, with the same method and waveform.
- Statistical acceptance:
  - About 12 seeded scenarios: SHORT and TINY, CE and L, −6/−12/−20 dB, drift, ±100 ppm, a neighbour, a lead-in.
  - Compare against committed **Python reference summaries**: pass count, header, cw text, `f_hz` within 0.01 Hz, tau0 within 0.5 CH samples, latent SNR against truth within 0.1 dB.
  - The summaries are produced by `tools/gen_qrss_rx_reference.py`, run per scenario (10–15 s each), never as a test.
  - Parity runs pass `budget_s=None`, because the live 180 s / 12-candidate budget makes results depend on CPU speed.
- API:

```cpp
struct Capture { std::vector<std::complex<float>> fe; uint64_t q; double t0_index; int fs = 4000; };
Capture capture_from_audio(std::span<const double> x8k, uint64_t q, double start_unix);
struct ReceiveConfig { std::vector<const FrameSpec*> frames; bool live_only = false; Estimator est = Estimator::Joint;
                       std::vector<std::pair<double,double>> erase_s; bool round_b = true;
                       std::optional<int> max_candidates; std::optional<double> budget_s; std::vector<Hint> hints;
                       bool old_edge_noise = false, old_gating = false; };   // see question 7
std::vector<PassResult> receive_slot(const Capture&, const ReceiveConfig&, const ReceiverSeams& = {});
```

- CLI: `sstvae-qrss receive` with `qrss_receive.py`'s flags and report lines. Tests grep `header     K1ABC FN42 picture 12345678`, `Hz at t0`, `mean W` and `Z_ref`. It exits 1 when no pass is found. Stage 2 supports `--no-store` only.
- The app is unchanged in stage 2, because it never calls `qrss_receive`.

**Exit criteria**
- `pytest --native-qrss=rx` is green on the fast tier of frontend, acquire, lacquire, track, ltrack, ldemod, cwid, lunits, receiver, lreceiver, rules, spread and smoke.
- Statistical acceptance is green.
- The A-7 false-alarm calibration has been **re-checked against the C++ statistic**: 200 noise captures, which only fits natively.
- The mixed chains pass in both directions.

### Stage 3: live listener, store and association (about 5.0k production, 1.4k tests)

**Modules**
- `store`: passes npz, acc JSON+npz, prov, `log.jsonl`, twin, `due`, merge, expire, `rebuild`, `loo`.
- `associate`: rules 1–4, keeping the rule strings (they are a GUI contract).
- `passband_store`:
  - Raw `fe_<hour>.i16`, `fe_<hour>.json` and `meta.json`.
  - Reading Python-packed `.flac` through vendored `dr_flac`, from the same family as `dr_wav`.
  - Packing finished hours to `.flac` with a small FLAC encoder in `core/qrss` (fixed predictors, Rice coding, verbatim fallback), so the store stays the size the Python listener keeps it; checked by decoding with soundfile and dr_flac to the identical samples.
  - No packing (question 9).
- `dial_log` reader.
- `dir_lock`, interoperable with Python: `flock(LOCK_EX|LOCK_NB)` on `<dir>/.lock` on POSIX, and byte-0 `LockFileEx` on Windows to match `msvcrt.locking`. It is **not** QLockFile.
- `live`:
  - `StreamFE`, `FeRing`, `capture_read`/`capture_prepare`. The noise fill is a seam; default is a C++ generator, and tests inject Python's `default_rng(q)` noise.
  - The scheduler.
  - Tiles.
  - The `state.json` writer, with the same schema and no NaN tokens.
  - `tiles/<id>.png` via `images::save_png`.
  - `listener.log`, rotated at 2 MB.
  - `recover`/`backfill`.
- `render_accumulator` and `qrss_decode --key/--list`.

**Concurrency**
- The GUI pump stays cheap: `feed()` does StreamFE and a ring write.
- One scheduler thread, one live worker and one low-priority finish worker (`setpriority`, or `THREAD_PRIORITY_BELOW_NORMAL` on Windows) replace the spawned process.
- Hints, `s.dets` and `s.cands` are snapshotted under the lock. Python reads them outside it.
- Render and associate run outside the lock.
- The store gets a mutex.
- Module tables are `call_once` or immutable.

**App swap**
- A `QrssReceiver` interface with `ProcessReceiver` (today's) and `InProcessReceiver`.
- Both write the same `state.json` and tiles, so `QrssWindow`'s 1 s polling and the 915-line `test_qrss_window.cpp` fixtures are unchanged.
- In-process feeding uses `RingBuffer::read_since` with **capture timestamps**, so there is no Stamper. It detects `total < cursor` on ring replacement as a gap.
- `DialLog` changes go in directly, and the file is still written for terminal-run listeners.

**Parity**
- Bookkeeping, exact: `test_qrss_live`'s stub-receiver scenarios are rewritten as ctests in the `test_rx_engine.cpp` style. They cover merge by band centre, waveform switch, ghosts, the headerless SNR cut, hints with a USB dial shift, dial-epoch finishes, recover/backfill and provisional pictures. The same scripted `(feed, step)` sequence goes through Python with the same stubs, and the call logs and tile fields are compared key by key.
- Store, exact:
  - Python writes a store and C++ rebuilds every accumulator to S/W float32 bit-identical, and the reverse.
  - The merge digest is equal.
  - `due()` and `expire()` agree under a fake clock.
  - Re-run `test_qrss_multipass.py`'s P1/P2/P6 and rules 1–4 against a C++ store through a facade.
- End to end, tolerance: a TINY slot replay (half heard, then whole). Tile `f_hz` within 0.1 Hz, `snr_db` within 0.3 dB; callsign, picture ID and segment equal.

**Exit criteria**
- The app receives with `.venv` renamed away.
- `qrss_decode.py --list/--key/--em/--retro` and `qrss_receive.py` work on a store the C++ wrote, and the reverse.
- A terminal-run Python listener is refused by the `.lock` while the app listens.

### Stage 4: EM, retro search and decode (about 2k)

Required since 2026-10-13 05:00Z: the maintainer would rather have no Python in the release builds, so nothing the app or a release CLI does may run Python.

- **`em.py`**, which is CE-only and offline, ported whole: `lmmse_latents`, `prior_from`, `em_prior`, `acc_mean_w_db`, `known_header`, `known_keying`, `pass_spec`/`pass_path`, `rereceive` (with `split=True`, the default), `em_refine` (LOO EM, `EM_ROUNDS`, `EM_TOL_DB`), `template_z`/`_gauss_z`, the candidate set, `_chunk_template`, `_search_chan`, `_search_input`, `_from_passband`, `template_search`, `receive_template` and `retro_detect`. L members are left as Python leaves them (no re-receive, no L search); when Python gains L, the port follows.
- **`math/special`** gains `gamma.logsf` (integer shape, closed form) and `norm.isf`, accurate far into the tail: `template_z` turns a Bonferroni-corrected p-value of order 1e-12 or smaller into Z, and the Z > 6 gate sits right there.
- **`sstvae-qrss decode`** replaces `qrss_decode.py`: `--list`, `--key`, `--em`, `--retro`, the same stdout, refusals and exit codes, on a store either implementation wrote.
- **App API**: `em_refine(store, key)` and `retro_detect(store, key, passband, spec)` as typed calls with progress and cancel, for the UI thread to wire, Qt-free like the transmit API.
- **Needs from stages 2 and 3**: `receive_pass(prior=, return_tracks=, f_mix_hz=)`, the store (`update_pass`, accumulators, `canonical_index`) and the passband store reader (raw `.i16` and packed `.flac` hours).
- **Exit criterion**: the packaged app sends, listens, decodes, refines (EM) and searches back (retro) on a machine with no Python installed.
- **Parity**: `rereceive`, `em_refine` rounds and `template_search` cells within the stage 2 tolerances on oracle-injected fixtures; `template_z` against scipy to 1e-12 relative over the range the gate uses; `retro_detect` finds the same passes with Z within 0.05 on 6 SHORT/TINY fixtures (detections and misses agree); and the statistic's false-alarm calibration rechecked on the C++ statistic (noise captures, as A-7) because `TBD_GUMBEL_*` and the A2 thresholds are calibrated on Python's.

## 3. Stage 1 task list

Do these in order, one module per task, each green before the next.

1. **Constants generator.**
   - Write `gen_qrss_config_header.py`, producing `qrss/config.hpp` and `tables.cpp`, with a CI `--check`.
   - A pybind test walks every public name in `sstvae.qrss.constants`: floats compared by `float.hex`, rationals as (num, den), and presets compared on every derived length.
   - Expected values:

     | Frame | n_sym | n_pos | Duration (s) |
     |---|---|---|---|
     | full | 57274 | 58810 | 1782.678 |
     | medium | 20777 | 21545 | — |
     | short | 7670 | 8438 | — |
     | tiny | 1753 | 1753 | — |
     | l-full | 28637 | 29405 | — |
     | l-short | 3834 | 4218 | — |

   - Also check that the `INFO_SET` SHA-256 is 4e064eee… and that the tables match `ce.PULSE_TABLE` / `lwave.PULSE_TABLE` bitwise.
2. **`util/sha256`.** FIPS vectors, padding edges, `hashlib` parity (stage 0 checks, re-run here).
3. **`sequences` + `slot`.**
   - `sha_bits` for every domain at n ∈ {1, 7, 255, 256, 257, 660, 3539, 50600}.
   - Pinned digests: CE preamble 41deb0c5…, `references_ce(3539)` 715b6ad7…, `scrambler(1954752, 50600)` a6f36677…, L preamble 51917ffc…, L references (1770) 1d490636….
   - The scrambler at q ∈ {1954752, 1954753, 2^40+12345}, with q as a big-endian uint64.
   - `spread_whitener(50600)`.
   - `qpsk` values ±1/√2.
   - `parse_slot` accept/reject over an edge-case table (Z, ±hh:mm, off-quarter-hour, no zone), with the same refusal texts.
4. **`frame`.**
   - Every Layout array equal for every preset, LEGACY and PROTOTYPES included.
   - `pos_of` over all s; `window_spans_s`.
   - `assemble` bitwise on Gaussian x with random 2474 header bits, at hdr_rho 0 and 0.054. Use the same operation order: `out = x; out += sqrt(rho)*h`.
   - L: `(x + √ρ·h)/√(1+ρ)` then `pair` by `/√2`, bitwise; `unpair(pair(v))` within 1 ulp.
   - `get_any`/`twin` name mapping.
   - Validation refusals for a malformed `cw_after`.
5. **`precoder`.**
   - `block_sizes(50600)` = 790×64, 32, 8.
   - `wht`, `precode` and `unprecode` bitwise against Python, with the exact butterfly order (h ascending; x+y then x−y; then `/sqrt(M)`). Inputs: (a) fp16-grid vectors, (b) `from_int8` vectors (q/20, where only the exact order matches).
   - Lengths 1–200 random, plus 1024, 4096, 16384, 50600. Batch rows.
6. **`morse`.**
   - `keying_units` for every one of the 37 characters.
   - `K1ABC/P` digest 46c4fb45….
   - `00000000` takes 173 units.
   - `check_callsign` accept/reject agreement on a fuzzed set: lowercase, inner spaces, 9 characters, '-', '.'.
7. **Polar encoder + `header`.**
   - CRC check value 0xA69D over "123456789". Keep the beacon CRC; do not "fix" it to CCITT-FALSE.
   - Field offsets: callsign 0–48, grid 48–63, picture 63–95, mode 95–97, segment 97–99, codec 99–115, version 115–119, reserved 119–126, CRC 126–142.
   - Natural-order polar transform with no bit reversal.
   - `encode` bitwise for the three test headers and 1e4 random headers.
   - `unpack` agreement on 1e5 random 142-bit vectors plus every single-bit flip.
   - Grid round trip over all 32,400 codes.
8. **`picture`.**
   - `prepare_segment`: RMS via `numpy_sum`; double to half in one rounding.
   - Air maps from `framing::tx_perm`. `never_sent` has 2,200 entries per group.
   - `picture_id` = 0xD349C671 on the stored closed-form tensor (mode A, codec 0xD1D8).
   - `to_int8` at exact ±k.5/20 boundaries, and clip at ±127.
9. **`.qrsp` I/O.** C++ writes and Python `load_qrsp` accepts it (it recomputes the ID); Python writes and C++ loads it; a wrong ID is refused.
10. **`util/crc32` + `beaconfile`.**
    - `to_bytes` byte-identical, 50,970 bytes, for `synthetic_picture(3, mode 1)`: segments 0 and 1, FSK and OOK, grid set and unset.
    - `from_bytes` rejects every single-byte flip, bad magic, a −128 latent, a keying or header mismatch under a valid CRC, and nonzero pad or reserved bits.
    - `qrss_ce_init` accepts the C++-written file.
11. **`tx.slot_symbols` / `slot_keying`.**
    - Bitwise for CE full/short/tiny and l-full/short/tiny, from a `.qrsp` (fp16) and a `.bin` (int8), at the three slots.
    - `qrss_c_oracle` equality against `qrss_ce_symbol`.
12. **`dsp::upfirdn` + public `firwin`.**
    - `upfirdn` against scipy at 1e-15 relative for `(PULSE_TABLE, x, up=485, down=2)` and `(table, x, 970, 64)`, complex and real, including output length.
    - `firwin(481, 125, fs=8000, ('kaiser', 12))` at 1e-15. Use the frozen table for the transmitter, and test the generator anyway.
13. **`ce` transmit.**
    - `_grid_sum`/`phase_grid` at 8000 Hz, n0 = −1940, 60 s: within 1e-12 rad.
    - `cw_phase_turns`: FSK turns bitwise, OOK amplitude bitwise (units 5–183, hard switching).
    - `keyed_span_pos` with lead-in 0 to 10 s.
    - `_ramp_envelope` at 50 ms.
    - `baseband`: exact path only, with `n0 = nearbyint(start_s·fs)` and its `|start_s·fs − n0| < 1e-9` branch condition kept.
    - `to_audio`: integer turns mod den·8000 bitwise, using `unsigned __int128` or mulmod for `nn·num`; audio within 1e-12.
    - `check_carrier` strings.
    - `carrier_fraction`.
    - Chunk invariance in C++, bitwise across block sizes.
14. **`lwave` transmit.**
    - `at_positions`/`data_grid`.
    - `mix_turns` exact mod 242,500.
    - `pilot`, `id_weight`, `cw_turns`, `composite` before the clipper: within 1e-12.
    - `band_mask` on `fftfreq(nfft)`: within 1e-15.
    - `clip`: three passes. Exponent 1.0 is a copy, 2.0 a square, and only 1.5 goes through `pow`. Only the correction is filtered.
    - `frame_baseband` on l-tiny and l-short, with and without a 5 s lead-in, using Python's n0 and nfft: about 1e-9 absolute.
    - `resample` with up = 32: chunk-invariant in C++, and within about 1e-9 of Python.
    - Semantic loopback statistics measured by Python on the C++ output.
15. **`tx.PassAudio` / `transmit_audio`.**
    - Full length for CE tiny and short and L tiny and short.
    - Carriers 1200, 1500, 2650 and 1500.3 Hz; lead-in 0 and 5 s; FSK and OOK on CE.
    - CE max abs error ≤ 1e-12, L ≤ 1e-9.
    - Length equals `slot_samples`.
    - Refusals with Python's exact text: L+OOK, an OOK `.bin` on L, `check_pilot` below 322.07 Hz, a missing callsign, `pre_s` that does not cover the keying.
16. **Codec glue** (codec marker).
    - `source_sha256`/`codec_id` equal `picture.codec_id_from_onnx` (0xD1D8).
    - `encode_picture` on a 640×480 PNG at fp16 gives the same segs bytes and picture ID as `qrss_encode.py`.
    - Requires `.venv` onnxruntime at 1.28.0.
17. **CLI `sstvae-qrss encode|beacon|transmit`.** `test_qrss_beacon.py`'s CLI tests and `test_cli_transmit_refusals` pass with the native subcommand substituted: same stdout lines, refusal strings, exit codes, no output file on refusal, and a `--float` WAV byte-identical to `write_wav_float` of the C++ audio.
18. **App.** Typed `qrss_tx::Backend`, with the native backend as default. `test_qrss_tx.cpp` keeps its assertions on the seams: one encode, one make per pass, keyed once per pass at Q−11 s, the next pass made during the current one. GUI constants come from `qrss/config.hpp`.
19. **Cross-implementation chain and loopback.** C++ transmit, then Python simulate and receive (smoke equivalent, CE and L). Then the TINY loopback (exit criterion 6).

## 4. Risks and hard parts

### 4.1 What will not be bit-exact, and the bar to use instead

- **CE phase and audio.** `upfirdn`'s summation order and numpy's SVML `cos`/`sin`/`exp` cap agreement near 1e-13 rad, so use 1e-12. Copying scipy's loop order costs nothing and shrinks the error to the trig.
- **L clipped frame.**
  - A whole-frame FFT (scipy pocketfft against vendored `pocketfft_hdronly`).
  - A threshold nonlinearity: about 35% of samples are over A_CLIP on the first pass, so boundary samples can flip.
  - `pow(x, 1.5)` from libm.
  - Bar: about 1e-9 plus the semantic statistics.
- **Receiver.**
  - Error sources: `oaconvolve` and FFTs (2^19, 65536, 16384, 2500), `einsum`, pairwise means, LAPACK `lstsq`/`eigh`/`polyfit`, the QR LSQ spline, `CubicSpline`, `std::complex` Annex G division against numpy's Smith form.
  - These errors reach discrete decisions: the `select_q` argmax, the `fit_timing` cell and runner-ups, segment-walk thresholds, Newton exits, gate V at Z > 6, cwid margin ≥ 6, the A1/A2/A3 thresholds.
  - Bar: per-stage tolerance with Python intermediates injected, plus statistical end-to-end checks. Never compare `pass_uid`: it hashes the float32 z, so the two implementations always name the same pass differently, and `Store.twin` is what dedupes.
- **Live captures with unheard spans.** `capture_prepare` fills them with `default_rng(q)` noise (PCG64 plus numpy's ziggurat). Do not port PCG64. Inject the noise in tests and accept statistical parity live.

### 4.2 Bit-exact traps that must be got right

- **double → half.** Convert in one correctly rounded step. Going through float32 double-rounds 5.5e-5 of values, about 8 latents per mode-C picture, which gives a different picture ID. MSVC has no `_Float16`, so the conversion is hand-written and tested exhaustively.
- **The `prepare_segment` RMS uses numpy's pairwise sum.**
- **WHT butterfly order on int8/20 inputs.** The C reference's integer WHT agrees only to 4.4e-16.
- **No FMA** anywhere in the bitwise paths (`-ffp-contract=off`).
- **Python rounding is half-to-even** (`round`, `np.rint`, `np.round`): use `std::nearbyint`, never `std::round`. This affects `n0`, `quantise_mix`, tile ids and int8. The one exception is `si5351`'s round-half-away, which is not ported anyway.
- **Floor-div and floor-mod on negative FE indices** (`i_a % 16`, `n0 % P`, `-(-n//P)`). C++ `%` truncates, which silently shifts the CH grid or the mixing phase.
- **NEP 50:** `float32_array * python_float` stays float32. This matters for the passband quantiser, the blanker threshold and `/ln 2` on medians.
- **Constants from `math.exp`** come from the generator, never from `std::exp`.
- **`Fraction(float).limit_denominator(1<<20)`** must be ported faithfully. Carriers arrive as floats from `--freq` and from the app's `%.1f`.

### 4.3 scipy and numpy with no C++ equivalent in `native/`

Each item names its plan.

- `upfirdn`: write it (stage 1).
- `firwin` with fs and Kaiser/Hann windows: expose `firwin_band` (stage 1). Freeze the tables anyway, because scipy's Kaiser uses Cephes `i0` while native has a power series.
- Complex `resample_poly` with Kaiser(10), with `n_pre_pad`=16 and `n_pre_remove`=11 alignment so that `ch[k]` lines up with `fe[16k]`.
- `oaconvolve`/`fftconvolve` in mode 'same'.
- `freqz` at arbitrary frequencies.
- `windows.hann(sym=False)`.
- `make_lsq_spline` (FITPACK-style QR, including the NaN-on-empty-span behaviour).
- Not-a-knot complex `CubicSpline`.
- `i0e`: port Cephes. `std::cyl_bessel_i` is missing from libc++ and overflows.
- `gammaincc` for integer k: closed form, which agrees to 1e-14.
- `gammainccinv` with non-integer shape: freeze the M=11 value instead.
- `median_filter(reflect)` and `maximum_filter1d`.
- `norm.isf`, `gamma.logsf`: stage 4 only.
- `welch`, `quantile`, `interp` for statistics: not ported. Python measures C++ output instead.
- `lfilter`: simulator only.
- pocketfft's 16-plan LRU will thrash with the new lengths (2^19, 65536, 16384, 16000, 2500, 2420, 449064). Give the hot lengths their own cached plans.

### 4.4 `live.py`'s processes and threads

Python's structure is a spawned `qrss-finish` child (niced) that receives a pickled 58 MB capture per dial epoch, plus a live worker, a finish worker, a stdin reader and a FLAC packer thread. The C++ replacement is low-priority threads in the GUI process.

Python behaviour to fix rather than copy:
- **Unlocked shared reads.** `_refresh` reads `tiles()` and `store.due()` outside `listener.lock`, and `_keep` mutates `s.dets` outside it. In C++ that is undefined behaviour.
- **One exception loses a slot.** An exception in a live refresh sets `done`, so the end-of-slot receive never runs and `recover()` later skips the slot. Decide this explicitly: the recommendation is that a failed refresh does not retire the slot.
- **Long work under the lock.** Render (ORT), `associate` and the first codec load all run under the lock, and stall `feed()` for seconds.
- **Lazy caches.** Python's `lru_cache`s, `ChanCapture._psd`, `_MF_TABLES` and the Store's ZW cache become immutable or `call_once` tables, plus a store mutex.

### 4.5 File formats

- **npz.** `np.savez` writes ZIP_STORED with zip64 local extras (0xFFFFFFFF sizes), so the reader uses the central directory and the writer emits CRC-32.
- **`PassResult` meta** is JSON stored as a uint8 array and may contain NaN, Infinity or −Infinity (cwid `z_match = -inf`). nlohmann rejects these tokens, so the lenient reader is needed.
- **`state.json`** must never contain them: `QJsonDocument` rejects the whole file, and the app silently stops updating.
- **The passband store** relies on sparse 57.6 MB hourly memmaps. On Windows: set FSCTL_SET_SPARSE, or else 48 h takes 2.8 GB. `os.replace` of a mapped or open file fails there (`MoveFileExW`). There is no inter-process lock beyond `.lock`, and `qrss_decode --em` takes none, so mixed Python and C++ writers can lose an acc-JSON update.
- **The beacon file has no header-version byte.** A future `FORMAT_VERSION` bump invalidates every existing `.bin`; the port keeps that rule knowingly.
- **Text interfaces.** CLI stdout and error strings are interfaces (tests grep them). So are `associate`'s rule names, which become `note` strings matched by `qrss_window.cpp::plain_note`, and `GUESS_NOTE`.

### 4.6 Process risks

- **The receiver is a moving target.** See §0. The stage 2 hash pins keep drift visible.
- **Calibrations tied to the statistic.** `D_PASS`, `KAPPA_SELF`, the `G_*_L` gains, `TBD_GUMBEL_*` and the A1/A2 false-alarm thresholds are calibrated on the exact Python statistic. A C++ normalisation difference silently moves the false-alarm rate, which is why the A-7 recheck is an exit criterion.
- **Stale comment.** `lframe.py`'s "pending Jeff's call" comment is stale: waveform-l.md records the spread copy as chosen on 2026-10-11. The generator takes the value from the code, so nothing breaks, but the comment should be fixed on the Python side.
- **Environment.** pybind11 is missing in this container and `.venv`'s onnxruntime is 1.31.0, not the pinned 1.28.0 (stage 0).

### 4.7 Deliberately not ported

- **The simulator:** `channel.py`, `qrss_simulate.py`, `Truth`, and the dead `_tone_stub` and `_FE_TAPS` fallbacks in `channel.py`.
- **Training.**
- **`si5351.py`.** No app or CLI uses it. `qrss_beacon_c` is the reference for hardware beacons. Port it only if a clock-chip output feature is wanted.
- **`qrss_beacon_c` itself.** It stays C, as the independent oracle. Only `sha256.c` is reused, as C++.
- **Diagnostics:** `ce.loopback_stats`, `loopback_mf`, `symbol_classes`, `lwave.papr_db`, `occupied_hz`, `loopback_stats`.
- **Test-only code:** `genie_track`, `genie_track_l`, `kalman_rts(causal=True)`, `cwid.decode_units`, `PathFn._inside`, `_DEBUG_SEG`.
- **Python-only plumbing:** `Stamper`/`StdinSource` (the in-process listener uses capture timestamps; a C++ CLI `listen` takes `--wav` only), the multiprocessing finish process, and the `OPENBLAS`/`OMP`/`MKL` thread pins.
- **Transmitting the v1 and prototype frames** from the app. Their presets are generated so the parity tests and receive fallbacks work.

## 5. Test runtime budget

No run should exceed a few minutes. The fast QRSS tier as a whole is about 200 s single-process and about 2.5 min on 4 CPUs, so it is run per stage, never whole under substitution.

Estimates marked "est." are for code that does not exist yet.

| Check | Runs in | Time | Fits? |
|---|---|---|---|
| ctest `qrss_golden`, `qrss_formats`, `qrss_c_oracle` (bitwise vectors, C reference) | C++ | < 5 s est. | yes |
| ctest `qrss_tx` (symbols, audio excerpts, chunk invariance, L clipper on l-tiny/l-short) | C++ | < 10 s est. | yes |
| `test_qrss_native_parity.py -k tx` (full-length TINY+SHORT transmit both sides, fp16 exhaustive, npz/qrsp/bin round trips) | pytest | about 45 s est. (Python SHORT transmit is about 0.7 s) | yes |
| `--native-qrss=tx` on frozen 0.5, format 1.5, header 5.8, spread 0.3, ce 7.6, lwave 4.9, beacon 24, c_ref 8, channel 10.6, lchannel 5.3, smoke 7.7 | pytest | about 76 s (Python-side measured) | yes |
| Codec-marked encode / picture ID | pytest | about 10 s | yes (needs ORT 1.28) |
| Stage 2 oracle-injected per-module parity (6 SHORT/TINY fixtures; Python reference about 4.6 s per SHORT pass) | pytest | about 60 s | yes |
| `--native-qrss=rx` fast tier: frontend 6.6, acquire 23, lacquire 7.3, track 2.4, ltrack 6, ldemod 1.5, cwid 1.5, lunits 0.8, receiver 0.1, lreceiver 3.2, rules 3, spread 0.3, smoke 7.7 | pytest | about 65 s Python-side, less with C++ | yes |
| Statistical acceptance: C++ receive on about 12 scenarios against committed summaries | C++ | 30–60 s est. | yes |
| A-7 Gumbel / A2 Gamma recheck on the C++ statistic (200 noise captures) | C++ | about 20 s est. | yes, natively only |
| Stage 3 listener bookkeeping ctests (stub receivers), store interop round trips | C++ and pytest | < 15 s | yes |
| `test_qrss_live` fast 22 s, `test_qrss_multipass` 7.7 s, TINY slot replay about 10 s | pytest | about 40 s | yes |
| `pytest -m slow -k qrss` (120–150 tests) | pytest | about 2 h | **no.** Its role is taken by statistical acceptance |
| `test_qrss_acceptance.py` (FULL slots, codec, COCO) | pytest | tens of minutes | **no.** Run manually by the owner |
| Generating the Python receiver reference summaries | script | 10–15 s per scenario | not a test. Run per scenario, each under a minute, when the hash pin trips |
| FULL-pass loopback or on-air | manual | 30 min | owner only. TINY loopback (about 70 s) is the automated proxy |

## 6. Defaults taken (Jeff can overrule any of these)

1. **Design rule 3** (never edit `native/`) is lifted for this port by Jeff's request of 2026-10-12 20:02Z, in its narrow form: QRSS adds new files under `native/core/qrss/`, `native/core/{util,io,math}/`, `native/apps/`, `native/bindings/module/qrss/` and `native/tests/`, plus the CMake and bindings wiring. SSTVAE's modem, its golden vectors and `NATIVE_SUBSTITUTIONS` stay untouched. QRSS gets its own generator, its own corpus (`native/tests/golden_qrss/`) and its own substitution table behind `--native-qrss`.
2. **Waveform L is ported with CE.** Everything is experimental; if L changes, the port follows.
3. **The receiver is ported against hash pins**, not after a freeze. It comes after the transmitter anyway.
4. **The store stays shared and file-compatible** with the Python tools (npz/JSON, `.lock`).
5. **The app's Python path stays selectable** (`SSTVAE_QRSS_BACKEND=python`) until each stage has run on Hamlet; that switch is the UI thread's to wire.
6. **In-process encode uses fp16**, as `qrss_encode.py` does, so a picture gets the same ID from the app and the CLI.
7. **Old receiver rules** (`QRSS_OLD_EDGE_NOISE`, `QRSS_OLD_GATING`) are decided in stage 2; default is to port them as `ReceiveConfig` flags, since `test_qrss_rules.py` exercises both.
8. **Android is out of scope for now**, but transmit audio is streamed (`PassAudio::render`) rather than one 115 MB buffer, which Android would need anyway.
9. **Passband store:** the C++ listener writes raw `.i16` hours and packs finished ones to `.flac` with a small dependency-free FLAC encoder (fixed predictors and Rice coding; dr_flac reads them back, and so does soundfile). Packing used to be Python's, and an unpacked 48 h store is about 2.8 GB (4 kHz complex int16). No libFLAC.
10. **EM, retro search and decode are ported** (stage 4). Jeff, 2026-10-13 05:00Z: the maintainer would rather have no Python in the release builds.

## 7. Orchestration

- Stage 0 and `sequences` are written first by the Build thread, as the worked example of the conventions: a module's C++ in `core/qrss/`, its binding in `bindings/module/qrss/<module>.cpp` (self-registering), its substitution rows in `tests/qrss_native/<module>.py`, its golden entries in the generator, and a ctest.
- Later modules are ported in waves of independent modules, each in its own git worktree and build directory, then reviewed adversarially and merged. Registration is by glob, so parallel modules touch no shared file.
- `native/gui/` is the UI thread's: the app swap (stage 1 step 18, stage 3) is handed over as an API, not edited here.
