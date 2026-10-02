# Hybrid Elite Mafia Studio Suite — native integration milestone

C++20 / JUCE 8.0.10 native 13-stem processing studio, updated 2026-10-01. Bounded VST3 hosting is now added and tested with an in-house Linux stereo effect. This source snapshot builds a standalone application. It is not a Windows binary, installer or completed arranger DAW.

## Implemented workflow

- Import 1–13 mono/stereo WAV files, Apply while stopped, then mix 13 synchronized file players with gain/mute/solo and eight serial insert slots each
- Stay in one main window: HOME, MIXER, embedded PLUGIN with adjustable split/expand, searchable/category PLUGIN VAULT, fixed ROUTING telemetry and SETTINGS
- Load one explicitly selected trusted VST3 stereo effect, edit its real controls, save/recall opaque state and include independent instances in offline export. The fixed slot limit is 6 ms; reload/rejection preserves alignment and old state
- Use the 12 actual registered native processors: 11 track effects plus listening-only monitor EQ. All 98 native parameter bindings are verified; artwork does not add DSP
- Read real calibrated final L/R peak/RMS meters, per-track meters, native processor measurements and the existing worker-backed waveform/FFT. Units, taps and limitations are documented
- Save/recall XML or JSON sessions; unload sources or start a confirmed new session without deleting source audio
- Export a private session snapshot to stereo float32 WAV at 48 kHz, including the track inserts and unchanged Task #2B master, excluding monitor correction/live mic. Tail defaults to 2 seconds and can be set to 0–30 seconds in the UI
- Retain the original live-vocal capture/monitoring path and background take writer, with no-overwrite WAV/metadata publication and recovery-file preservation
- See the approved Ghost in the Machine badge and illuminated title/labels at startup. Control lights still reflect actual states
- Normal quit protects unsaved session edits and active work. Failed/cancelled saves keep Studio open

Preferred device rate is 48 kHz; actual negotiated rate is prepared and displayed. Editing, session recall and offline export work without an audio device. Playback and physical recording require one.

## Build

Use the exact JUCE 8.0.10 source checkout and CMake 3.22 or later. The standard build does not depend on adjacent recovery folders:

```sh
cmake -S . -B build -DJUCE_PATH=/path/to/JUCE-8.0.10 -DCMAKE_BUILD_TYPE=Release
cmake --build build --target HEMStudioSuite
```

Windows with Visual Studio 2022 C++ tools:

```powershell
powershell -File tools/build-windows.ps1 -JucePath C:/SDK/JUCE-8.0.10
```

WASAPI is the Windows default. Optional ASIO requires an independently supplied Steinberg SDK and explicit HEM_ENABLE_ASIO/HEM_ASIO_SDK_PATH. This package does not install tools or accept licenses. JUCE licensing applies. The Windows build script and Windows hardware have not been executed here.

Native and external-state CTest targets are individually runnable. Actual VST3 load tests require an explicitly supplied in-house fixture bundle. For example:

```sh
cmake --build build --target hem_native_lifecycle_tests hem_plugin_meter_tests
ctest --test-dir build -R 'hem_native_lifecycle_tests|hem_plugin_meter_tests' --output-on-failure
```

For the entire suite, build all targets first, then run ctest --test-dir build --output-on-failure (add -C Release with Visual Studio). HEM_DSP_TESTS_ONLY=ON provides seven dependency-free numerical/geometry targets. The cloud-only object-reuse helpers document the efficient verification performed here; they are not the portable build route.

## Read first

- VST3-INTEGRATION-STATUS.md: exact external-host contract, tests and compatibility limits
- Documentation/RELEASE-LICENSE-CHECK.md: pinned SDK/JUCE licensing checkpoint before distribution
- INTEGRATION-STATUS.md: provenance, current capabilities and remaining release scope
- VERIFICATION-2026-10-01.md: exact passing checks, actual desktop results and limits
- WORKSPACE-VIEWS.md, PLUGIN-METER-VALIDATION.md, CLOSE-SAFETY.md: view/control/meter/lifecycle contracts
- OFFLINE-MIX-VALIDATION.md, TAKE-PUBLICATION-VALIDATION.md: audio-file safety and export/recording contracts
- SOURCE-CODE.md: current source and tests in copyable sections
- HISTORICAL-v0.10-README.md and older versioned documents: recovered background only

## Scope still open

File looping is per file; no arranger timeline, clip editing, MIDI, automation lanes or external AU hosting is implemented. VST3 acceptance currently covers only our in-house Linux stereo fixture, not arbitrary third-party plugins or Windows. The vault currently contains the 12 native entries, not 100 installed plugins. Session files do not persist the separate temporary vocal rack or learned runtime detector history. Broader external-plugin compatibility, AU and automation remain separate milestones. No Windows/hardware or full-load real-time acceptance is claimed. The unchanged master limiter retains its outstanding reconstructed-peak and listening-quality limitations.

## VST3 test fixture

The default build does not install or scan plugins. Build only the supplied test-owned fixture when needed:

```sh
cmake -S . -B build -DJUCE_PATH=/path/to/JUCE-8.0.10 -DHEM_BUILD_VST3_TEST_FIXTURE=ON
cmake --build build --target HEMVST3TestFixture_VST3 hem_external_host_tests hem_external_integration_tests
```

Run the two external test executables with the exact produced .vst3 bundle path as their sole argument, or configure HEM_TEST_VST3_BUNDLE to that explicit path for CTest registration. Never point them at a plugin directory or unapproved third-party binary. Tests/VST3Fixture/README.md documents the fixture contract. Its binary and JUCE/SDK sources are not included in this source ZIP.
