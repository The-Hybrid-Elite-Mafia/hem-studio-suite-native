# Genuine Windows CI and source provenance

## Source import

The original Google Drive archive `HEM-Studio-Suite-Native-Integration.zip` was downloaded as actual bytes (3,086,972 bytes), passed ZIP integrity checks, and was inspected before import. Archive SHA-256: `aa8dc468f3e01233865daa6e52d62e80b1e6e356e149e5b553b621f31d0ad119`.

All 191 files were imported unchanged under `native/`, including the original manifest, documentation, artwork, and historical verification records. All 190 manifest-listed file hashes matched. Exact import commit: `1576e082e30872ce7b40ccb56c6dac3c27117bea`. The original repository README and `docs/WINDOWS_BUILD_STATUS.md` are preserved. Their old source-unavailable status is historical and superseded by this document. Source and CI development are on `ci/windows-native-2025`.

## Manual build

Choose **Actions → Native Windows build and tests → Run workflow**, select the reviewed `ci/windows-native-2025` branch. The workflow is registered on `main` only so GitHub exposes manual dispatch; `main` does not yet contain imported source and deliberately fails source validation if selected. There are no push, pull-request, or schedule triggers.

The requested runner is explicitly `windows-2025`, with the installed VS 2026 generator and explicitly selected MSVC v143/14.44 x64 Release. JUCE 8.0.10 is checked out from its official repository at `3af3ce009f6a02f6fa651008fffb5b41743a9fab`. Official checkout and artifact actions are commit-pinned. There are no source submodules in the supplied archive; checkout supports recursive submodules if the selected revision contains any. No compiler/dependency installer or paid service is introduced.

Configuration enables all tests, disables DSP-only mode, uses WASAPI/ASIO OFF, enables only the owned VST3 fixture, and supplies its exact generated path. MSVC's runtime is selected statically for the private portable app bundle. All test assertions, DSP constants, and protected CoreBaseline are unchanged. The CMake source-list repair described below links an existing implementation omitted from one test target. Any narrowly scoped build fixes after the exact import are documented below. `ci/windows/expected-tests.txt` asserts the full original 29-test inventory. Build failure, any failing test, or explicitly reported SKIP fails the job; nothing uses continue-on-error or excludes a failing test.

The build uses ALL_BUILD, including `HEMStudioSuite`, all test executables and `HEMVST3TestFixture_VST3`. It is limited to two MSBuild workers. One manual job has a 120-minute job bound, with an 80-minute build-step timeout leaving time for evidence uploads; artifacts expire after three days. The account's included allowance and existing zero-spend stop were checked before the first dispatch; no billing/access settings are changed. Full compiler text, MSBuild diagnostic/binary logs, CTest verbose output, JUnit, and CTest's original logs upload even after failure.

## Packaging and proof boundaries

The exact app is `build/HEMStudioSuite_artefacts/Release/Hybrid Elite Mafia - Studio Suite.exe`. Both images are embedded through `StudioArtwork`. Packaging checks PE dependencies against Windows system DLLs and fails if an unhandled runtime is found; it never collects arbitrary EXEs. The private bundle contains this executable, notices, runtime/build evidence and SHA-256 checksums. The packaged executable must open a responding native window on the runner before upload. This smoke test is not physical audio-device acceptance.

A successful workflow proves the selected revision built and its existing 29 registered tests passed on that actual Windows runner. It is not an independent native -0.95 dBTP certification: the original gate and difficult vectors are missing from this package. `hem_master_limiter_tests` covers the separate `Source/DSP/MasterLimiter.h`, while the shipped application routes through `CoreMasterStage` to `CoreBaseline::MasterTruePeakLimiter`. `hem_core_master_stage_tests` verifies exact baseline parity, latency, irregular blocks, sample-meter finiteness, ordinary new/new[] instrumentation and oversize-block handling; it explicitly disclaims reconstructed true peaks.

The original fixture CTest does not invoke its separate optional `--editor` mode; its extra 32 editor cycles are not claimed.

The original recorder/export tests conditionally depend on Windows symlink privilege. Recorder SKIP output is treated as a CI failure, not a quiet pass; no permission settings are changed by this workflow.

Native independent true-peak acceptance, full realtime allocation/determinism coverage, 808/listening acceptance, ASIO, and physical audio devices remain NOT VALIDATED. Historical `native/Verification` files are Linux-era records, not current Windows evidence. Do not weaken tests or alter known-good baseline/DSP thresholds to obtain a green run. Public release and dependency licensing remain blocked pending the owner's license review in `native/Documentation/RELEASE-LICENSE-CHECK.md`.

## First real Windows run and toolchain adaptation

[Run 37031918670](https://github.com/The-Hybrid-Elite-Mafia/hem-studio-suite-native/actions/runs/37031918670) used the requested `windows-2025` label on Microsoft Windows Server 2025 10.0.26100, runner image `windows-2025-vs2026` version `20260925.250.1`, runner agent 2.337.0, and CMake 4.4.3. It failed at the protected baseline byte-hash check before compilation or tests. The Git import blobs remain byte-identical; `.gitattributes` now disables checkout newline rewriting rather than changing the audited baseline hash or source.

The same run's `vswhere -version [17.0,18.0)` query returned `[]`: this current image contains no VS 2022 instance. [The exact official image inventory](https://github.com/actions/runner-images/blob/win25-vs2026/20260925.250/images/windows/Windows2025-VS2026-Readme.md) lists VS Enterprise 2026 18.10.12217.157 and `Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64`. Keeping `windows-2025`, CI therefore uses `Visual Studio 18 2026` with `-T v143,version=14.44,host=x64`, which retains the installed VS2022-era compiler toolset. [CMake documents this generator and explicit toolset selection](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2018%202026.html). No runner change, project source replacement or test weakening was made. The subsequent real run must verify this configuration.

First-run failure artifact: `Windows-evidence-37031918670-1`, SHA-256 `77b911ef24a95fe73517341699e1fb7a4ed15659d50db361cf3bcf3cccf3b4a3`. No executable or test pass resulted from this first run.

## Genuine compiler failure: stale DSP test symbol

The real Windows compiler in [run 37032453031](https://github.com/The-Hybrid-Elite-Mafia/hem-studio-suite-native/actions/runs/37032453031) reported MSVC C2039/C2065 at `native/Tests/DspTests.cpp:42`: `hem::Delay` is not declared by the included dependency-free `DSP/DspCore.h`. That kernel was already named `hem::HostDelay` in the imported source and is used by the actual insert/vocal host. The unrelated CoreBaseline `hem::Delay` uses a different API and is not linked by this test target.

The correction changes only `hem::Delay` to `hem::HostDelay` on that line. The `prepare(13)` call, emitted impulse, exact `l[13] == 1 && l[0] == 0` requirement, every other assertion/vector/threshold, and all DSP implementations remain unchanged. This is a stale symbol fix, not a weakened test. A source-only Linux syntax check independently reproduced the original error and accepted the single-symbol fix; it is development evidence only, not Windows validation. The complete real Windows build/test workflow must still be rerun against the corrected commit. The exact original remains preserved in import commit `1576e082e30872ce7b40ccb56c6dac3c27117bea`.

## Genuine linker failure: missing external-host implementation

Later in the same [real Windows run 37032453031](https://github.com/The-Hybrid-Elite-Mafia/hem-studio-suite-native/actions/runs/37032453031), `hem_external_integration_tests` failed with six unresolved `ExternalPluginHost` methods (constructor, destructor, discoverSelected, stage, capture, collectGarbage), ending with LNK1120. `HEM_NATIVE_TEST_SOURCES` is captured before the later loop attaches `Source/Plugins/ExternalPluginHost.cpp` to the application and other existing host targets; the external-integration target is declared after that loop, so neither route supplied this file.

The correction adds the existing `Source/Plugins/ExternalPluginHost.cpp` directly to this target's source list, as already done for the other external-host test targets. No implementation, test body, assertion, fixture, DSP constant, target registration, or test exclusion changes. The full real Windows run must still validate the repaired target.
