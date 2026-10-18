# Native plug-in meter contract and validation

## Scope

All 12 native processors now publish a measurement snapshot only after their real, nonempty stereo `processBlock` finishes. Constructing an editor, preparing a processor, changing a parameter, and receiving empty or unsupported-channel buffers cannot fabricate meter readiness. `prepareToPlay` clears the snapshot even when an existing DSP core retains an old atomic meter value. Before the first processed block the native editor explicitly says **NO PROCESSED AUDIO**, with no level fill.

No parameter IDs, ranges, defaults, attachments, or DSP algorithms changed. Control component IDs and accessible names remain attached to the existing APVTS bindings. The analytic EQ graph is expressly labelled **ANALYTIC CONTROL RESPONSE / TARGET** and has an independent measured output-peak readout beside it. It is a control-response calculation, not a live spectrum analyser or a measurement of parameter-ramp progress.

## Measurement and calibration

| Display | Exact source / unit | Fixed graphical scale |
| --- | --- | --- |
| S.I.P.A. correlation | Existing StereoCore output-block `sum(L*R) / sqrt(sum(L²)*sum(R²))`; no mean subtraction | -1 to +1, ticks -1 / -0.5 / 0 / +0.5 / +1, signed fill from zero |
| Transient gain | Existing maximum-absolute **signed** envelope gain command in the block; before wet/dry mix and output gain, in dB | -12 to +12 dB, zero at centre, signed fill |
| VCA gain reduction | Existing maximum envelope attenuation in the block, before wet/dry mix and makeup, in dB | 0 to 24 dB, fill from zero |
| D.H.T.A. reduction | Existing maximum band/wideband detector envelope multiplied by wet fraction, in dB; not a broadband input/output level ratio | 0 to 24 dB, fill from zero |
| Restorer reduction | Existing maximum over eight bands and the block of the cut command multiplied by wet fraction, in dB; not a broadband input/output level ratio | 0 to 24 dB, fill from zero |
| EQ, Sub-Bass, Vocal, Tube, Tape, Monitor | Existing maximum absolute output sample over both channels and the current block, converted by `20*log10(amplitude)` | -60 to 0 dBFS, ticks -60 / -48 / -24 / -12 / -6 / 0 |
| True-Peak Limiter | Existing output reconstruction estimate: eight fractional phases of a 32-tap normalized windowed-sinc FIR; block maximum over both channels, converted by `20*log10(amplitude)` | -60 to 0 dBTP **EST.**, same tick spacing as sample peak; separately reports actual detector GR |

Peak silence is `-inf`, not an invented finite signal at the display floor. Nonzero values outside the graphical scale retain their actual numeric value and explicitly say BELOW SCALE or ABOVE SCALE. A clipped bar is never a claim that the numeric measurement was limited to the display range.

Correlation is undefined when StereoCore's existing denominator condition `sum(L²)*sum(R²) > 1e-20` is not met. This includes exact silence and a silent channel. The editor shows **UNDEFINED / SILENCE OR INSUFFICIENT STEREO ENERGY**, with no fill. A genuinely measured correlation of zero remains an available numeric measurement but correctly draws no signed fill. The centre reference line and labelled ticks are calibration marks, not fake signal indicators.

Limiter text identifies the reconstruction as an **8x, 32-tap FIR estimate**. At the end of serialized preparation, the processor publishes the actual `getLatencySamples()` result and rate into the coherent atomic meter snapshot. The GUI reads only that snapshot, including for milliseconds conversion, never JUCE's plain latency field or a separately sampled rate. Before preparation (or while a new preparation is incomplete), latency is explicitly unavailable; it is not hardcoded to 5 ms. For example, 11,025 Hz reports 56 samples / 5.079 ms. This work does **not** claim ITU/EBU true-peak certification, universal intersample-peak accuracy, or an independent compliance test.

## Ballistics and stopped processing

The display reads the **last completed audio block**, at the existing 25 Hz GUI repaint cadence. Peak and GR values are block maxima; transient gain is signed absolute maximum; correlation uses that block's complete correlation sums. The displayed block sample count and duration make the window explicit. There is **no extra UI peak hold, release animation, random animation, smoothing, or synthetic audio**. A GUI refresh can miss intervening short blocks; this is not a latched session maximum or a loudness meter.

Dynamics envelopes retain the DSP's original sample-rate-dependent attack/release and parameter ramps. Real GR can persist into silent output while an envelope releases; that remains a valid reading and is labelled SILENT OUTPUT. If processing stops, the last completed block remains labelled LAST BLOCK until the next processed block or prepare/reset. The editor does not assert a live device connection.

## Real-time and concurrency behavior

`ModuleProcessor::meterSnapshot()` returns a bounded coherent read of lock-free atomic payload fields. The audio writer brackets publication with an atomic sequence and writes the actual core measurement, readiness/availability/activity flags, block size, and count. A reader makes at most three attempts and returns unavailable rather than spin or expose a torn measurement. Prepare and process remain serialized by the existing host contract. Prepared state, sample rate and latency are part of that same payload: reset first invalidates the prior measurement/metadata, and each of the twelve preparation methods publishes coherent new metadata after its core and reported latency are ready.

The telemetry path has no heap allocation, mutex, UI call, clock query, or signal generation. Existing amplitude meters are reused without rescanning their audio. Only S.I.P.A. receives a complete bounded extra output-energy scan to distinguish its zero fallback from defined correlation. The four non-amplitude dynamics displays use an early-exit bounded scan for nonzero output activity. Static assertions require the integer, float and double payload atomics to be lock-free on the build target.

## Regression target

`Tests/PluginMeterDisplayTests.cpp` is intended for a JUCE console target linked with the same processor, editor and DSP source files as the existing processor-binding tests:

- Source/Plugins/Processors.cpp
- Source/Plugins/SpecialtyProcessors.cpp
- Source/Plugins/FinishingProcessors.cpp
- Source/Plugins/Editors.cpp
- Source/DSP/DspCore.cpp

Use the existing JUCE audio-utils/audio-processors links and generated JuceHeader. There are no additional packages or libraries. `Source/Plugins/PluginMeterReadout.h` is header-only.

The test uses actual registered processors, processBlock calls and native editor `paintEntireComponent` rendering at 1x and 2x graphics scale. It covers:

1. Unprepared, prepared-but-unprocessed, skipped empty/mono buffers, real silence, processed-block counts, and sample-rate reprepare for all twelve processors
2. Correlation +1, -1, orthogonal zero, one silent channel, and both sides of the existing denominator guard
3. Actual output sample peaks across all six sample-peak processors, 0.5 amplitude = -6.0206 dBFS, true silence, over-scale numerical indication, and calibrated raster widths/ticks
4. Known full-wet 4:1 VCA attenuation, matching real output gain, the 0..24 dB raster, and real release into silence
5. Negative, zero and positive transient gain, verified against actual full-wet output sample gains, with signed zero-based raster geometry
6. The limiter's exact reported latency and actual output's defined 8x FIR reconstruction, including measured GR; this is implementation verification, not standards certification
7. Concurrent audio-writer/message-reader snapshot coherence over 3,000 real blocks, plus 1,200 serialized prepare/process cycles at 44.1/96/11.025/48 kHz while the actual GUI readout is read concurrently; rate, actual latency, milliseconds conversion and block duration must stay coherent
8. Actual JUCE raster absence of fake fills, signed zero-reference lines, calibration ticks and measured bar endpoints at both pixel scales

An optional first command-line argument is an output directory for the actual editor PNGs, including unprepared/prepared/silent states for all processors and correlated/anticorrelated/zero, real peak/GR/transient/FIR measurements. On headless Linux, run via the project's Xvfb/native-GUI test environment.

## Execution status

**PASS — final post-metadata-change regression, 2026-10-01.** The actual run log at `Verification/PluginMeterDisplayTests.run.log` confirms all twelve processor readiness/reset/real-silence states, correlation +1/-1/zero/undefined cases, measured sample peaks and VCA gain reduction, signed transient gain, the defined 8x FIR output estimate and real latency, coherent atomic snapshots, concurrent reprepare rate/latency metadata, and actual native editor calibrated raster at 1x/2x.

The coordinating task also confirmed the final native host app rebuilt successfully after the rate/latency snapshot fix. All twelve preparation methods reset then publish metadata; all twelve process wrappers publish actual measurement readiness. This final documentation update changes no source files, parameter contracts, or DSP. These results verify this implementation and its native display behavior; they do not establish ITU/EBU true-peak certification.
