# Native Windows build status

## Current verified result: build and 29/29 tests passed

**The genuine `windows-2025` pipeline completed successfully on 2026-10-02 at 20:27 UTC.** [Successful run 37053162846](https://github.com/The-Hybrid-Elite-Mafia/hem-studio-suite-native/actions/runs/37053162846) built the application, every test and the owned VST3 fixture, executed all 29 registered CTest targets with zero failures/disabled/skipped tests, packaged the exact app, and opened a responding native window from the packaged EXE. The two focused raster regressions also passed before ALL_BUILD; the full suite reran them. Full CTest time: 10.67 seconds.

Tested source commit: [`55a881700f4d90775f36b3e785d763a8652a19dc`](https://github.com/The-Hybrid-Elite-Mafia/hem-studio-suite-native/commit/55a881700f4d90775f36b3e785d763a8652a19dc), on `ci/windows-native-2025`. No merge into main was performed. Main registers the manual workflow; select the tested source branch for future manual builds.

Runner: Windows Server 2025 Datacenter 10.0.26100 x64, image `windows-2025-vs2026` `20260925.250.1`, agent 2.337.0. CMake 4.4.3; VS18/2026 generator; MSVC compiler 19.44.35229.0 using installed v143/14.44.35207 x64 tools; Release/C++20/static runtime. Official JUCE8.0.10 remains pinned at `3af3ce009f6a02f6fa651008fffb5b41743a9fab`. WASAPI, ASIO OFF. The requested runner label remained `windows-2025` throughout.

### Verified private artifacts

- [App ZIP](https://github.com/The-Hybrid-Elite-Mafia/hem-studio-suite-native/actions/runs/37053162846/artifacts/11250281475): `HEM-Studio-Suite-Windows-x64-Release-55a881700f4d90775f36b3e785d763a8652a19dc`, 6,620,101 bytes, SHA-256 `0164147440d89b36fc50894e7a74954a796329f06a96f2a8b126b0f593e8ce5e`; expires 2026-10-05 20:27:35 UTC
- [Complete evidence ZIP](https://github.com/The-Hybrid-Elite-Mafia/hem-studio-suite-native/actions/runs/37053162846/artifacts/11250810279): `Windows-evidence-37053162846-1`, 16,354,465 bytes, SHA-256 `396a1d9d6508ef9397792bdd7016d9185225d42a0f69077c10add6de74be1a28`; expires 2026-10-05 20:27:37 UTC
- Exact EXE: `Hybrid Elite Mafia - Studio Suite.exe`, 12,116,480 bytes; SHA-256 `73d5e2bb555a9fffa7bc3072b9726efe84cc6bfe8e6c6b56ce74b4adeec7208b`

Both actual downloadable ZIPs were downloaded and integrity/hash checked. Every package manifest entry matched; all eight package files were accounted for. The EXE was independently inspected as an x64 PE GUI image. Windows PE dependency inspection found system DLL imports only; both artwork images are embedded. The package includes build info, checksums, launch/dependency evidence and license checkpoint/notices.

### What this does not establish

This is a verified private engineering build and test suite, **not a completed DAW or production certification**. The original independent native **-0.95 dBTP gate remains NOT VERIFIED** because its gate/vector source is absent. The separate DSP MasterLimiter test does not certify the shipped CoreBaseline master. Physical audio devices, ASIO, full real-time allocation/determinism, 808/listening quality and broad third-party compatibility remain NOT VALIDATED. Distribution/licensing remains uncleared. All numerical assertions, thresholds, artwork, rendering backend and protected CoreBaseline were preserved; the documented test fixes correct stale references or invalid image-readback lifetime.

## Historical setup, failures and corrections (preserved below)

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

## Complete build; 27/29 Windows tests passed

[Run 37042574825](https://github.com/The-Hybrid-Elite-Mafia/hem-studio-suite-native/actions/runs/37042574825), commit `d0261f3d8f75424759fedb2762dfda755d23c4cd`, completed ALL_BUILD successfully in 1:18:26 with zero errors, then executed all 29 tests: 27 passed, 2 failed, zero disabled/skipped, 12.07 seconds total. Native audio/DSP, baseline adapter parity, lifecycle, sessions, VST3 hosting and real external integration passed. The failures were offline export-panel pixel opacity and startup branding pixels. No successful release package or hardware acceptance is claimed.

Both failed tests read native-image pixels while a Graphics context remained active, which the exact JUCE8 Direct2D implementation explicitly prohibits. Corrected commit `55a881700f4d90775f36b3e785d763a8652a19dc` closes the existing drawing context before the same pixel reads. Independent review confirmed all 8 branding and 19 offline-worker require calls, renderer selection, artwork, dimensions, coordinates and thresholds remain unchanged. No production renderer or DSP implementation was modified.

The next run builds/runs these two regressions early, reuses the same build tree, then still builds the entire project and reruns all29 CTest targets. The correction requires actual Windows validation before being called successful. The verified failure artifact is `Windows-evidence-37042574825-1`, SHA-256 `7c4cafd16f6aa43eafb9efb2f05ed622fce031859b900a5dbb952b12ab55cc8c`. Its retained EXE is diagnostic only, not a completed release package.
