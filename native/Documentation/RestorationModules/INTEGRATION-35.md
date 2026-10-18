# Restoration3 integration, 2026-10-07

Status: checkpoint007 functional source validation passed on Linux; frozen for verified private backup. Performance/platform release acceptance remains blocked.

## Existing source, not new duplicate engines
Approved02 Dynamic Resonance Suppressor,06 Breath/Mouth Control and09 Clip Repair are appended once after the exact32-entry registry prefix. New integration IDs are dynamic-resonance-suppressor, breath-mouth-control and clip-repair. Their recovered APVTS roots, parameter order/ranges and neutral semantics are preserved. These registry slugs establish the first integrated session identities; no prior integrated slug was recovered.

The three recovered DSP headers match the retained recovery payload exactly. ClipRepairCore itself was earlier reconstructed as executable-equivalent source from retained implementation evidence, so matching that payload is not proof of byte identity to the lost original. See RECOVERY-PROVENANCE.md. All previous DSP and protected core/limiter files remain unchanged versus006; evidence/restoration35-dsp-preservation.json records exact hashes. The existing spectral-restorer engine remains separately preserved.

## Adapter and host architecture
Constructor defaults are canonicalized through the existing host parameter conversion, so raw state, reset, preset comparison and serialization use the same floating-point values without changing semantic defaults. Three wrappers retain their recovered core processing and observer behavior. Atomic lifecycle generation guards reject GUI snapshots crossing reset/reprepare; release and invalid prepare expose no ready meter or stale audio buffer. Shared independent stereo peak/RMS publication uses the existing levels-only path, without adding generic FFT worker threads. Sample-rate and fixed latency contracts remain unchanged.

Main host Close is an asynchronous message-thread action guarded by SafePointer and exact current-editor identity. Repeated clicks are coalesced by lifetime invalidation; replaced editor, retired processor and destroyed owner callbacks cannot affect a newer selection. Artwork09 has no invented Close/preset controls and uses existing host Back/Escape.

## Actual artwork and truthful measurement
Approved original PNGs02/06/09 remain immutable. The95 pictured items are individually inventoried in CONTROL-BINDINGS.csv. All30 APVTS parameters are wired, including both pictured06 Bypass buttons to the same parameter. OUTPUT on06 is measured output level, not a gain control. Built-in full-state presets/Previous/Next/Close exist only where pictured; actual native popup interactions remain unverified in this environment.

Current inner-material dial rendering preserves authored outer rings. Input/output use independent L/R sample peak and white300ms RMS markers. Reopened frozen publications stay dark, as do unavailable/released/reset states. Plot traces are clipped away from captions and illustrative baked signals are removed.02 uses actual32 constant-Q band centers,80Hz..min(18kHz,0.42*sampleRate), and is not labelled as FFT.09 shows aligned dry, normal output and applied repair contribution; its detected-event lamp lights independently of repair amount. Display history lengths are calculated from actual decimated sample spacing, rather than nominal rounded durations.

## Fresh evidence so far
- Core suite:1,825,181 assertions PASS; Clip Repair:310,552 checks PASS
- Both DSP suites: AddressSanitizer/UndefinedBehaviorSanitizer PASS with leak detection disabled. LeakSanitizer cannot finish under this cloud runtime's ptrace environment; no leak-check PASS
- Adapter bindings:208,993 assertions PASS, canonical defaults/binary recall, strict state rejection, XML/JSON sessions/presets, all3 insert impulse latency at six rates
- Adapter lifecycle/correspondence:9,932,478 assertions PASS,288 wrapper/core cases, independently computed stereo peak/RMS, invalid-rate recovery and no stale release/reset telemetry; zero measured callback/reset allocations. This adapter probe does not measure deallocations
- Original UI:5,952,407 assertions PASS,38 deferred rasters, real mouse/keyboard/reset/host control binding, full presets, duplicate bypass, outer rings, freshness and neutral clip detection
- Independent meter aggregate:35 processors PASS, explicit16/6/4/3/3/3 family partition and restoration four-lane pixel/mutant tests at1x/2x
- Host Close/Back:6 repeated close cycles PASS, stale/replaced/retired/destroyed-owner safety and09 Back/Escape
- Aggregate parameter bindings:594 PASS;13x8 XML/JSON session recall PASS again against the final production object graph
- Final Linux app, workspace and full-host graphs compile/link successfully
- All 12 final production-object gates PASS; source and linked-object hashes reverified
- Final workspace: all 35 editors and 105 Back/Escape lifetime cycles PASS, with exact state and sample-exact no-editor-twin audio parity
- Numerical regression selection: 16/16 PASS, 129.64 seconds, including protected baseline/master, Delay, nonlinear and both restoration suites
- Completed isolated worker evidence is retained with immutable hard links to avoid duplicate disk copies; backup treats each named file as regular file content

## Test corrections, not masked product failures
The recovered strict-state fixture assumed APVTS children followed parameter specification order; JUCE sorted the children alphabetically. The fixture attempted sensitivity maximum+1 on the attack field, a valid attack value. Tests now address children by parameter ID and retain strict rejection assertions. The initial host Close fixture used a compact preview-only host size; the host intentionally disables interaction there. The functional fixture uses1920x1200 while keeping existing compact-preview safety unchanged.

## Outstanding acceptance
Previous dense30 workloads still miss deadlines, including the tested13-chain and full-track cases. No full-load real-time capacity PASS, Windows/audio-device/listening/installer/external-VST3 PASS or native popup/file-chooser PASS is implied. Five approved engines are still absent and host/original chassis work remains. Preserve the separate protected master-limiter contracts, including the-0.95dBTP lane.
