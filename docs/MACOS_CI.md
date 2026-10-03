# Native macOS build and test report

## Verified result
On 2026-10-03 at 00:13 UTC, native Apple Silicon build and all 29 CTests passed.
Run: https://github.com/The-Hybrid-Elite-Mafia/hem-studio-suite-native/actions/runs/37079630104
Tested source: a71dd3554afa8ee79f636409e6d42b433f1217b3
Windows starting point: 55a881700f4d90775f36b3e785d763a8652a19dc

- Build: PASS, full default target including HEMStudioSuite, HEMVST3TestFixture_VST3 and test executables
- Focused regressions: 3/3 PASS
- Full suite: 29/29 PASS; JUnit records failures=0, disabled=0, skipped=0
- Package/hash verification: PASS
- Native architecture: arm64 only, verified with file and lipo
- Intel/universal package: NOT BUILT OR TESTED
- Physical Mac audio, final listening and interactive user hardware GUI validation: NOT HARDWARE-VALIDATED
- Independent shipped-master -0.95 dBTP acceptance: NOT VERIFIED; original gate absent from recovered source

This is the same native Studio Suite feature set, not a browser conversion.
The protected CoreBaseline source hashes and DSP constants remain unchanged.
Windows branch, workflow and existing Windows packages were not modified.

## Platform and dependencies
Standard GitHub-hosted macos-15, actual macOS 15.7.9 build 24G830.
Xcode 16.4; Apple Clang 17.0.0 (clang-1700.0.13.5); CMake 4.4.3.
C++20 and Objective-C++, Release, macOS deployment target 12.0.
Only macOS 15.7.9 was exercised; deployment target alone does not prove every older OS.
Official JUCE 8.0.10 commit 3af3ce009f6a02f6fa651008fffb5b41743a9fab.
JUCE uses CoreAudio on Mac. Native VST3 hosting and the owned fixture are retained.
No AU host or new feature was silently added.

## Artifacts
Application artifact:
HEM-Studio-Suite-macOS-arm64-Release-a71dd3554afa8ee79f636409e6d42b433f1217b3
https://github.com/The-Hybrid-Elite-Mafia/hem-studio-suite-native/actions/runs/37079630104/artifacts/11259195808

Evidence artifact:
macOS-arm64-evidence-37079630104-1
https://github.com/The-Hybrid-Elite-Mafia/hem-studio-suite-native/actions/runs/37079630104/artifacts/11259205825

GitHub artifact retention expires 2026-10-06 around 00:13 UTC.
Application download was also preserved separately in the user's Library.
The application artifact contains the app ZIP, SHA256SUMS.txt, BUILD-REPORT.txt and CTest JUnit.
Outer artifact SHA-256:
28075c3d196c6ab5d9818c1db8944bd63182de2b6b6d88c531292849a494c190
Inner app ZIP SHA-256:
2d4ec21637b375fc7e670e1923689cfc9888b3944a9083b97a4d14c81dd4703e
Evidence SHA-256:
21bd7f714d9cb5e1bf62b21e894798309b74b881c4fdd088f7e08d9d47939ff3

The app is not Developer-ID signed or notarized. Linker ad-hoc signing is not notarization.
This is a private engineering package; public distribution licensing is not cleared.
otool dependency output is retained; application dependencies were macOS system frameworks/libraries.

## Mac corrections and failures retained
The first complete native Mac build passed compilation but only 26/29 tests.
Failures were exact session parameter recall, signed zero-gain meter raster, and an external-host integration timeout.

1. Exact no-op recall: SessionState now avoids normalizing/snapping an already identical raw value.
   JUCE APVTS initializes raw defaults without snapping; another conversion can change their float representation.
   All validation still happens before mutations. No tolerance, rounding or assertion was relaxed.
2. Floating-point contraction: Mac builds use -ffp-contract=off.
   Actual diagnostics showed neutral transient gain becoming +1.5636e-07 on ARM and falsely requiring a nonzero bar.
   Separate float operations restore exact neutral behavior without suppressing real measured meter values.
3. AppKit bootstrap: the console integration test initializes NSApplication before entering the main loop.
   This fixes the initial nil-NSApp dispatch/join deadlock; the actual app already has JUCE's normal application entry point.
4. Objective-C++ visibility: hidden .mm implementation symbols prevent host/plugin JUCE state collisions.
   LLDB pinpointed invalid free in the fixture's var/Value/Label construction while opening its original generic editor.
   JUCE 8.0.10's CMake sets C/C++ hidden visibility but omits OBJCXX; enabling that language needs its own setting.
5. Raster helper ends the Graphics context before returning the image.
   It retains all exact-color, calibration, geometry and audio measurement assertions.

One short retry failed due to malformed shell quoting, and one configure retry failed because Objective-C++
was enabled after JUCE subdirectories. Both were corrected; neither yielded a test pass.
Focused failures were diagnosed with actual native LLDB and raster evidence before the final full pass.
No original tests were removed, loosened, skipped or replaced by mocks.

## Reproduce
Workflow: .github/workflows/macos-native.yml
Authorized automatic trigger is limited to branch ci/macos-native and relevant native/ci/macos/workflow paths.
No main-branch registration was made.
Separate directories: ci/macos, build-macos, macos-logs, dist-macos.

On an arm64 Mac with Xcode command-line tools, CMake and the pinned JUCE checkout:
Set HEM_MAC_ARCH=arm64, then run bash ci/macos/build-test.sh with each phase:
validate, configure, build, test, package.
The configure phase expects JUCE at dependencies/JUCE.
Packaging uses GITHUB_SHA/GITHUB_SERVER_URL/GITHUB_REPOSITORY/GITHUB_RUN_ID for exact provenance;
the GitHub workflow supplies them.
The script checks all 29 expected test names, retains pipefail, and refuses skipped coverage.
Failures retain compiler/test logs and native diagnostics; success alone enables application packaging.

## Remaining boundaries
Existing separate limiter tests and CoreBaseline parity passed, but do not certify the missing independent
reconstruction-aware active-master -0.95 dBTP gate. No claim of full real-time allocation safety,
hardware latency, ASIO/LDAC behavior, or listening quality follows from this Mac CI result.
Intel architecture requires its own native run and package.
The final successful job used approximately 21 rounded native Mac runner minutes.
No paid allowance, account budget or signing purchase was enabled.

Primary references:
https://docs.github.com/en/actions/reference/runners/github-hosted-runners
https://clang.llvm.org/docs/ClangCommandLineReference.html
https://lldb.llvm.org/man/lldb.html
https://forum.juce.com/t/fixed-linux-crash-when-loading-both-vst2-and-vst3/53967
