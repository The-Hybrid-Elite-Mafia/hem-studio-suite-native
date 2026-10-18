# Main HEM Studio Suite: 2026-10-16 checkpoint 4

SOURCE ONLY: unbuilt and untested integration on the verified 0.11.3 source. This is not an installer or a claim of Windows readiness. Installed 0.11.2 is unchanged. Since the user's instruction to leave testing to their PC, no compilation, program execution, or audio testing has been performed. Backup integrity checks are not functional tests.

## Restore
Use a separate dated working copy. Restore original base Drive file 1Xs6e0N_0l407ACWY52lmZegAoBd9UQbg, then 0.11.3 overlay 1Soqu3epqc5boCJoJTidVRDHnq-46r99_, then this cumulative patch. The base + overlay's 300 production inputs were hash-verified before editing. PATCH-MANIFEST-CHECKPOINT4.json records each changed/new file and baseline/current SHA256. Review mismatched local baseline hashes rather than blindly overwriting PC work. Earlier checkpoints remain retained.

This supersedes checkpoint 3, which was backed up before the final EOF edge correction.

## Cumulative repairs
- Utility panels retain intrinsic minimum size rather than ratcheting larger after opening in a large viewport. Export resize/reopen and cross-panel regressions are prepared.
- Existing read-ahead EOF preparation now maps an explicit end position to the exact ceil-sized output frame count, avoiding one final replayed frame at noninteger resampling ratios. Spectral Apply preserves explicit EOF intent; exact/beyond-EOF regression cases were added to the existing alignment test source and remain unrun.
- Back navigation captures the current origin for a fresh editor entry, preserves it for internal editor replacement, and closes actual editor/clip ownership on return. Clip Escape is deferred with lifetime guards; Reference Match Escape uses the host Back route.
- These source defects do not prove the cause of the original Windows Back-click, Vault-typing, or maximized Export-click failures. Those remain native acceptance items.

## Selected-track spectral integration
Access from File > Selected-track 2D / 3D spectral scanner, or Analysis > Selected-track 2D / 3D spectral scanner. Existing live master analysis remains available.

The reusable Workbench measured FFT/attenuation core and rotatable 3D/2D panel are adapted into MAIN, without its session model or standalone transport. The graph is measured amplitude over time/frequency, with time/frequency selection and tapered attenuation. It is not an automatic artifact classifier, AI restoration system, or 3D source isolation engine.

MAIN owns a serial ThreadPool for source decoding, SHA256 verification, FFT, attenuation, float32 WAV writing and read-ahead preparation. Callback additions use preallocated stereo buffers and existing prepared read-ahead transports, bounded sample arithmetic and atomics; no FFT, file I/O, string construction, allocation or locks are introduced there. Original/processed/delta audition is privately monitored through the existing listening path and final limiter. Delta is original minus processed.

Apply requires stopped transport and unchanged selected track/generation/source/device context. Output must preserve sample rate, channel count and exact frame count. It switches only the selected track source, retaining the stopped timeline position. Original media is never overwritten. New WAV and JSON provenance are published using no-replace semantics into a sibling HEM Spectral Renders folder; committed output is retained for ordinary session save/reopen. Keep that folder with the project media when moving a session. A read-only source folder makes Apply fail without replacing the original.

Undo/Redo is one-level and current-session only. Normal session saves retain the active rendered source path, but do not serialize spectral undo history. The original path/hash is also retained in the generated JSON provenance. Imported/unloaded/recalled sources clear stale in-memory history. Existing track processors, artwork and parameter contracts are preserved; a spectral result is not mislabelled as a pitch/time clip recipe.

Cancellation, navigation and stale generation rejection detach callback access before worker retirement. Every new play/seek/loop/recovery transport request closes the scanner before staging its new epoch, preventing a pending Apply from restoring an obsolete selected-track position. Heavy payload destruction and temporary-file cleanup are queued on the serial worker. Published-file ownership is tracked so failed publication cannot remove a pre-existing target. Final application shutdown drains outstanding work before destroying the disk thread.

Bounds: mono/stereo, 8–384 kHz, nonempty finite samples; 512 MiB combined original/output working budget with 16 MiB staging reserve, 512 MiB source-file ceiling, and 30-minute absolute ceiling. The combined budget means about 5.64 minutes of stereo 96 kHz audio. Oversized input is rejected, not truncated. FFT size 2048, hop 512, visual grid at most 96 time by 64 frequency bins. This is finite-window frequency resolution, not arbitrary surgical resolution. Native 96 kHz behavior/performance is not yet qualified.

Workbench-derived files retain AGPL-3.0-or-later notices and license. Assess distribution obligations before any public release; this checkpoint is private source work.

## Verification boundaries
Earlier utility-sizing C++20/sanitizer/mutation checks and existing geometry/neutral-mapping checks passed before the no-testing instruction. They do not validate this new integration. New SpectralCoreTests, EditorNavigationChecks, and expanded UiInteractionTests are prepared but unrun. Static review checked pinned JUCE 8.0.10 APIs, thread ownership, cancellation, publication ownership and host wiring. Static review cannot establish compilation or runtime correctness.

## Windows continuation and acceptance gate
1. Build the isolated copy using the existing JUCE 8.0.10, pinned MIT VST3 SDK, MSVC and Windows SDK setup; C++20. This cloud lacks the complete native GUI toolchain/SDK. Do not install this candidate over the working app before verification.
2. Compile and run hem_spectral_core_tests, hem_utility_panel_sizing_tests, existing native regressions and updated host UiInteractionTests. Verify Release assertions remain enabled where existing tests need them. Spectral tests use explicit throwing checks.
3. Check actual Back mouse/Escape routes from Mixer and Rack, internal clip/Reference replacements, real Vault typing/focus, File menu, slot-2 plugin insertion, Export click at normal/maximized window and DPI, large-to-small reopen and panel switching. Complete the outstanding 40-module audit separately.
4. Scanner: load known mono/stereo WAVs at 44.1/48/96 kHz; inspect measured 2D/rotatable 3D; select time/frequency, preview, audition original/processed/delta, rapid Stop and navigation during preparation/render/audition. Confirm no audio after cancelled pending start.
5. Verify Apply/Undo/Redo exact source rate/channels/frames, untouched original hashes, stopped cursor alignment across all 13 stems, unaffected insert chains, saved-session reopen from durable WAV. Verify history is intentionally absent after reload. Check render-folder failure, source edits during jobs, cancellation, device change, track selection change, close during jobs and out-of-budget inputs.
6. Measure callback underruns/deadlines and memory at intended 96 kHz workloads; live speaker output and maximum plugin load remain unqualified. Only then create a separately versioned release candidate with accurate evidence.

Package contains source, tests, license and documentation only; no credentials, active user sessions, caches, intermediate objects or unrelated applications.
