# Analysis modules: integration and parameter contract

## Files and registry

- `Source/Plugins/AnalysisProcessors.cpp` compiles all three adapters and their original native editors. Link the same existing JUCE modules and `Source/DSP/DspCore.cpp` as existing processors. No new dependency.
- Include `AnalysisProcessors.h` in the registry; append, never reorder baseline registrations:
  - `loudness-lab` / `LoudnessLabProcessor` / APVTS type `LOUDNESS_LAB`
  - `spectrum-spectrogram` / `SpectrumSpectrogramProcessor` / `SPECTRUM_SPECTROGRAM`
  - `delta-monitor` / `DeltaMonitorProcessor` / `DELTA_MONITOR`
- Approved artwork 29, 30, 31 must be embedded in ApprovedArtwork and dispatched by `approvedFaceplate` (owned by UI integrator).
- The only shared source changes in this lane are `InsertChain.h/.cpp`. They use the optional `analysis::ReferenceConsumer` interface from `Source/DSP/AnalysisReference.h`, without a dependency on concrete analysis processors. Other processors retain their current behavior.

## Parameter definitions

All parameter IDs are stable. Values below are physical APVTS values, not normalized automation values. Integer choices use exact unit steps. State validates the complete expected tree before replacement. SessionState uses the generic PARAMETERS transport root; migration validates and preserves it. Direct binary plug-in state separately requires the processor-specific APVTS root and rejects a generic transport root.

### 29 Loudness / Metering Lab

| ID | Values / range | Default | Effect |
|---|---|---|---|
| meterView | 0 Peak, 1 RMS, 2 VU | 0 | Actual display detector choice |
| peakHold | bool | false | Displays session-held sample peak in Peak mode |
| pause | bool | false | Excludes subsequent audio from measurement, exact audio pass-through |

History slots are explicitly unavailable until their individual M/S/I measurements are ready. Traces break across unavailable slots; a ready measurement of silence still retains its measured floor. Following an observed capture gap, M/S display readiness waits for fresh contiguous windows plus one measurement update. Reset is an explicit action, not a saved parameter. It resets measurement/history on the worker through a generation boundary. Time is measured accepted audio since reset, excluding Pause. The pass-through path has zero latency and does not modify any sample, including nonfinite input. The analyser marks nonfinite measurement data incomplete.

The true-peak aperture is deliberately relabelled `8x FIR PEAK ESTIMATE / dBFS est`, because these tests do not establish certification. VU is average-rectified, 300 ms, calibrated so a -18 dBFS RMS sine gives 0 VU; it is explicitly an approximation. I and LRA refuse to claim completeness after transport loss or four-hour capacity overflow. Reset is required. No peak normalization, gain or loudness-target processing is performed.

### 30 Spectrum / Spectrogram

| ID | Values / range | Default |
|---|---|---|
| fftSize | 0 512, 1 1024, 2 2048, 3 4096, 4 8192 | 2 |
| windowType | 0 Rectangular, 1 Hann, 2 Blackman | 1 |
| channel | 0 stereo maximum, 1 Left, 2 Right, 3 Mid, 4 Side | 0 |
| averaging | 0 to 2000 ms, step 1 | 100 |
| history | 1 to 60 s, step 0.1 | 10 |
| displayRange | 24 to 160 dB, step 1 | 100 |
| rms | bool: Peak / RMS selector | false |
| maxHold | bool | false |
| freeze | bool | false |
| depthView | bool: Flat / Depth selector | false |

Reset is an action. Spectrum dBFS is coherent-gain-corrected bin-centered sinusoidal amplitude, or sinusoidal RMS when selected; this is not noise-density or broadband integrated RMS. DC and Nyquist do not receive the one-sided doubling or sinusoidal RMS correction. Averaging is linear power with actual hop duration. The spectrogram stores actual frames at approximately 100 ms intervals, with per-column sample-derived timestamps; it never invents skipped data. Frequency uses actual sample rate and FFT size. Stereo maximum does not cancel antiphase input. Mid/Side intentionally may cancel it. Audio is exact zero-latency pass-through.

### 31 Delta Monitor

| ID | Values / range | Default |
|---|---|---|
| monitorGain | -24 to +24 dB, step 0.1 | 0 |
| soloMode | 0 Processed, 1 Dry, 2 Delta | 0 |
| delayTrim | -3 to +3 ms, step 0.01 | 0 |
| drySource | 0 Unconnected, 1 Chain start (aligned) | 1 |

Processed Source is a truthful fixed `Insert input` readout; there is no invented route selector. Auto Align is a one-shot action, not a saved toggle. Its bounded off-audio cross-correlation search picks the stronger reference channel and rejects silence or ambiguous correlation. The editor applies a successful requested result on the message thread to the ordinary saved `delayTrim` parameter. Audio never reacts to an asynchronously arriving worker result by itself. Offline rendering therefore uses the same saved trim, mode and gain. For repeatable exports, complete alignment first; the Auto Align action is not an offline render engine.

The actual reference API is one-block borrowed pointers plus sample count, rate, timeline and validity. `processBlock` consumes it once. Missing pointers, wrong rate/count, stale timeline, unconnected source or underfilled delay history never become a perfect-null claim. Missing reference produces fixed-latency unity processed audio and unavailable dry/delta values. The processed meter remains available.

InsertChain copies its input into a preallocated independent reference lane for each chunk, advances that lane through a separate delay for every immutable slot budget, and supplies it immediately before processing a reference consumer. The reference is therefore chain-start audio already aligned to this slot's input, rather than this processor's own input subtracted from itself. The fixed per-slot 6 ms host budget and total chain latency are unchanged. Delta itself declares round(3 ms) latency; the processed path remains fixed at that latency. Signed trim changes only the dry delay over a 10 ms ramp (fractional interpolation), up to 6 ms total dry delay. Monitor gain and solo transitions use existing 10 ms ramps. Solo/gain intentionally affect exported audio.

## Test integration

- Dependency-free numerical target: `Tests/AnalysisPluginTests.cpp` plus existing `Source/DSP/DspCore.cpp`, C++20, pthread on Linux
- JUCE binding target: `Tests/AnalysisPluginBindingTests.cpp`, normal application sources excluding Main/MainComponent plus AnalysisProcessors.cpp
- JUCE native editor target: `Tests/AnalysisPluginEditorTests.cpp`, same linking. Optional output directory argument produces 9 idle and 3 actual-input rasters
- Generic registry control tests must support actual ComboBox controls, and real multi-button selectors. Each selector's option-zero button has the raw parameter ID; other options use `id_value`. Property `analysisChoiceParameter` identifies the group, `analysisChoiceValue` identifies the physical choice. Exercise each button's actual onClick callback and verify APVTS plus feedback. Never create invisible sliders to satisfy a test designed only for knobs

## Scope and unresolved acceptance

No Windows, ASIO, device or listening checks have been run. No full official EBU/ITU test-set certification is claimed. Four-hour capacity exhaustion is implemented, not duration-soak tested. Rapid offline rendering may overflow an analysis transport; this invalidates I/LRA and reports drops rather than fabricating complete statistics. Audio processing remains deterministic and allocation-free. The editor preserves the approved chassis and uses existing material controls; current live traces are actual measured data, not copied illustration pixels.

### Final measurement-history validity boundary
Unready history values use NaN and never draw a floor placeholder. A trace begins anew after every unavailable interval. Actual ready silence remains a finite measured floor. Loudness drop accounting is published by the single audio producer with its reset epoch, using a baseline established before capture for that epoch. An in-flight old block cannot contaminate new-epoch counts. The polling worker also checks drop publication while idle; tail loss before the first consumption or after the last packet cannot leave I/LRA marked complete merely because no later timestamp gap arrives.
