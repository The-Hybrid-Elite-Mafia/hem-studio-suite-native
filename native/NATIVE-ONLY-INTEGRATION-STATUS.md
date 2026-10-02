# Native Studio Suite integration status

Frozen source milestone, 2026-10-01. Native Linux build and focused verification passed. This is a usable 13-stem processing-studio source milestone, not a finished arranger DAW or Windows release.

## Provenance and immutable baseline

Recovered host: HEM-Studio-Suite-Native-v0.10.zip, SHA-256 `88cbf7b3cf8a09d6326b152a36f03c1d3d0baccfcdc4d17a3783a08d3fdda663`. The original archive/extraction and earlier delivered snapshots are preserved. Current work extends the recovered source rather than replacing it with a new host.

Exact Task #2B files remain unchanged:

- Source/CoreBaseline/Engine.h: `84a3829568bc0cb5c99280769bbf9e72f128d5015a8d48e0a3a24debf582217f`
- Source/CoreBaseline/Engine.cpp: `d409243bff6a3f646d9af5726c64c99b378017845bbf295a61f7e1de710802a4`

No experimental limiter is adopted. CoreMasterStage is an integration adapter to the existing hem::MasterTruePeakLimiter. Its reported latency, not a hardcoded delay, drives alignment: 237/253/445 samples at 44.1/48/96 kHz.

## Current implementation

1. C++20 / JUCE 8.0.10, 48 kHz preferred, actual device rate/block used for preparation. Stereo file tracks keep their original routing; host delay helper is named HostDelay to avoid the baseline namespace collision.
2. Thirteen synchronized file players, eight serial inserts each, actual registry/native processors, gain/mute/solo, safe processor publication and retirement. No-device preparation supports editing/import/session/export without waiting for nonexistent audio callbacks.
3. One main window with Home, 13-strip Mixer, adjustable split/expanded native Plugin view, category/search Vault, fixed-routing signal telemetry and Settings. Ancillary analysis, monitoring, vocal and export panels remain embedded. Navigation does not stop transport.
4. Twelve actual native processor registrations, eleven usable as track inserts and one monitoring-only. All 98 parameter bindings (84 numeric, 14 toggles) are connected to real processor state. The vault sizes from the registry; additional entries require real implementations, not decorative cards.
5. Source-drawn functional knobs, sliders, buttons and illuminated labels, approved ghost/crown branding and original chassis. Buttons illuminate according to real state. New concept faceplates are not claimed as wired skins.
6. Exact emitted-output stereo sample-peak/RMS meters, per-track post-insert/trim meters, independent L/R levels, defined dBFS scale, release/hold and clip/reset. Native processor measurements now distinguish unprocessed audio, silence, undefined correlation and actual signed gain/GR. The FFT/waveform tap is pre-listening/pre-final-master; analytic EQ response is labelled as a control target.
7. XML/JSON session save and staged whole-bank recall, retained source/inserts during import, stale-generation rejection, source unload and confirmed New. New/unload never delete source WAVs.
8. Real asynchronous offline mix export: private 13×8 snapshot; sample-rate conversion; gains/mute/solo/bypass; unchanged final master; exact fixed-latency removal; bounded streaming; progress/cancel. Default is 48 kHz stereo float32 WAV through the longest loaded source plus 2 seconds, UI tail 0–30 seconds. Cursor and loop are ignored; listening EQ/calibration/live mic are excluded.
9. Atomic no-replace publication for export and recording WAVs/sidecars. Existing bytes and raced destinations remain intact; recording failures retain recoverable partials. A successful WAV with sidecar conflict reports its metadata warning truthfully. Hard-link-capable destination filesystem required.
10. Normal close/quit checks actual serializable session dirtiness and active work. Keep open is non-destructive. Approved shutdown waits asynchronously for recording/session finalization and cancelled export/import. Save failure or chooser cancellation preserves the open session. No autosave/crash recovery is implied.

## Verified milestone

25 distinct test programs have passing evidence across the recovery/integration milestones; the current changed subsystems were rebuilt and rerun. See VERIFICATION-2026-10-01.md for the per-program split and exact limits. Standard CMake configuration succeeds; the final native application links and starts on Linux. Final executable SHA-256: `972d9471041b97d06f6a722ae997277071607fe8c8a982ecfcc41feb0f8c06eb`.

Actual desktop checks include functional knob/default reset, real light states, import/Apply, native insert replacement, vault search, embedded export producing a verified WAV, responsive mixer at 1280×720, no fabricated unprocessed-plugin meter, Keep open, Cancel new, and confirmed reset of the disposable QA session. This cloud desktop exposes no physical audio device.

## Remaining completion gates

- Windows compilation, packaging/installer and physical driver/device acceptance. A no-install VS2022 build script is supplied; WASAPI is the default. ASIO requires a separately supplied SDK. No Windows toolchain or SDK was installed.
- Hardware playback/recording/loopback, hot-plug, full 13×8 load deadlines, disk-full/power-loss/long-session stress, and broader accessibility/DPI acceptance.
- The independent master-limiter reconstructed-peak and listening-quality acceptance problem remains open. GUI or adapter parity does not certify it.
- The recovered handoff explicitly carries generic VST3/AU hosting, automation and opaque external-plugin state recall. Those are not implemented by the recovered native registry and remain a separate major milestone.
- There is no arranger timeline, clip editing, MIDI, take placement on a timeline or common loop-range editor. Current Loop repeats each file at its own length. Offline export runs once from zero regardless of Loop.
- The vault UI is registry-driven but contains only 12 real native entries. No 100-plugin library or 32 new DSP modules is claimed. Further exact faceplate integration needs real parameter/layout contracts.
- The temporary vocal rack is saved in take metadata, not recalled as a fourteenth session track; learned runtime detector histories are not session automation/state.

The original handoff said an editing timeline was not established complete but did not define an arranger-editing contract. This freeze documents that gap without inventing a new arranger. Build and individual test commands are in README.md.
