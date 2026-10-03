# Universal macOS validation

Verified October 3, 2026 at 02:18 UTC: both native architectures passed all 29 tests and actual app startup/window checks. This branch is derived from the verified Apple Silicon source at a71dd3554afa8ee79f636409e6d42b433f1217b3 and its report-only successor 30e49dc931d37106de45febc00167d5cbeec2b5b. The existing ARM64 release is preserved.

The workflow builds the real application, the in-house VST3 fixture and all 29 test executables with `CMAKE_OSX_ARCHITECTURES=arm64;x86_64`. It transfers the exact same checksummed runtime to native Apple Silicon and Intel runners. Each runs the unchanged 29 registered CTest commands (only build-root paths relocated), rejects skipped coverage, verifies every runtime Mach-O contains both slices, checks each slice's minimum OS and dynamic dependencies, and launches the actual packaged app to observe a live application window. No mock targets or second AI are introduced.

Configured deployment floor: macOS 12.0 Monterey, matching the established Mac target. This is a conservative project deployment setting, not a claim that JUCE itself requires 12 or that every older OS was tested. Apple Silicon begins at macOS 11; the selected project target and APIs/toolchain determine the higher configured floor. C++20/JUCE compile availability checks and each actual Mach-O load command plus Info.plist must agree. Actual native execution is on macOS 15 runners. Runtime compatibility with macOS 12, 13, 14, future versions and arbitrary external plugins remains unverified until exercised. This is not 'all versions' certification.

Workflow: `.github/workflows/macos-universal.yml`, branch `ci/macos-universal`. Existing Windows and ARM64 workflows are untouched. Build directories, artifacts and concurrency are separate. Official JUCE 8.0.10 is pinned at 3af3ce009f6a02f6fa651008fffb5b41743a9fab. All DSP constants, baseline hashes, renderers and test assertions remain unchanged.

Budget caps: 55-minute build, two 10-minute native test jobs, 3-minute packaging job. At conservative 10x weighting this reserves 780 included minutes, versus 939 displayed remaining at 00:46 UTC October 3. No paid budget or billing setting changed; recheck before any retry. A timeout fails honestly.

Output is a single universal `.app` ZIP only after both native test jobs and app startup smoke pass. It has ad-hoc code signatures only, no Developer ID signing or notarization. Gatekeeper may block installation; no security bypass is prescribed. Physical audio hardware, listening, user-installed plugins, final interactive GUI and the missing independent shipped-master -0.95 dBTP gate are not established by these tests. Public distribution licensing remains uncleared.

References:
- https://docs.github.com/en/actions/reference/runners/github-hosted-runners
- https://github.com/actions/runner-images
- https://github.com/juce-framework/JUCE/tree/3af3ce009f6a02f6fa651008fffb5b41743a9fab

## First universal attempt
Run 37083941846 at 14ade4a6072777633bc6c39cae3671e92a021331 completed the real universal build, then failed transfer packaging because CTest inventory captured before compilation omitted command paths for not-yet-built executables. No native test job ran. The correction regenerates inventory after compilation; assertions and DSP are unchanged. Runtime staging is now archived before audit and retained if an audit fails so a packaging-only correction need not throw away the compilation. Retry caps are 43 + 4 + 4 + 2 native runner minutes, conditional on fresh budget verification.

## Packaging-only recovery
Run 37086588351 compiled every universal target successfully. All 31 Mach-O files passed dual-slice/minimum-OS/system-dependency checks, then bundle metadata audit failed because JUCE's generated app Info.plist omitted LSMinimumSystemVersion. Runtime artifact 11261920349 preserves the full compiled payload. No native test ran in that failed job.
The recovery workflow downloads that exact artifact, verifies all original file hashes, sets macOS 12.0 in the app/fixture Info.plists, ad-hoc reseals the bundles and seals new checksums. Both architecture jobs then execute these identical final bytes. Binary source is b41c54d41aa767dee900dd78ef623277baf12719; workflow/metadata revision is separately recorded. No app/DSP/test source recompilation or test relaxation occurs. Recovery caps total 13 native runner minutes, conditional on fresh quota verification.

## Final verified result
- Universal compilation: PASS at b41c54d41aa767dee900dd78ef623277baf12719, run 37086588351.
- Metadata finalization and native validation: PASS at af44f66add6838198d58b9b11a8aca98765e06f4, run https://github.com/The-Hybrid-Elite-Mafia/hem-studio-suite-native/actions/runs/37089217893 .
- Native ARM64 macos-15: 29/29 PASS, zero failures/skips, actual packaged app window observed.
- Native Intel macos-15-intel: 29/29 PASS, zero failures/skips, identical packaged app window observed.
- Both test hosts actually ran macOS 15.7.9 (24G830). No Rosetta test substituted for native Intel execution.
- 31 Mach-O files audited, each with arm64 and x86_64, minimum macOS 12.0 and system-only dylib dependencies.
- Final app and fixture Info.plist minima set to 12.0 and bundles ad-hoc signed/strictly verified. Not Developer ID/notarized.
- Identical full runtime archive SHA256 on both native jobs: 6dcaf72f96e97e6f78f80d080ec4d720865d34d4631d2ad94ade0c40e0a72985 .
- Successful release artifact: HEM-Studio-Suite-macOS-Universal-Release-af44f66add6838198d58b9b11a8aca98765e06f4, artifact 11261726987, 13,558,033 bytes.
- GitHub outer artifact SHA256: 344cfe696149f9018e142f1d9e833f5006c397f291ba54b3bdfda7fe0ba90c10 .
- Inner app ZIP SHA256: 1cc565e8cb8baca646d06a3230d336812aeb9302564691f60883a52eecaaa903 .
- User delivery HEM-Studio-Suite-macOS-Universal.zip contains the same app plus instructions/evidence without a nested app ZIP. Size 13,580,764 bytes; SHA256 0657491e9ca2cee399442f47f61636bd9ac7e87e39da39000b9933dc53a70a4f .
- Both outer artifact and inner/delivery ZIP CRC and SHA256 independently verified after download.
- Apple Silicon first release and Windows builds preserved; no Windows-source/workflow changes.

Actual native startup proves launch and a visible window, not full interactive usability or audio-hardware acceptance. macOS 12-14 runtime remains unverified despite the configured floor. Original missing independent master -0.95 dBTP gate, physical audio, listening, signing/notarization and public distribution clearance remain outstanding.
