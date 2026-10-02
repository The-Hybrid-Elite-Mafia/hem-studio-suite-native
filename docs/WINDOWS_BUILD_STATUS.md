# Native Windows build status

## Verified source import

The actual Google Drive ZIP was downloaded and inspected. `HEM-Studio-Suite-Native-Integration.zip` is 3,086,972 bytes, contains 191 files, and passed integrity checks. SHA-256: `aa8dc468f3e01233865daa6e52d62e80b1e6e356e149e5b553b621f31d0ad119`.

The exact original source, assets, tests, license checkpoint and historical documents are preserved unchanged under `native/` on branch [`ci/windows-native-2025`](https://github.com/The-Hybrid-Elite-Mafia/hem-studio-suite-native/tree/ci/windows-native-2025). Import commit: `1576e082e30872ce7b40ccb56c6dac3c27117bea`. All 191 remote file blob hashes were checked against the original bytes. Existing source and DSP constants were not replaced to make CI pass.

## Windows workflow

The manual-only workflow is [Native Windows build and tests](https://github.com/The-Hybrid-Elite-Mafia/hem-studio-suite-native/actions/workflows/windows-native.yml). Select `ci/windows-native-2025`, whose initial reviewed workflow/source commit is `c27bd99c2bcade21454127ca5031c0ca0aa036b6`. The workflow is registered on main so the dispatch UI is available, but main does not contain imported source. Running main deliberately fails source validation.

- Genuine GitHub-hosted `windows-2025`, installed Visual Studio 2026 generator with explicit MSVC v143/14.44 x64 Release
- Official JUCE 8.0.10 pinned at `3af3ce009f6a02f6fa651008fffb5b41743a9fab`; reviewed official actions pinned to commits
- Full `ALL_BUILD`, exact `HEMStudioSuite` app, owned `HEMVST3TestFixture_VST3`, and all 29 original CTest targets
- WASAPI, ASIO OFF, no DSP-only mode; explicit fixture path with no third-party plugin scan
- Any build/test failure or reported SKIP fails CI; original test assertions are unchanged
- Full compiler, MSBuild, CTest, JUnit and environment evidence uploads even on failure
- Exact-EXE private package with verified system dependencies, embedded artwork, notices, SHA-256 manifest and responding-window smoke check
- Manual runs only, bounded job/build time, three-day artifact retention, no billing/access changes

The included allowance and existing zero-spend stop were verified before dispatch. Paid usage is not authorized.

**First real run [37031918670](https://github.com/The-Hybrid-Elite-Mafia/hem-studio-suite-native/actions/runs/37031918670) failed during setup before compilation/tests.** It verified Windows Server 2025 10.0.26100, image windows-2025-vs2026/20260925.250.1, and CMake 4.4.3. The protected byte check blocked checkout newline conversion; `.gitattributes` now preserves original bytes. The VS2022 query returned no instances, while the exact official image contains VS2026 and MSVC14.44. Corrected commit `5634ce2f32741cade07eb30c2c0c45c4561400a2` keeps windows-2025 and adapts only the CI generator/toolset. No completed Windows build, passing test result, or executable is claimed yet. See the actual Actions run for subsequent status. Implementation details and limits are in [the branch CI guide](https://github.com/The-Hybrid-Elite-Mafia/hem-studio-suite-native/blob/ci/windows-native-2025/docs/WINDOWS_CI.md).

## Explicit acceptance limits

The original native independent -0.95 dBTP gate and difficult vectors are absent from this source package, so native true-peak acceptance remains NOT VERIFIED. `hem_master_limiter_tests` checks a separate historical DSP limiter; it does not certify the shipped `CoreMasterStage` → `CoreBaseline::MasterTruePeakLimiter`. Native adapter tests check exact baseline parity, latency, selected allocation behavior and edge cases, not independent reconstructed true peaks.

Physical Windows audio, ASIO, broad third-party plugins, full realtime allocation/determinism, and 808/listening acceptance remain NOT VALIDATED. Historical Linux logs are not Windows evidence. Private artifacts are engineering deliverables only; distribution/licensing is not cleared.

## Actual full-compile attempt and consolidated retry

[Run 37032453031](https://github.com/The-Hybrid-Elite-Mafia/hem-studio-suite-native/actions/runs/37032453031) completed with failure on 2026-10-02 at 17:37 UTC. Windows setup, protected baseline SHA checks, pinned JUCE and all 29 CTest registrations passed. MSVC compiler 19.44.35229.0 (v143 toolset directory 14.44.35207) compiled and linked the exact Studio Suite EXE and the owned VST3 bundle. However, ALL_BUILD exposed two defects and reached its 80-minute limit before all targets finished. No CTest runtime tests ran, and no successful app package was uploaded.

The two narrowly scoped corrections preserve every assertion/threshold: DspTests.cpp now refers to the existing HostDelay type instead of the stale Delay name; CMake now includes the existing ExternalPluginHost.cpp in the external-integration test target. The protected CoreBaseline implementation is unchanged. Corrected commit `d0261f3d8f75424759fedb2762dfda755d23c4cd` also caps nested compiler concurrency, gives compilation 140 minutes within a 180-minute job, and retains a compiled EXE as clearly unverified diagnostic evidence if a later stage fails. Release-artifact upload remains gated on successful complete build, all tests, packaging, dependency and launch checks.

The full downloaded failure artifact, `Windows-evidence-37032453031-1`, passed ZIP and SHA-256 verification: `a58d5fb873eabcdc895bbf7024377ec02f7c0586788a0dc7587318c3f4a74929`. Compiler text and diagnostic/binary MSBuild logs were retained. The next actual run must validate the corrected commit; build-and-test success is still not claimed.
