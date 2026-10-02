# Native Windows build status

## Verified source import

The actual Google Drive ZIP was downloaded and inspected. `HEM-Studio-Suite-Native-Integration.zip` is 3,086,972 bytes, contains 191 files, and passed integrity checks. SHA-256: `aa8dc468f3e01233865daa6e52d62e80b1e6e356e149e5b553b621f31d0ad119`.

The exact original source, assets, tests, license checkpoint and historical documents are preserved unchanged under `native/` on branch [`ci/windows-native-2025`](https://github.com/The-Hybrid-Elite-Mafia/hem-studio-suite-native/tree/ci/windows-native-2025). Import commit: `1576e082e30872ce7b40ccb56c6dac3c27117bea`. All 191 remote file blob hashes were checked against the original bytes. Existing source and DSP constants were not replaced to make CI pass.

## Windows workflow

The manual-only workflow is [Native Windows build and tests](https://github.com/The-Hybrid-Elite-Mafia/hem-studio-suite-native/actions/workflows/windows-native.yml). Select `ci/windows-native-2025`, whose initial reviewed workflow/source commit is `c27bd99c2bcade21454127ca5031c0ca0aa036b6`. The workflow is registered on main so the dispatch UI is available, but main does not contain imported source. Running main deliberately fails source validation.

- Genuine GitHub-hosted `windows-2025`, Visual Studio 2022 MSVC x64 Release
- Official JUCE 8.0.10 pinned at `3af3ce009f6a02f6fa651008fffb5b41743a9fab`; reviewed official actions pinned to commits
- Full `ALL_BUILD`, exact `HEMStudioSuite` app, owned `HEMVST3TestFixture_VST3`, and all 29 original CTest targets
- WASAPI, ASIO OFF, no DSP-only mode; explicit fixture path with no third-party plugin scan
- Any build/test failure or reported SKIP fails CI; original test assertions are unchanged
- Full compiler, MSBuild, CTest, JUnit and environment evidence uploads even on failure
- Exact-EXE private package with verified system dependencies, embedded artwork, notices, SHA-256 manifest and responding-window smoke check
- Manual runs only, bounded job/build time, three-day artifact retention, no billing/access changes

The included allowance and existing zero-spend stop were verified before dispatch. Paid usage is not authorized.

**At this documentation update, the Windows workflow is ready for dispatch; no completed Windows build, passing test result, or downloadable executable is claimed.** See the actual Actions run for subsequent status. Implementation details and limits are in [the branch CI guide](https://github.com/The-Hybrid-Elite-Mafia/hem-studio-suite-native/blob/ci/windows-native-2025/docs/WINDOWS_CI.md).

## Explicit acceptance limits

The original native independent -0.95 dBTP gate and difficult vectors are absent from this source package, so native true-peak acceptance remains NOT VERIFIED. `hem_master_limiter_tests` checks a separate historical DSP limiter; it does not certify the shipped `CoreMasterStage` → `CoreBaseline::MasterTruePeakLimiter`. Native adapter tests check exact baseline parity, latency, selected allocation behavior and edge cases, not independent reconstructed true peaks.

Physical Windows audio, ASIO, broad third-party plugins, full realtime allocation/determinism, and 808/listening acceptance remain NOT VALIDATED. Historical Linux logs are not Windows evidence. Private artifacts are engineering deliverables only; distribution/licensing is not cleared.
