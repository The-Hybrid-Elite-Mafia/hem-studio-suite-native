# Clip Repair: bounded-lookahead waveform estimation

## Scope and honesty

Clip Repair estimates plausible missing waveform samples on short, flat-topped peaks. It does **not** recover the original recording, undo an unknown nonlinear process, guarantee improvement on music, or use AI. The test references are known-clean synthetic signals, not claims about arbitrary audio.

The module changes only detected plateau samples. It never rewrites their neighbouring shoulders or couples the stereo channels. Ambiguous, excessively long and insufficiently sampled events stay unchanged and are counted as skipped. Detection requires at least three same-sign near-flat samples at/above the selected threshold and smooth rising/falling shoulders; exceeding a threshold alone is not enough.

## API and integration

- Header: `Source/DSP/ClipRepairCore.h`
- Settings: `hem::ClipRepairSettings`, also `hem::ClipRepairCore::Settings`
- Core: `prepare(double rate, const Settings& initial = {})`, `reset()`, `latency()`, `sampleRate()`, `preparedRateWasValid()`
- Processing: `process(float* left, float* right, int n, const Settings&)`
- Observation: `processObserved(left, right, n, settings, observer)`; observer signature `void(const std::array<float,2>& dry, const std::array<float,2>& wet, const std::array<float,2>& delta, float activity)`
- Use two distinct writable channel buffers. Prepare/reset/process are single-audio-thread operations; prepare must not run concurrently with processing.
- Callbacks are synchronous, output-time-aligned and must be realtime-safe/noexcept themselves.
- Report `latency()` to the host on preparation. All modes, including amount zero and bypass, use the same dry delay.

The observer receives the delayed original, the normal processed output (including output gain and bypass), and the actual float-rounded repair contribution. Delta excludes gain-only changes: it is wet minus the gain/bypass-only baseline, and therefore equals wet minus dry at unity output gain. Listen Repair auditions delta; the observed wet trace stays the normal processed signal so it remains useful while auditioning.

## Controls

| Setting | Range / default | Behaviour |
|---|---|---|
| `threshold` | -24 to 0 dBFS / -0.3 | Minimum plateau level; not a peak shaper |
| `interpolationLength` | 0.1 to 4 ms / 2 | Maximum repairable plateau duration |
| `amount` | 0 to 100% / 0 | Blend from exact delayed dry to estimated waveform |
| `output` | -24 to +6 dB / 0 | Normal/repair audition output gain; excluded when fully bypassed |
| `algorithm` | 0 Fast, 1 Precise / 1 | Hermite interpolation or validated local sinusoidal model |
| `listenRepair` | false | Audition only the applied repair contribution |
| `bypass` | false | Crossfade to exact delayed dry |

Amount, output, bypass and audition use 10 ms ramps. Threshold, maximum event length and algorithm are sampled at the next candidate event; an already prepared event patch remains internally consistent. An amount/output/bypass change applies at output time. At default amount/output/listen values, every finite dry sample, including signed zero, is preserved bit-for-bit after the fixed delay.

## Estimators

### Fast: cubic Hermite bridge

For a plateau of L samples, the first reliable samples immediately before and after it are endpoints y0/y1, separated by D=L+1 samples. One-sided three-point derivatives estimate their slopes m0/m1. With t=k/D:

```text
h00 = 2t^3 - 3t^2 + 1
h10 = t^3 - 2t^2 + t
h01 = -2t^3 + 3t^2
h11 = t^3 - t^2
estimate(k) = h00*y0 + h10*D*m0 + h01*y1 + h11*D*m1
```

The interpolant matches endpoint values and estimated first derivatives. Only missing plateau samples are substituted. A hard-clip lower-bound check prevents lowering their observed magnitude.

### Precise: validated local sinusoidal hypothesis

Up to 0.75 ms of unclipped material on each shoulder supplies the independent evidence. An adaptive integer stride h improves conditioning for low frequencies at high sample rates. The identity

```text
x[n-h] + x[n+h] = 2*cos(h*w)*x[n]
```

provides a two-shoulder least-squares estimate of cos(h*w). After recovering w, the bridge is

```text
estimate(k) = y0*cos(w*k) + B*sin(w*k)
B = (y1 - y0*cos(w*D)) / sin(w*D)
```

Every available shoulder sample validates this hypothesis: maximum absolute residual must be at most 2.5% of plateau magnitude and squared residual at most 0.01% of shoulder energy. The angular gap must be below 0.95*pi and denominators must be well-conditioned. Precise skips failed fits instead of silently falling back to another algorithm.

Both algorithms reject a proposed patch if any sample is non-finite, changes sign or exceeds four times the plateau magnitude. The whole patch validates before any sample is stored. The cap applies only to inferred corrections, not to dry audio or to general output limiting.

## Latency, lifecycle and realtime contract

Supported finite rates are 8,000 through 384,000 Hz inclusive. Invalid/out-of-range rates fall back to 48 kHz and are exposed by `preparedRateWasValid()==false`. Delay is `floor(rate*0.006)` samples (288 at 48 kHz, 2,304 at 384 kHz); hence it is always at most six milliseconds. The maximum 4 ms plateau and 0.75 ms future shoulder fit inside this delay. The stream uses past/history plus already received lookahead samples: it is causal with the reported insert delay.

Two stereo rings are allocated only by prepare. There is no callback allocation, locking, FFT plan creation or worker dependency. Fixed scalar/stack temporaries are used per frame. Telemetry atomics are compile-time required to be lock-free. Reset clears the rings/counters and restores ramps to the latest settings without allocating. Process before prepare and null/non-positive buffer requests are harmless no-ops. Non-finite input samples are replaced by zero; non-finite controls use documented defaults, finite controls are clamped. Float overflow at extreme output amplitudes saturates to finite representable float.

A host draining an offline stream supplies latency() trailing samples; zero padding cannot create missing context at the original file boundary. Events lacking sufficient real shoulders may consequently remain unrepaired.

## Telemetry

All counters are cumulative per channel since prepare/reset and are published once per process call:

- `clipEvents`: qualifying near-flat plateau candidates, including those later skipped; not proof that a particular waveform was originally clipped
- `skippedEvents`: candidate events too long, ambiguous, poorly conditioned, missing shoulders or yielding no valid change
- `repairedSamples`: delivered channel samples with a nonzero actual float correction; amount zero, full bypass and output-gain-only changes never inflate it
- `peak`: maximum absolute audible sample in the most recent nonempty process call
- `repairPeak`: maximum absolute applied repair contribution in that call
- `activity`: 1 if the call applied any repair; observer activity is the corresponding per-frame value

An event can be detected and validly estimated while amount is zero, without being counted as repaired. One stereo event may produce two channel events. Atomic fields are independent snapshots, not a transactional bundle.

## Verification

`Tests/ClipRepairCoreTests.cpp` is dependency-free and uses the existing allocation probe. Run from native's parent:

```sh
c++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror \
  -Wno-mismatched-new-delete native/Tests/ClipRepairCoreTests.cpp \
  -o /tmp/clip_repair_tests
/tmp/clip_repair_tests
```

The GCC warning suppression is only for the repository's existing global new/delete allocation-probe implementation.

The initial 144-case sine matrix uses 8/44.1/48/96/192/384 kHz, 125/257/997/2003 Hz, 1.05/1.25/1.65 peak amplitudes and a 0.8 hard ceiling, for both algorithms. In 132 sufficiently sampled cases, worst repaired/original-error MSE ratios were 0.0152923 for Fast and 2.20e-8 for Precise. Twelve 8 kHz high-frequency cases deliberately remain unchanged because they lack usable plateaus/shoulders. These ideal stationary-sinusoid outcomes do not predict quality on arbitrary programme material.

The suite also checks bit-exact neutral/bypass delay, unchanged clean high peaks/multitones/impulses/Gaussian transients/square pulses, near-flat quantization tolerance, all controls, truthful skip/correction counts, stereo isolation, observer alignment, smooth output/bypass ramps, block-partition invariance, deterministic reset, invalid input protection and zero processing/reset allocations.

## Primary mathematical references

The implementation is original C++; no external source code or DSP library was copied or linked.

- [SciPy's CubicHermiteSpline documentation](https://docs.scipy.org/doc/scipy/reference/generated/scipy.interpolate.CubicHermiteSpline.html), checked 2026-10-06: cubic interpolation using endpoint values and first derivatives
- [Julius O. Smith, Physical Audio Signal Processing: Digital Sinusoid Generators](https://www.dsprelated.com/freebooks/pasp/Digital_Sinusoid_Generators.html), checked 2026-10-06: second-order sinusoidal recursions; the two-sided identity and bounded gap bridge above are derived from that sinusoidal model

