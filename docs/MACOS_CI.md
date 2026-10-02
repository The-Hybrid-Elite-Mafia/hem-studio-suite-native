# Native macOS build and test

Status: workflow prepared; no macOS build or tests yet verified.

The Mac branch starts at Windows-tested commit 55a881700f4d90775f36b3e785d763a8652a19dc.
All native source, DSP constants, UI functions and 29 CTest assertions remain identical.
Separate paths: ci/macos, build-macos, macos-logs, dist-macos.
Windows branch, workflow and release artifacts remain unchanged.

Manual workflow: .github/workflows/macos-native.yml. Select ci/macos-native explicitly.
First architecture: arm64 on standard macos-15. Optional second: x86_64 on macos-15-intel.
These are independent native packages, not a universal binary. Do not call Intel verified from ARM results.
Official runner reference: https://docs.github.com/en/actions/reference/runners/github-hosted-runners

Dependencies: CMake >= 3.22, Apple Xcode/Clang recorded by the run, official JUCE 8.0.10
commit 3af3ce009f6a02f6fa651008fffb5b41743a9fab. C++20, Release, deployment target macOS 12.0.
CoreAudio replaces platform-specific WASAPI; this is the same native app and VST3 host.
No new AU hosting, feature replacements, reduced test suite or second AI are introduced.

Build: bash ci/macos/build-test.sh validate; configure; build; test; package (one phase per invocation).
HEM_MAC_ARCH must equal the physical runner architecture.
Local packaging requires GITHUB_SHA, GITHUB_SERVER_URL, GITHUB_REPOSITORY and GITHUB_RUN_ID
to label the exact evidence; GitHub Actions supplies these automatically.
Build the full default CMake target, including HEMStudioSuite and HEMVST3TestFixture_VST3.
Every expected CTest must be registered; failure, missing tests and skipped coverage fail the workflow.
The Bash pipeline preserves compiler exit status through tee via pipefail.

Artifacts: HEM-Studio-Suite-macOS-ARCH-Release-COMMIT (only after full success);
macOS-ARCH-evidence-RUN-ATTEMPT (always). Retention 3 days.
Successful package includes .app ZIP, SHA256SUMS.txt, JUnit and BUILD-REPORT.txt.
Developer ID signing and notarization are not performed. Linker ad-hoc signature is not notarization.
Real Mac GUI, microphone permission interaction, audio hardware, latency and listening require hardware validation.
Independent active-master -0.95 dBTP acceptance remains unverified because that gate is absent
from the recovered package. Passing the separate limiter tests does not establish it.
Private engineering package; public distribution licensing is not cleared.

Budget: fresh account observation 2026-10-02 22:17 UTC: 410/2000 included minutes used,
$0 billable and $0 Stop-usage budget retained. First job has a 120-minute cap.
Recheck allowance before a second architecture or costly retry. Never enable paid use.
