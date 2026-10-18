# Native integration verification — 2026-10-01

## Result and boundaries

The final Linux native application compiles, links and opens against JUCE 8.0.10 with C++20. Final ELF SHA-256: `972d9471041b97d06f6a722ae997277071607fe8c8a982ecfcc41feb0f8c06eb`. Standard CMake configuration also succeeds, including all 25 test targets. No full CTest aggregate execution, Windows build, installer or physical-interface acceptance is claimed.

25 distinct programs have passing evidence across the preserved recovery/integration milestones. This count is not a limiter-certification corpus result. The unchanged Task #2B baseline remains separately limited.

## Final-source focused reruns

The following current-source programs passed after affected native units were rebuilt. Raw output is in Verification/*.run.log and exact compilation/link commands in Verification/*.commands.txt.

- CoreMasterStageTests: sample-exact baseline parity at 44.1/48/96 kHz, reported latency 237/253/445, irregular blocks, NaNs, oversize safety, stereo and zero observed ordinary new/new[] in processing
- NativeHostLifecycleTests: actual MainComponent cold no-device startup; 13-track async import/Apply; insert/replace/remove and editor retirement; simulated disconnect; session save/recall; float32 export; clear/reimport; stale generations; cancel-close; failed save; successful save-before-close; active-export Keep open and approved cancellation with no final output
- NativeWorkspaceTests: all 13 real mixer strips, gain/mute/solo/bypass and meter/reset routing, 12-entry search/categories, monitor-only dispatch, editor lifetime, bank replacement, split/expand, navigation continuity and calibrated rasters at multiple sizes including the default 1046×395 content area
- ProcessorControlBindingTests: all 12 actual native editors, all 98 parameters (84 numeric, 14 toggles), defaults/min/max/reset, accessible identities and raster bounds
- PluginMeterDisplayTests: all 12 readiness/reset/real-silence states; correlation +1/−1/zero/undefined; actual sample peaks/VCA GR/signed transient gain; defined 8× FIR estimate and real latency; coherent concurrent audio snapshots and 1,200 reprepare cycles; calibrated 1×/2× native raster
- OfflineMixTests: 13 stems, exact frames/gains/mute/solo; all native insert types and eight restored slots/bypass; mixed-rate resampling; baseline parity and alignment at 44.1/48/96; monitor exclusion; cancellation, no-overwrite races, RIFF guard and cleanup
- OfflineMixWorkerTests: busy exclusion, message-thread completion, restart, cancel, shutdown cleanup and actual export-panel raster at 1×/2×
- TakeRecorderTests: exact mono/stereo float WAV frames/samples/placement; zero instrumented producer allocations; existing/raced WAV and metadata preservation; dangling symlinks; competing commits; retained recovery files; truthful metadata warning and empty-take cleanup
- ChainTests: eight inserts, unity, latency-aligned bypass, replacement/retirement, state roundtrip, removal and device-rate restart
- StemMixerTests: real 13-stem sum, per-track peak calibration, bounded chunking, mute/solo source advancement, common seek and steady frame advancement
- HardwareLookAndFeelTests: actual native knob/fader/button raster and states, fixed dBFS geometry, real first-paint snapshots, stereo/RMS/silence/clip/reset at 1×/2×
- StudioBrandingTests: exact embedded approved ghost identity, startup brand/subtitle illumination, bounded crop and paint at 1×/2×

## Preserved earlier passing checks

The five original dependency-free DSP programs were compiled from the pristine v0.10 source: DspTests, SpecialtyTests, FinishingTests, MasterLimiterTests and MonitoringRouterTests. Those kernels remain unchanged; MasterLimiterTests exercises the archived host model, not Task #2B certification.

AudioMetersTests and ControlGeometryTests passed numerical calibration and geometry checks: DC/sine at 44.1/48/96; independent stereo; silence; exact release/hold/clip/reset; non-finite/extreme input; partition invariance; rate reset; allocation guard; mapped pointer/fader positions and scaled bounds. Their source remains unchanged in this freeze.

The preserved integration also passed ImportTests, SessionTests, AnalysisTests, MonitorProfileTests, VocalPipelineTests and DeviceInputTests: staged import/resampling/rejection; 13×8 XML/JSON state; real calibrated FFT/waveform; measured-profile validation; capture/monitoring semantics and rack latency; 16-channel routing and missing-input silence. Current changed host/import/session/publication paths receive the focused lifecycle/recorder/export reruns above. Earlier raw logs are retained with .log names; latest focused logs use .run.log.

## Actual native desktop observations

Earlier integration checks verified input-knob drag from 0.0 to −10.8 dB, double-click reset, actual Loop state, readable import controls and honest NO AUDIO output meters. This follow-on also verified:

1. Native vault search for Tape, actual insertion into the selected slot and embedded processor view
2. WAV chooser import and Apply of a generated 1-second stereo fixture without a physical device; real inserted processing retained
3. Embedded export producing 144,000 stereo float32 frames at 48 kHz (1 second source + 2 seconds tail), with 2,557 actual fixed-latency samples removed. Saved file format/header/hash were independently read; evidence is Native-UI-export.json. The earlier accessibility text-entry limitation was resolved through supported native key input
4. Final application 1280×720 mixer: Mute/Solo/Edit and gain/readout fit vertically; horizontal scrolling exposes all 13 channels
5. Final SIPA editor before audio processing: NO PROCESSED AUDIO, no fabricated correlation fill
6. Actual Close Studio dialog: Keep open retains the inserted processor; actual New dialog: Cancel preserves it; Start new resets the disposable QA state and preserves source files
7. Approved ghost badge, illuminated header/labels and actual state-driven button lights at startup. Other open image windows/folders were left intact

The GUI export ran immediately before the final meter/close changes; the same current-source renderer and production MainComponent export paths passed again in final executable tests. No GUI test generated physical sound, recorded a microphone or established hardware latency.

## Build method and corrections

The initial native CMake build compiled the real JUCE 8.0.10 modules. Efficient follow-on builds recompiled changed native host/test units using recorded CMake flags and reused only unchanged JUCE module objects and byte-identical baseline objects. Dependency-hash caching rejects dependencies changed during compilation. Final app/build/test command logs are included. Portable CMake targets are the standalone reproduction route, without requiring the recovery workspace.

The development CMake configuration specified Release/-O1, but JUCE recommended flags also append -O3 to C++ units. This is not a production performance or universal real-time guarantee. Allocation guards observe ordinary C++ new/new[] and do not replace sanitizer/all-allocator coverage.

Corrections found by verification:

- Pinned JUCE 8 writer API/ownership fixed in the import fixture
- Source read-head assertions explicitly account for JUCE resampler's three-frame first-block read-ahead while retaining exact common-seek/next-block checks
- Headless processor popout/editor lifetime failure resolved by the real embedded workspace; no-device pending publications now settle without waiting for an absent callback
- Default-size mixer clipping corrected and covered by viewport-bound tests
- Recording sentinel fixture writes raw bytes rather than JUCE's default CRLF text conversion; preservation assertions compare exact bytes
- Plugin meter readiness, undefined correlation, signed geometry and rate/latency coherence corrected without changing parameter contracts or DSP kernels
- Save failure and active-export shutdown exercised through the actual close state machine

## Still unverified / incomplete

Windows compilation and filesystem behavior, real audio drivers, physical playback/recording/loopback, full-load deadlines, hot-plug, disk-full/power-loss and long-session stress remain open. Export/recording publication requires hard links and safely rejects unsupported filesystems. No arranger timeline, external VST3/AU hosting, MIDI or automation lane system is implemented. The 12-entry native registry is the actual inventory. Independent baseline limiter true-peak/listening-quality acceptance remains open. No 100% meter, zero-dropout or completed full-DAW claim follows from these focused passes.
