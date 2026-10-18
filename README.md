# HEM Studio Suite (Hybrid Elite Mafia)

Windows 13-stem processing studio (C++20, JUCE 8.0.10): import 1–13 WAV stems, eight insert slots per track,
the native HEM processors with their photoreal faceplates, plugin vault, routing, sessions (XML/JSON) and stereo export.
Originally built by ChatGPT (handoff "0.11.4 checkpoint 6"); made public, given the HEM icon and a standard installer by Claude.

## Layout
| Folder | What |
|---|---|
| `native/` | App source (`Source/`), tests (`Tests/`), artwork (`Assets/`), docs, `CMakeLists.txt` |
| `pitch-time-work/` | Signalsmith Stretch / Linear (MIT), used by the pitch/time modules |
| `Dependencies/JUCE-8.0.10/` | JUCE 8.0.10 source as handed over |
| `Dependencies/vst3sdk/` | Pinned MIT-licensed VST3 SDK snapshot |
| `Installer/` | Inno Setup script, icon, notices |
| `Manual/` | Illustrated manual (0.11.2) and checkpoint notes |
| `.github/workflows/windows.yml` | Cloud build (**manual only**): build, focused tests, installer, Release |

## Build (Windows, Visual Studio 2022 x64, CMake 3.22+, Inno Setup 6)
```
cmake -S native -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DHEM_ENABLE_ASIO=OFF -DHEM_BUILD_VST3_TEST_FIXTURE=OFF -DBUILD_TESTING=ON
cmake --build build --config Release --parallel --target HEMStudioSuite
ctest --test-dir build -C Release -R "^hem_(utility_panel_sizing|instrumental_core|spectral_core|import)_tests$"
"%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" "/DBuildDir=%CD%\build\HEMStudioSuite_artefacts\Release" /DAppVersion=0.11.5 Installer\HEMStudioSuite.iss
```
JUCE and the VST3 SDK are picked up from `Dependencies/` automatically. Linux (g++, Ninja) also builds the app and tests.

## Keys and licences
No signing keys, passwords or API keys are used or included; the app and installer are unsigned.
Third-party licences: see `Installer/THIRD-PARTY-NOTICES.txt` and the other notice files (includes an AGPL-3.0 spectral component).
JUCE 8 licensing applies (see `Dependencies/JUCE-8.0.10/LICENSE.md`).

## Status
See `native/README.md` and `native/INTEGRATION-STATUS.md` for what is implemented. Not yet verified by a person on Windows:
mouse/keyboard workflows, full scanner/instrumental renders, listening.
