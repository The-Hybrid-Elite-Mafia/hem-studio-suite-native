# Validation receipt: approved 29–31

6 October 2026, cloud Linux / g++ C++20 / existing JUCE 8.0.10. These are new runs against the actual new kernels, JUCE adapters, native editors, and the real InsertChain implementation.

## Passed

1. Dependency-free numerical executable (`evidence/analysis-modules/analysis-tests`)
   - 997 Hz stereo/mono LUFS calibration at 44.1, 48 and 96 kHz; M, S, I; silence and absolute/relative gates
   - Correlation, crest and VU calibration
   - Generated EBU Tech 3342 LRA minimum-requirement fixtures 1–4
   - Generated EBU Tech 3341 tapered intersample-peak fixtures 15–18 against the existing 8x FIR estimate
   - FFT: 5 rates x 5 sizes x 3 windows = 75 coherent-gain cases, actual frequency/time mapping, antiphase preservation, RMS calibration and side null
   - Independent-stream Delta null, missing-reference behavior, fixed latency, measured delay alignment and silence rejection
   - Bounded transport overflow, nonfinite control protection and callback C++ new/new[] allocation probe
2. AddressSanitizer + UndefinedBehaviorSanitizer numerical executable, final kernel source
   - Passed with ASAN_OPTIONS=detect_leaks=0
   - LeakSanitizer separately failed to initialize because this environment uses ptrace. Its diagnostic is preserved in `leak-sanitizer-blocked.txt`; leak checking is NOT claimed
3. JUCE AnalysisPluginBindingTests
   - Exact bitwise pass-through for 29/30 at 3 rates x 4 callback sizes, including a callback larger than prepared maximum
   - Strict malformed/duplicate/nonfinite state rejection and lifecycle readiness
   - Borrowed-reference consumption and stale-reference rejection
   - Genuine upstream 2x gain compared against independent chain-start dry lane, proving reference is not the processed stream
   - Unchanged 8 x 288-sample host budget at 48 kHz; Delta's declared 3 ms latency is compensated correctly
   - One-shot Auto Align measures a 17-sample lag and commits it to saved delayTrim on the message thread
   - Zero callback C++ new/new[] allocations in tested paths
4. JUCE AnalysisPluginEditorTests
   - All actual choice-button, ComboBox, toggle and dial routes reach APVTS; defaults, units, accessible names, reset values and bounds checked
   - Default selection state available before the first paint
   - Nine idle renders at 1000, 1280 and 1672 widths, plus three live-input renders
   - Real FFT data-pixel presence assertion and visual review of actual traces
   - Exact pixel preservation of authored outer dial hardware: radius 48–66 px for spectrum dials; radius 66–96 px for Delta dials, excluding their separate readout wells
   - No JUCE assertions in final editor run

## Integrity

`protected-source-hashes.json` verifies DspCore.h/.cpp, MasterLimiter.h, CoreBaseline/Engine.cpp and both protected limiter test files remain identical to the telemetry snapshot. `source-hashes.json` identifies the final new analysis files and narrow reference-lane host changes. CMake/registry ownership remains with the integration owner.

## Not established

- Windows build or execution, ASIO/device behavior, acoustic/listening acceptance
- Full official EBU test-set certification or public-release license clearance
- Four-hour continuous duration/overflow soak, exhaustive thread sanitizer coverage, physical realtime deadline profiling
- Complete larger-catalog integration: these three adapters have been verified in isolation against real application objects; the integration owner must rerun the expanded app/registry/session tests after adding the entries

The measurement transport deliberately reports drops. Rapid offline rendering may make I/LRA unavailable rather than produce a misleading complete measurement. The audio path remains deterministic. The peak aperture explicitly identifies an 8x FIR estimate; VU is an approximation, not IEC certification.

## Expanded-host integration correction, 18:32 UTC

The initial isolated binding tests did not exercise SessionState's generic PARAMETERS transport. Expanded NativeHostLifecycleTests exposed that migration incorrectly required the processor-specific APVTS root for that path. The correction accepts and preserves the strictly validated generic session root while direct binary state still independently requires the own APVTS root. New binding fixtures cover all three processors' actual SessionState and APVTS round-trips, seven malformed-tree transactional rejection cases, unrelated roots and rejection of generic transport as direct APVTS state. The combined application/lifecycle rerun is owned by the integration worker; its final result is recorded below when received.

Confirmed final combined application rerun, 18:34 UTC: AnalysisPluginBindingTests and NativeHostLifecycleTests both PASS against the rebuilt 20-module app, including all plugin slider/session round-trips. Evidence: `evidence/native-build/reused-tests/AnalysisPluginBindingTests/{run.log,result.json}` and `evidence/native-build/reused-tests/NativeHostLifecycleTests/{run.log,result.json}`. This supersedes the earlier session-schema integration failure.

## Final truthful-history/startup-overflow gate

Focused fixtures added: 0.533-second actual tone has valid M with no unready S trace or startup-placeholder bridge; forced transport loss leaves incomplete I unavailable and breaks M history across the missing window; ready silence retains its real floor. A deterministic stopped-consumer fixture offers 65,536 samples to a 32,768-sample queue before first consumption, then stops producing: expected drops are exactly 32,768 and I/LRA must remain incomplete without another packet. A reset with old full queue checks exactly 4,096 new-epoch drops, excluding 32,768 prior-epoch drops. Final combined build/run result follows when returned by the integration worker.

Final analysis gate confirmation, 19:04 UTC: the rebuilt actual application passes AnalysisPluginBindingTests and AnalysisPluginEditorTests, including all truthful-history, startup-tail-overrun and reset-epoch drop-count regressions above. Exact evidence: `evidence/native-build/reused-tests/AnalysisPluginBindingTests/{run.log,result.json}` and `evidence/native-build/reused-tests/AnalysisPluginEditorTests/{run.log,result.json}`. DSP/kernel, control IDs, latency and protected limiter sources were not changed by these final validity fixes.
