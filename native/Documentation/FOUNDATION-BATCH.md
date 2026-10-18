# Foundation batch — proposed engineering implementation

## Architecture and thread safety

The five additions are fixed-storage, sample-streaming `FoundationCore` specializations. They contain only scalar values, fixed arrays, filter histories and sample-counted ramps. Kernels have no JUCE or GUI dependency. The small `DspCore.h` dependency supplies the established 10 ms `Ramp`; none of its vector-backed processors are instantiated by these kernels.

APVTS processors read their 6/10/8/8/6 parameters once per callback. Prepare reads the current snapshot, resets ephemeral histories, initializes ramps to that snapshot, and publishes readiness with zero algorithmic latency. Runtime recall changes targets without resetting history. Message/staging threads own APVTS validation, editors, attachments, allocation and insertion. Audio owns detector/filter histories and publishes through the existing coherent atomic telemetry. Host ownership, eight-slot latency budgets, compensation and bypass are preserved.

Callback call graph reviewed: `FoundationProcessor::processBlock` → `settings` (cached atomic parameter reads) → `FoundationCore::process` → fixed-array filters, `Ramp`, bounded local math → atomic measurement → existing `publishMeterBlock`. No direct allocator, lock, file/network I/O, logging, sleeps, GUI callback, message posting, or processor destruction occurs on this path. Coefficients update on absolute sample phase modulo 16. The unchanged `InsertChain::process/run` uses prepared scratch buffers, a two-channel borrowed JUCE buffer view and preallocated compensation rings; publication retires pointers, destruction occurs off-thread.

Tests instrument ordinary/array/nothrow/aligned C++ `new` and allocator imports present in the test executable's import table. Import patching is confined to the test process, happens before preparation, and is never production code. Each successfully instrumented import is printed. System-DLL-internal allocation calls and dynamically resolved unimported functions are outside this probe's coverage. Both calibrated probes reported zero callback-thread allocations in the approved runs. Unrelated background-thread allocations are separately counted, never attributed to the callback.

## Implemented modules

| Registry ID | State type | Controls | Meter | Conservative tail |
|---|---|---:|---|---:|
| dc-subsonic | DC_SUBSONIC | 6 | emitted sample peak | 1 s |
| smart-gate | SMART_GATE | 10 | effective blended reduction, pre-output | 0 s |
| vocal-rider | VOCAL_RIDER | 8 | signed absolute-max riding gain, pre-mix/output | 0 s |
| plosive-control | PLOSIVE_CONTROL | 8 | wet-scaled low-band detector reduction | 0.5 s |
| hum-buzz | HUM_BUZZ | 6 | emitted sample peak | 8 s |

The exact 38 ranges/defaults/steps are declared in `FoundationProcessors.cpp`. The old twelve registry entries and state types retain ordering and identity. Old processor parameter definitions and version hints are unchanged. Five new entries are appended. All defaults are neutral; each processor reports zero added algorithmic latency, not zero phase shift.

DC uses the specified normalized 5 Hz recurrence and two optional identical 12 dB high-pass sections. Gate uses linked peak detection, close-boundary expansion, hysteresis and exact consecutive-sample hold. Hard-gate mode crossfades its requested attenuation. Rider uses double linked RMS energies, exactly ceil(100 ms) detector warm-up and direction-dependent dB movement. Plosive uses a complementary low-pass/remainder split and linked fast/slow burst contrast. Hum uses sixteen double-precision notch histories with ramped participation and finite-depth linear blending. Hidden filter paths advance while inactive. Non-finite samples are zeroed per channel and its histories reset. Ordinary finite signals are not clipped. Filter frequencies explicitly clamp to [1 Hz, 0.45 × sample rate] at other supported positive rates.

## UI, state and export

Generic native editors reuse the supplied approved raster assets and physical-value sliders. No artwork was invented. Every new parameter has an APVTS attachment, units and numeric reset. Cleaner cutoff/order and hard-gate ratio show conditional inactive states. Help describes deterministic processing and neutral zero-range/depth conditions. Meter scales remain unchanged; gate avoids the old wet-scaled suffix.

New direct processor state validates the entire tree before commit, including types, counts, duplicate/unknown IDs, non-finite/malformed/out-of-range numbers and boolean values. Session XML/JSON use the existing validator and factories. Fractional harmonic counts are snapped by the APVTS integer step, not advertised as strictly rejected.

Export preflight sums the new declared filter-decay bounds in each unbypassed audible chain and selects the longest chain. It is conservative even if an effect currently has a neutral range/depth. Shorter explicit durations require `allowTailTruncation`; the UI exposes that deliberate choice without changing the requested duration. The tail slider supports 120 seconds, enough for eight 8-second hum sections. Legacy generic .02-second metadata is not reinterpreted as an established filter-decay contract. Optional worker-only before/after-master observers support floating-point parity tests before WAV encoding.

## Test and review status

Two added targets: `hem_foundation_tests`, `hem_foundation_integration_tests`. The exact expected inventory is now 33, retaining all original 31 targets. Existing registry enumeration tests were extended; no previous assertion was removed to relax a DSP threshold. Existing native pointer interaction tests enable documented inactive paths before testing them.

Numeric tests contain independent filter/detector/gain recurrences, analytic complex transfer checks, neutral/dry/output calibration, stereo/impulse/silence and invalid/extreme inputs, parameter corners, sample partition checks, smoothing/reference error, hold/envelope timing, RMS caps/floors, plosive bursts/recovery, hum harmonic/Q/count/depth matrices and declared decay bounds. Integration tests cover all 38 controls with measured fixtures, direct/XML/JSON state, corrupt-state rejection, meter readiness/peaks, eight-slot allocations and timing, actual pointer/keyboard/reset callbacks, editor sizes/scales, an eight-second hum export and live/offline comparison before and after the unchanged master.

With explicit execution approval, both foundation targets passed, followed by all 33 CTest targets (0 failed, 0 target skips; four internal symlink fixtures skipped in hem_take_tests). See task-2/verification-approved-r2.log and build-foundation/full-suite-results.xml. A first integration failure was traced to a global allocation window counting unrelated background threads: the diagnostic found C++=10/imports=10, all on other threads. The corrected probe preserves the zero-callback-allocation assertion, reports background counts separately, and calibrates using a deliberate allocation. No DSP code or acceptance threshold was changed during verification. All five floating live/offline comparisons were exactly zero error. The approved app launched, responded, and closed with exit 0; its original approved hash is unchanged. The final master limiter's independent acceptance is unchanged. Device dropout/listening evaluation is not performed by these deterministic tests.

These are proposed engineering definitions, not recovered original specifications. Exact original plugin-specific faceplate/control-list parity remains blocked by missing authorized references. The source tree is an isolated development implementation, not a replacement for the current Windows candidate or installed Studio.

## Preparation and emitted-sample safety

These five modules support finite sample rates from 8000 through 384000 Hz inclusive. Validation precedes ramp-length and detector-duration integer conversions; rates are never substituted. The qualified 44100/48000/96000 arithmetic is unchanged. Pure-kernel prepare returns false on an unsupported rate, reports rate zero/not prepared, and processing silences buffers without advancing. The APVTS wrapper clears readiness and telemetry, reports rate zero, and throws an explicit invalid_argument during off-audio preparation. A valid reprepare restores readiness and reports exactly the kernel rate. Host preparation and processing remain serialized.

Before narrowing an emitted double to float, the kernel checks finiteness and float representability, then guards the final float. An unrepresentable sample emits zero and clears that channel's filter, DC and energy history. Linked detector/envelope/hold state is reset on this exceptional event; the healthy channel's independent histories remain intact. Ordinary representable finite audio is not clipped, and ordinary parameter changes do not reset histories. Invalid input retains the existing affected-channel sanitation. The guard is fixed-storage and callback-safe.

Compiled regression coverage adds invalid/extreme/boundary sample rates, failed preparation after successful processing, consistent effective-rate telemetry, silent unprepared callbacks, valid reprepare, positive/negative overflow, subsequent recovery, healthy-channel history retention, and preservation of large representable audio. These regressions passed in the approved numerical and integration runs.

Timing limits: the full-suite chain measurements included one worst-case 1.58344x deadline at 96 kHz/17 samples (p99 36.8 us). Timing is reported rather than asserted by this test; this is not proof of dropout-free real-time device operation. Exact original visual parity and listening acceptance remain outstanding.

Coverage correction: 32 expanded targets have no reported internal skips; the take target has four Windows symlink-privilege skips (preexisting/raced WAV and preexisting/raced sidecar). The original 31-target candidate had exactly the same four internal skips, with identical take-test source. CTest target success does not establish these cases. Actual imported hooks were malloc (numerical: 1), and malloc/realloc/calloc (integration: 3). Five additional bounded integration runs covered 30000 timed callbacks: all passed, no repeated maximum exceeded its deadline, worst maximum fraction 0.483025. The original 1.58344x excursion remains valid evidence; its cause is not established.
