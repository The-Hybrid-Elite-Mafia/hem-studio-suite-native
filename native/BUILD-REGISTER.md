> Historical recovered design/status document. Current implementation and verification take precedence in README.md, INTEGRATION-STATUS.md and VERIFICATION-2026-10-01.md.

# HYBRID ELITE MAFIA — Native Build Register

Updated September 29, 2026 (America/Chicago).

## CURRENT PACKAGE — START HERE ON THE PC

**HEM-Studio-Suite-Native-v0.10.zip — source version 0.10.0**

This is the current COMPLETE SOURCE FOLDER. It includes the preceding packages' code plus the latest modules and monitor routing. Extract it into a new folder. Do not combine old ZIPs with it. It is a C++ source package, not a compiled Windows installer.

## Retained archive history

1. HEM-Studio-Suite-JUCE-Foundation.zip — earlier native foundation and master faceplate.
2. HEM-Studio-Suite-Native-Modules.zip — v0.2 core native processors and single-file host.
3. HEM-Studio-Suite-Specialty-Queue.zip — v0.3 specialty DSP, windows and eight tracks with eight inserts each.
4. HEM-Studio-Suite-Native-v0.4.zip — archived; adds Vocal Presence & Resonance, Triode Tube, Tape and monitor-only XM5 User EQ.

5. HEM-Studio-Suite-Native-v0.5.zip — archived; 13 stem tracks with eight inserts each, device-neutral Audio Output, optional monitor EQ, common-position Pause and actual device-name telemetry.

6. HEM-Studio-Suite-Native-v0.6.zip — archived; adds a 13-WAV Explorer drop zone, worker preparation/read-ahead/resampling, all-or-nothing batch staging, explicit Apply while stopped, and off-thread source cleanup. Existing insert chains remain intact.

7. HEM-Studio-Suite-Native-v0.7.zip — archived; adds complete 13-track / 104-slot XML session save/recall, background whole-bank reconstruction, validated all-or-nothing restore, and a mandatory final -1.0 dBTP master limiter after monitor EQ.

8. HEM-Studio-Suite-Native-v0.8.zip — archived; XML/JSON PresetManager, bounded background stereo FFT/waveform analyser, native analysis window, and an independent direct/corrected monitoring router with background measured-profile import.

9. HEM-Studio-Suite-Native-v0.9.zip — archived; dedicated microphone selector and vocal tracking panel, simultaneous 13-track backing playback, raw mono or printed-stereo float-WAV recording worker, independent natural-latency eight-slot vocal rack, explicit software/direct-interface monitoring modes and placement sidecars.

Earlier versions are archives, not dependencies. HTML/browser projects remain separate and are not needed for this native build.

## Current registry — 12 processor classes

Track insert effects: S.I.P.A.; Surgical Master EQ; VCA Compressor; True-Peak Limiter; D.H.T.A.; Transient Shaper; 808 Sub-Bass Manager; Artifact & Spectral Restorer; Vocal Presence & Resonance Architect; Triode Tube Saturator; Tape Saturator.

Monitoring only: Optional Monitor EQ, with no device-brand requirement. It is flat/disabled by default, is prohibited from track insert slots, and processes a separate master copy. It disables after device changes. Users select available computer speakers, soundbars, headphones or audio interfaces through Audio Output. Physical routing compatibility remains subject to the OS/driver.

All twelve have native editor classes. The updated 13-stem summing test is supplied for Windows execution. Numerical DSP tests and JUCE syntax checks passed; linked Windows runtime/UI and device tests have NOT yet run.

## Locked visual direction

Heavy smoked obsidian glass; cyan framing; amber active controls; fixed readable numbers; HYBRID ELITE MAFIA / STUDIO PLUGINS branding. Dedicated plugin windows and a per-track rack. Exact approved plugin-faceplate overlays remain unfinished; current plugin editors are drawn native controls.

## Importer status

Complete class and host integration are included. Drop 1–13 mono/stereo WAVs or use Browse WAVs. Track assignment uses the received file order. Preparation does not replace current playback. Pause/stop then Apply; all tracks rewind. Device/loop changes invalidate staged imports. Read-ahead limits RAM use; original WAVs are never rewritten. Source compilation checks passed; the new Windows import test and actual Explorer/audio runtime checks remain pending.

## Session and final-output status

Version 0.9 is the current full source package; it retains the v0.7 session/limiter work. SAVE SESSION and LOAD SESSION retain track names/file paths, all gains/mutes/solos, every ordered plugin ID/parameter/bypass state, input/output gain, loop and position. Recall prepares the full replacement off-thread, swaps only when stopped, and frees old readers/processors off-thread. Monitor EQ values are stored but recalled disabled. Runtime DSP histories and the learned noise profile reset. Audio device selection stays local.

The mandatory final limiter is after monitor EQ and before the unchanged device copy. It has no bypass, fixed -1.0 dBTP ceiling and 5 ms lookahead. Its finite 8x/32-tap reconstruction bound is numerically tested, not a universal physical-DAC guarantee. New overload/non-finite/latency/allocation tests passed at 44.1, 48 and 96 kHz. Host and XML test sources passed JUCE header syntax checks. The XML integration test and full application still need linking/running on Windows.

## Background systems status — v0.8

PresetManager adds JSON alongside XML and worker-safe list/rename/delete APIs. The host uses the save/load picker. The analyser uses a bounded SPSC queue, 8192-point stereo FFT and 64-frame peak buckets; overload drops only visual data. Monitoring supports direct or corrected listening and up to twelve imported calibration bands, always followed by the final master limiter. The master source is untouched. No invented XM5 coefficients are included; measured device/mode-specific profile data is required. Bluetooth codec selection remains outside this router.

Executed: real JUCE analysis-worker tests (including overflow, rate epochs and allocation-free push), calibration JSON and coefficient tests, and direct/corrected routing tests at 44.1/48/96 kHz. JUCE source syntax checks passed. Full XML/JSON processor/session tests and Windows GUI/hardware integration remain pending. CMake includes eleven test targets. BACKGROUND-SYSTEMS.md gives the new complete code blocks; SOURCE-CODE.md retains all application code.

## Live vocal recording status — v0.9

Complete source integrates live input capture before the host overwrites its audio buffer. It records separate 32-bit float takes alongside the thirteen background tracks. Disk writes and finalization run on a dedicated worker; overflow/write errors retain partial takes rather than silently dropping sections. Input selection, record/stop, monitoring and eight live inserts have native controls. The live rack reserves no latency for empty slots; only actual processor delay is added.

Direct-interface mode disables software vocal return; hardware monitoring must be enabled on the interface itself. Software monitoring is not zero latency. Physical device changes are gated to stopped playback/finished recording. Saved take metadata includes device and DSP delays plus estimated placement; actual alignment needs a loopback check. Vocal rack state is in take metadata, not in the thirteen-track preset schema.

Executed: actual JUCE mono/stereo float-WAV recording tests, exact frames/samples, background finalization, placement metadata and allocation-free capture push. New source passed JUCE syntax checks. Native input/rack test is supplied; actual Windows microphone, full-load simultaneous playback, UI and device latency testing remain pending. CMake now includes thirteen test targets.

## Remaining release work

- Compile/link in Visual Studio and run all CTest targets.
- Test per-track synchronized playback, looping, hot-swapping, bypass, device changes and real-time CPU load on the PC.
- Complete exact plugin image/knob/meter mapping and visual review.
- Execute Windows session round-trip/recovery tests; graphical missing-file relink remains future work. XML recall currently reports missing paths and leaves the session untouched.
- Offline WAV export that excludes monitor EQ.
- Recording, timeline editing and external VST scanning are not supplied.
- Spectral restoration is deterministic DSP, not trained AI.
- Rack delay is approximately 48 ms per parallel track, plus the mandatory 5 ms master limiter and device buffering. Reordering requires stopped playback.

## Proposed future modules

See PLUGIN-ROADMAP.md in the current package for the ranked 12-module proposal with official product/workflow references. First choices: Reference & Loudness Lab, Click & Plosive Repair, Multiband Dynamics Architect. These are planned, not implemented.

## Weekend instructions

Use the current ZIP only. Open README.md for Visual Studio/JUCE/CMake steps. SOURCE-CODE.md provides full copyable code blocks. Older archives remain intact as recovery points.



## Archive integrity

- HEM-Studio-Suite-JUCE-Foundation.zip
  - Bytes: 276279
  - SHA-256: 3266fa8d9b639a48e2fc294155988aa519a58ae481f49db2d3401fd9eaa6b2d9
- HEM-Studio-Suite-Native-Modules.zip
  - Bytes: 293058
  - SHA-256: 6e9d64757996e413ef4f5e31b9d7e22b3d178c9b79d5430c79c71d3fd03e43b0
- HEM-Studio-Suite-Native-v0.4.zip
  - Bytes: 353064
  - SHA-256: 39d16fcbf95b1179d7c8a8d4a8bbd1f251d7d935e54b51fb9e04cb9302cf5c21
- HEM-Studio-Suite-Native-v0.5.zip
  - Bytes: 356873
  - SHA-256: c8dca308c7af0ee312c49f711368c252c15dafbea894bc303e4453d902a4b1a9
- HEM-Studio-Suite-Native-v0.6.zip
  - Bytes: 370411
  - SHA-256: bf1be484378732a4ffd0bc0dff6fadb4e6e25ffd32651cfafa8731b2d941bee7
- HEM-Studio-Suite-Specialty-Queue.zip
  - Bytes: 334793
  - SHA-256: 52e63f7650258f0524f5e402c750ae28a667bb99b128e24fc723a48debcd39a7

- HEM-Studio-Suite-Native-v0.7.zip
  - Bytes: 394187
  - SHA-256: be2a29fdef8b56812da0b553526d8d828c07ab3e2e7e1f94cdc13862a796b22a

- HEM-Studio-Suite-Native-v0.8.zip
  - Bytes: 447143
  - SHA-256: 4d08cdc03b6b8a1396a8d71fb2bcfd6e3a7f3bd43e9563f2b401b024387fa770

- HEM-Studio-Suite-Native-v0.9.zip
  - Bytes: 481268
  - SHA-256: a66497b0cb905bbef24b562139b5ec0495c962d34a0b402ba70234b447b6c699

## v0.9.1 — monitoring notice

Archived: HEM-Studio-Suite-Native-v0.9.1.zip. Complete source package. Added an always-visible mode-specific banner to the vocal tracking window. Hardware-direct selection explicitly requires enabling Direct Monitor on the interface; software return stays off by default. No audio-routing changes. Panel source syntax checked against JUCE 8; Windows visual and device tests pending.

Package SHA-256: `b6a23acd2bd01e4749efd5f1f6b37d7bbd89e61460397c8769a8a982fc6cad9d`

## v0.10 — current device-neutral routing

CURRENT: HEM-Studio-Suite-Native-v0.10.zip. Complete source, not an installer. Native platform-driver/device/channel selector with rescan; ASIO and WASAPI Windows build flags; CoreAudio macOS microphone permission declaration. Vocal capture expanded from two to up to 128 enabled input channels, with driver channel labels and bounds checks. One mono or two stereo physical output channels. No brand assumptions. Startup microphone disabled; enable desired channels in I/O settings. Sample rate/block request only when advertised. Device changes require stopped playback/recording; reset disables software monitoring.

Validation: changed C++ translation units syntax checked. New dynamic-input regression test included but cannot link/run in this environment without JUCE GUI/processor objects. Actual cross-platform driver/hot-plug and UI acceptance tests pending. No universal hardware compatibility certification claimed.
