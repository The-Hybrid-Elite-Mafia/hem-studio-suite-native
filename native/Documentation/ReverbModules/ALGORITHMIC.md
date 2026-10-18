# Catalog 20: Algorithmic Reverb

## Evidence and scope

Implemented against the actual pixels of `plugin-images-number-two/final/20-algorithmic-reverb.png` and the catalog-20 rows in `evidence/control-inventory/all-controls.csv`. The artwork establishes Hall, Room, Plate, Solo Wet, Bypass, nine rotary controls, stereo meters, and a reverb-tail plot. Its empty numeric readouts do not establish ranges or defaults; the engineering contract below supplies those explicitly.

This module is a real stereo algorithmic reverb, implemented as a standalone C++20 header. It is not a UI mock, a convolution effect, a physical room simulation, or an emulation of a named hardware unit. It does not depend on JUCE, the host's block size, or any shared/master limiter.

Owned deliverables:

- `Source/DSP/AlgorithmicReverbCore.h`
- `Tests/AlgorithmicReverbTests.cpp`
- This document

Processor wrappers, faceplate binding, serialization, and whole-host integration are outside this module's ownership and are verified separately.

## Public API

Namespace: `hem`

- `AlgorithmicReverbSettings`: plain settings snapshot; units below
- `AlgorithmicReverbCore::prepare(double sampleRate)`: allocate and clear delay storage off the audio callback
- `reset() noexcept`: clear buffers, oscillators, ramps, and meters without allocating; serialize with processing
- `process(float* left, float* right, int samples, const AlgorithmicReverbSettings&) noexcept`
- `latency()`: always zero, because the dry path is immediate
- `tailSeconds(settings)`: conservative nominal -90 dB host-tail estimate
- Public lock-free atomics: `peak`, `wetPeak`, `tailLevelDb`

The core is non-copyable because its telemetry contains atomics. A null right pointer, or `right == left`, selects mono input/output. Mono excites both internal stereo inputs and returns the average of the two wet outputs. Distinct left/right inputs remain distinct; anti-phase stereo is not discarded. Null left or nonpositive frame count is a no-op. Processing before preparation safely passes finite dry input through and sanitizes nonfinite samples.

Sample rates from 8,000 through 192,000 Hz are supported. Other finite rates clamp to those bounds; nonfinite rates fall back to 48,000 Hz. Hosts must not call `prepare` or `reset` concurrently with `process`.

## Parameter contract

| Field | Range / meaning | New-insert default |
| --- | --- | --- |
| `size` | 25–200%, scales early, diffusion, and network delay times | 100% |
| `decay` | 0.1–20 seconds, nominal low-frequency RT60 | 2 seconds |
| `predelay` | 0–250 ms, entire wet path only | 0 ms |
| `diffusion` | 0–100%, allpass feedback/density | 70% |
| `modDepth` | 0–10 ms, per-line delay excursion | 0 ms |
| `modRate` | 0.01–5 Hz, center modulation rate | 0.5 Hz |
| `damping` | 500–20,000 Hz, feedback low-pass corner | 8,000 Hz |
| `mix` | 0–100%, linear dry/wet blend | 0%, exactly dry |
| `output` | -24 to +6 dB, after blend | 0 dB |
| `soloWet` | Overrides mix to 100% wet | false |
| `bypass` | Crossfades to unity-gain dry, stops new wet excitation | false |
| `algorithm` | 0 = Hall, 1 = Room, 2 = Plate | Hall |

Nonfinite continuous settings fall back to their declared defaults; finite out-of-range values clamp. Invalid algorithm integers clamp to 0–2. Damping additionally clamps to 45% of the current sample rate. All continuous controls and mode changes use finite, 40 ms linear ramps. The first processing call after prepare/reset snaps to its initial settings, so a restored wet preset does not accidentally start dry. Modulation rate alone has no effect when depth is zero.

Changing size/predelay while audio is sounding moves delay read heads smoothly and can intentionally create a Doppler/pitch transition. This is not a fixed-latency resampling path.

## Signal path and mode distinctions

1. Capture/sanitize stereo input without changing the normal dry path
2. Stereo fractional wet predelay
3. Three differently timed early-reflection taps per output, including cross-channel taps
4. Four Schroeder input allpass diffusers per channel with unequal left/right times
5. Eight fractional-delay feedback lines with separate frequency-dependent damping and RT60 gains
6. Mode-specific orthogonal feedback scattering
7. Orthogonal stereo wet-output projections plus early reflections
8. Linear dry/wet blend and output gain; Solo Wet and Bypass precedence

Hall uses a normalized 8-by-8 Hadamard scattering matrix, 29.7–79.7 ms base delays, broad early spacing, and 0.68 maximum diffuser feedback. Room uses a Householder reflection matrix, 11.3–37.1 ms base delays, stronger earlier reflections, shorter diffusion, and 0.50 maximum diffuser feedback. Plate uses a permuted pairwise orthogonal rotation network, 7.7–47.9 ms base delays, close low-level early reflections, and 0.74 maximum diffuser feedback. Thus selecting a mode changes actual feedback topology and temporal structure, not only an output equalizer or label.

Each line's nominal feedback gain is `exp(-ln(1000) * delaySeconds / decaySeconds)`. This calibrates low-frequency RT60 while damping progressively shortens higher-frequency decay. Convex, normalized interpolation between the three orthogonal matrices avoids a discontinuous topology switch; additional loss during transitions is expected.

Eight detuned sinusoidal oscillators modulate line lengths around the requested center rate. Sine/cosine recurrences are renormalized every 4,096 samples. Maximum excursion is the smaller of the requested depth and 35% of the line's base delay, preventing read-head collisions at small room sizes. Fractional interpolation is linear and bounded. Coefficients refresh every 16 frames using a persistent sample counter; all tests are block-partition independent.

## Neutrality, bypass, and safety

The new-insert state is bit-exact dry, including finite signed zero. Dry latency is zero even though the audible wet response has acoustic travel time and any requested predelay. Mix zero with unity output is also an exact steady-state dry endpoint.

Bypass is a 10 ms ramp of wet contribution and output gain to exact unity dry. Initial bypass snaps immediately; runtime bypass becomes bit-exact after its transition. While the bypass target is active, new wet excitation is zero and existing delay contents keep decaying. Unbypassing ramps back in; it does not clear the tail. Bypass overrides Solo Wet and output gain. Reset explicitly clears tails.

Delay storage is allocated only by prepare. The maximum prepared stereo storage is approximately 1.94 MB at 192 kHz. There are no callback allocations, locks, external I/O, or block-sized temporary buffers. Audio telemetry uses statically verified lock-free atomics.

Nonfinite input becomes zero. Only the wet excitation is fenced to ±8; recursive states have a ±64 emergency finite fence with subnormal suppression. These bounds protect against malformed upstream audio and pathological automation and are far outside the tested ordinary-signal range. They are not an added master limiter. The exact unity dry and settled bypass paths preserve every finite input sample. Other output configurations use double intermediate arithmetic and a last-resort ±1,000,000 finite fence to prevent overflow on finite `FLT_MAX` input.

## Real telemetry

- `peak`: maximum absolute rendered output sample in the last nonempty block
- `wetPeak`: maximum absolute internal wet sample before mix/output gain in that block, including a decaying bypassed tail
- `tailLevelDb`: actual wet RMS power through a 50 ms one-pole envelope, clamped to -120 through +48 dB

The tail-level value is measured audio, not a preset-derived or synthesized decay plot. A UI can record this stream against elapsed time. Any theoretical orange decay curve must be labeled separately. The estimated host tail is `predelay + 1.5 * decay + 0.0015 * sizePercent + 0.1` seconds. Damping normally reduces the audible high-frequency tail below that conservative estimate.

## Standalone verification

From `native`:

```sh
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror \
    Tests/AlgorithmicReverbTests.cpp -o /tmp/hem-algorithmic-reverb-tests
/tmp/hem-algorithmic-reverb-tests
```

The test source locally suppresses GCC's inlining-related `mismatched-new-delete` false positive only around the existing shared allocation probe, whose matched new/delete replacements intentionally use malloc/free. The DSP header compiles with strict warnings.

Coverage:

- Bit-exact neutral startup at 8, 11.025, 22.05, 44.1, 48, 96, and 192 kHz
- Actual impulse responses, delayed onset, stereo spread, and decaying tails at all seven rates
- Every continuous control and all three algorithms affecting real audio
- Calibrated -6/+6 dB output, 25% dry/wet mix, and Solo Wet override
- Exact 125 ms and 250 ms wet-predelay shifts
- Higher echo density at higher diffusion; measurable high-frequency damping
- Low-frequency RT60 fitted from the Schroeder decay of a 300 Hz low-passed impulse
- Initial/settled bypass neutrality, bounded transition entries, tail persistence, reset/reprepare determinism
- Mono, aliased buffers, anti-phase stereo, null/empty/unprepared calls
- Over two minutes of rapid full-control automation and separate long maximum-depth/rate modulation tails
- Invalid/nonfinite settings and samples, including finite maximum-float input
- Identical output with 1, 7, 64, 511, and 4,096-frame block partitions
- No allocations in observed audio callbacks and reset

Measured nominal two-second RT60 values at 48 kHz were 1.974 seconds (Hall), 1.979 seconds (Room), and 1.972 seconds (Plate). Increasing diffusion raised the number of non-negligible early samples from 2,689 to 5,091 in the tested window. The 133-second automation stress peaked at 0.556 or less for the specified input.

Verified on 2026-10-06 in the Linux cloud executor: the strict optimized build passed 3,284,537 numerical assertions with zero observed callback/reset allocations. The same suite passed AddressSanitizer and UndefinedBehaviorSanitizer with leak detection disabled. LeakSanitizer itself reports that it cannot run under this traced executor. Windows compilation, JUCE processor/editor integration, hardware audio playback, and subjective listening are not established by these standalone tests.
