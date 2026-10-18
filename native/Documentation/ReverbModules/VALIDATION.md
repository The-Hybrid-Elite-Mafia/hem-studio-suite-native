# Reverb recovery validation

This is a reconstructed validation record, not a byte-verified copy of the old document. Source/test byte identity is independently verified by RECOVERY-MANIFEST.json.

## Fresh recovery tests

The retained numerical author restored exact sources and reran these tests in the new runtime:

- AlgorithmicReverbTests: 3,284,537 assertions, strict GCC and AddressSanitizer/UndefinedBehaviorSanitizer, zero observed process/reset allocations
- ReverbSchedulerTests: 9,430,825 assertions, strict GCC and AddressSanitizer/UndefinedBehaviorSanitizer, zero observed process/reset allocations; full 3,841,536-tap reference and 12 host block sizes

Fresh logs are under the sibling reverb-retest-results directory. RECOVERY-ALGORITHMIC-SCHEDULER.json records the exact files and recovered test dependency. Leak detection was disabled because the traced executor cannot run LeakSanitizer. The recovered FoundationAllocationProbe dependency must be compared with the assembled baseline when available.

## Historical tests, not fresh recovery passes

Before the runtime was lost:

- Convolution suite: 432,903 assertions and ASan/UBSan; no callback allocations/frees
- Binding suite: 48,040 assertions and ASan/UBSan, including strict field/schema and transactional resource tests, deterministic queued-file generations, real staged SessionState parameter-tree XML round-trip, all parameter controls and whole-chain latency impulses
- Editor suite: six idle and two live software-rendered native PNGs; authored dial rings preserved, full idle meter apertures cleared of original signal/glow, no clipped TIME label, actual source-IR and measured wet-RMS plots
- Algorithmic 2 s nominal RT60 measured 1.974 / 1.979 / 1.972 s for Hall/Room/Plate; 133-second full-control stress peak 0.555214
- Scheduler independent full-response maximum error 1.4704e-8

Those old logs/rasters are not being represented as newly generated recovery evidence. The JUCE-dependent suites, screenshots, protected baseline parity and full integrated application must be rerun after rebuilding the restored host/dependencies.

## Known integration failure still requiring its separate fix

The shared stricter integrated24 gate found algorithmic damping starts at raw 7999.99951171875 instead of its declared 8000 Hz, then normalized recall snaps to 8000, breaking exact state equality. This occurs before editor construction. A dedicated reverb startup assertion previously allowed too much floating-point slack and did not catch it.

A constructor-only canonicalization recipe was independently probed successfully before the filesystem loss. It uses existing parameter notification/conversion APIs and synchronizes the owned ValueTree cache. The exact recovered source intentionally does not include that later, un-applied correction. See PENDING-CANONICAL-DEFAULTS.md and the separate patch. Do not weaken shared startup/state assertions, alter parameter ranges/defaults/skew, or modify original DSP kernels to conceal the issue.

Windows, real device/window interaction, subjective listening, full registered-ID XML/JSON session round-trips and maximum-load realtime qualification remain separate gates. Extreme long-IR/small-buffer CPU limitations are retained in SCHEDULER.md and INTEGRATION.md.
