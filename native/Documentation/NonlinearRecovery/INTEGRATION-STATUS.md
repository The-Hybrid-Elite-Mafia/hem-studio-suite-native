# Nonlinear recovery integration, 2026-10-07

Status: checkpoint006 functional source integration verified on Linux; frozen for private backup. Performance/release acceptance remains blocked.

## Recovery and identities
Existing recovered Parallel Distortion and Exciter are appended after the exact prior 30-entry registry prefix, yielding 32 processors. Existing Tape remains its original `tape` ID and `TAPE` state type, in its prior position. This count is a processor-registry count, not completion of the 31 approved plugins plus host. No duplicate engine is added for Tape.

NonlinearCore and NonlinearTelemetry are exact recovered bytes; see RECOVERY-HASHES.json. Stale branch-wide replacements of shared FinishingProcessors, Processors and SessionState were not adopted. Existing Vocal spectrum, all prior input metering, Delay history-reset support and family state/resource migration remain present.

## Shared helper and callback design
Only the residual-output option required by the recovered nonlinear DSP is added to OversampledSaturator. Its optional argument defaults false; the old result expression is preserved on that path. A test-only frozen 005a helper is namespaced separately, and 442,368 samples across 27 drive/bias/mix configurations, two channels and midstream resets match bit-for-bit. The frozen helper is never an application engine.

Parallel/Exciter retain the recovered atomic waveform publication and use existing lock-free shared level snapshots without adding analysis worker threads. Tape retains its existing analysis-worker behavior. Added prepare/release guards clear invalid or released buffers, invalidate stale meters and distinguish fresh audio after reprepare. GUI presentation never mutates DSP state. No callback allocation or mutex was added by these adapter changes; numerical and lifecycle allocation probes pass, with their stated C++ allocation interception scope.

## Original artwork
Original approved PNGs 26/27/28 are used. Independent faceplate staging checks stationary dial metal/rings, moving live pointers, controls, selector arrows, stereo peak/RMS displays, fresh-publication epochs, stale/reopen/release/reprepare, and authored signal masks. The final staged UI is adopted after hash checks; its factory-based fixture passes all three editors, 33 captured cases, exact selector-arrow pixels/hit targets, caption-safe ±2 FS endpoints, stale/reopened/released/reprepared states. Independent aggregate meter tests are adopted and pass all 32 processors, including peak/RMS pixel calibration, negative mutants and lifecycle gates at 1x/2x. Native window/dialog behavior is not established by memory raster tests.

## Evidence observed before final UI merge
- Numerical recovered nonlinear suite: PASS at 22.05, 44.1, 48, 96 and 192 kHz; neutral, bypass, stereo, automation, latency, partition invariance and allocation checks; alias/harmonic tests PASS
- Lifecycle stereo levels, actual output peak correspondence, release/reprepare and unchanged legacy worker defaults: PASS
- Full-host recovered nonlinear integration: PASS after updating its obsolete catalog count and making its editor visible before JUCE hit testing; state rejection/roundtrip, bindings, meter publication, preserved Tape state type and rack latency checked
- Legacy saturator default path: 442,368 bit-exact samples PASS
- Baseline/shared-helper numerical regression selection: 14/14 PASS (133.13 seconds), including master limiter, finishing, Delay and nonlinear suites
- Aggregate 32-processor bindings: 564 PASS; 13 x 8 XML/JSON session/resource recall PASS; native tempo, real outer callback, legacy telemetry and Delay state/export parity PASS again against the final production object set
- Final application, workspace and full-host nonlinear targets: compiled/linked successfully
- AddressSanitizer and UndefinedBehaviorSanitizer nonlinear DSP run: PASS with leak detection explicitly disabled. LeakSanitizer could not finish under this cloud runtime's ptrace environment; no leak-check pass is claimed
- Final aggregate pixels: all 32 processor gates PASS; final workspace: 32 takeover editors, 96 Back/Escape cycles, exact state and sample-exact audio parity PASS
- PNG artifact timing correction: old artifact-writing test crossed Delay's unchanged 250 ms freshness window. Three diagnostic trials measured 594/427/432 ms and proved the pixels correctly darkened; a fresh callback recovered live pixels. Encoding was moved after both scale assertions. Production freshness and calibration thresholds were not relaxed.
- Final narrow test sources and 45 shared production object hashes verified against the retained records

## Acceptance blockers
The prior dense 30-processor workload has deadline misses, including 4,500/4,500 misses for tested 13-chain and full-track scenarios. This remains a performance acceptance blocker. Linux numerical or raster success is not Windows, audio-device, listening, installer, external-VST3 or popup/file-dialog acceptance. Five approved engines have not been recovered, and other restoration/host work remains. Preserve the separate master-limiter contracts, including the protected -0.95 dBTP test lane.
