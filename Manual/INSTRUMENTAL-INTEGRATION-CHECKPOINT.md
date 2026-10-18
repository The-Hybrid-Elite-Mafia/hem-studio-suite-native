# MAIN Studio Instrumental Builder source integration

Prepared 2026-10-16 on the cumulative checkpoint4 MAIN Studio workcopy. SOURCE ONLY: no new build, compilation, test execution, audio-device test or GUI test has been performed. The user's PC remains the verification gate. Earlier passing unrelated checks do not establish that this integration works.

## Source and permission
The user approved integrating the verified older Hybrid Studio 2.0.0 copy into MAIN. Donor archive: (private Google Drive link removed) . Archive SHA256: 6658d05273285f35b19a35abdcdd9665516df04e7e0addc80a665caf9456f72a. This is a saved source snapshot, not verified latest GitHub code. Its original files remain untouched. No code/assets from the unrelated hem-studio-plugins copy are used. The phone integration is a separate task and is not included here.

The donor has no LICENSE/third-party manifest in the recovered archive. Attribution is preserved; public distribution/license clearance remains unresolved. No paid API, remote generation service, paid sample pack or bundled instrument library was added.

## What was copied/adapted
The donor's sample-based pattern scheduler, voice/choke behavior, Catmull-Rom sample reading and lane gain/pan/pitch controls were adapted into a standalone standard C++20 worker renderer. The donor's full HybridEngine, master compressor/clipper/limiter, standalone transport, project manager and artwork were not substituted for MAIN's existing engine.

The builder has 13 sample lanes and 32 sixteenth-note steps (two 4/4 bars), tempo 40–240 BPM, swing 50–75%, repeated patterns and an explicit tail. Per-lane sample selection, gain, pan, pitch, mute, solo and choke controls feed a frozen render plan. The original trigger-then-mix semantics mean muted/unsoloed lanes can still choke another lane. Chokes and same-frame events are evaluated across all lanes on the same timebase; rendering each lane independently would be incorrect.

## Workflow
Open File > Instrumental Builder / 13-lane sequencer. Load user-owned samples, set steps and controls, and save the pattern to retain it across Studio launches. Navigation preserves the current builder draft and loaded samples while cancelling any active job; reopening restores the same controls. The draft is not part of MAIN's session schema, so use separate pattern Save before closing Studio.

Render writes all 13 synchronized stereo lane WAVs at 96 kHz/32-bit float in the background, including silent lanes, with a common exact frame count. Each output contains its lane signal before MAIN's insert chain and master path. The common donor preroll is removed consistently. Tail duration is explicit; it is not an automatic analysis of reverb/sample decay. Output above unity is retained as float rather than silently clipped or normalized.

Stage 13 Stems does not replace tracks. It submits a protected batch to MAIN's existing importer. All target tracks must be empty, both when staging and immediately before final source publication. Click the importer's ordinary APPLY when stopped to make the batch active. This explicit commit also sets MAIN's project BPM to the frozen rendered pattern BPM. Existing insert chains remain in place. To preserve a loaded session, keep the generated WAVs and import them later into a suitable empty session rather than overwriting sources automatically.

No real-time sequencer playback, MIDI keyboard input, AI music generation, piano-roll instrument synthesis, or sample library is claimed by this offline integration. Listen through MAIN after explicitly importing the render. Pattern previews/live MIDI would require a separate reviewed transport integration.

## Safety and limits
All sample decoding, source checks, pattern parsing/writing and WAV rendering belong to a serial worker. The core streams fixed-size blocks for 26 channels, rather than allocating a whole 13-stem song. No new audio-callback path was added. Final teardown drains queued worker-retirement jobs before destroying the pool; the same narrow correction was applied to spectral teardown so queued payload cleanup is not discarded onto the UI thread. Normal navigation/close retirement remains nonblocking. Main-device changes, active import/export/recording and other resource operations are gated through the existing host busy/close flow.

Completed output is durable and can be referenced by saved MAIN sessions. New destinations use no-overwrite creation; existing source files, donor files and previous exports are retained. Pattern saves use a new sibling name if the requested path already exists. JSON completion manifests are fully written, flushed and closed privately before atomic no-replace hard-link publication; filesystems without hard-link support fail safely. Private JSON staging links and incomplete render folders are retained rather than deleting potentially pre-existing paths. Failed/cancelled exports are incomplete directories and must not be presented as finished renders or staged for import.

Host limits: combined 13-stem output at most 4 GiB with a free-disk-space margin; current decoded plan at most 192 MiB, each sample at most 64 MiB and 120 seconds, each input file at most 128 MiB. Old/new immutable plan snapshots can temporarily overlap during sample replacement, bounded separately. Oversized inputs/renders are rejected rather than truncated. Core-level validation is broader than these host limits; the integrated host limits govern.

Donor Catmull-Rom pitch/sample-rate conversion is not a band-limited resampler. This integration preserves that source behavior and does not claim alias-free commercial-grade pitch conversion. Existing MAIN audio/DSP performance claims remain unchanged and unqualified for this candidate.

## Prepared verification, not executed
- InstrumentalCoreTests: deterministic partition-independent renders, shared13lane frame alignment, timing/swing, pitch/pan/gain, mute/solo/choke, validation, cancellation and tail behavior.
- ImportTests: protected-empty-target policy and rendered BPM survive asynchronous staging; explicit commit remains required.
- UiInteractionTests: protected instrumental batch cannot consume/replace an occupied target or change its BPM; builder draft controls survive navigation and reopening.
- Existing scanner/Back/utility/transport regressions remain in the cumulative checkpoint.

## Windows continuation
1. Restore original verified base, 0.11.3 overlay, then this cumulative source checkpoint into a separate dated copy. Review the manifest before replacing any independently edited PC source.
2. Compile with the existing pinned JUCE 8.0.10/MSVC/Windows SDK/VST3 SDK setup and strict C++20. Compile the new standalone hem_instrumental_core_tests plus importer and real-host UI regressions. Run only when the user authorizes PC testing.
3. Verify every grid cell, lane selector, control, sample chooser and save/load callback; malformed/missing/moved sample files; cancellation and rapid navigation; close while a worker or chooser is active; disk full/read-only/collision behavior; stale rendered result invalidation after edits.
4. Verify generated WAV headers and exact common frames/rate/channel count, original sample hashes, expected swing timing and choke ordering, output folder/manifest completeness, render limit errors, and matching output across worker block sizes.
5. Verify populated tracks are never replaced through the builder flow, staging does not commit automatically, explicit stopped APPLY updates BPM, all13 stems align, inserts remain, main export works, and session save/reopen resolves durable media.
6. Listen on the PC with known samples, including pitched/resampled material and tails. Measure 96 kHz performance and normal project workloads. No installer or release-readiness claim until these checks pass.
