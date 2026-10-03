# Universal macOS validation

Prepared, not yet run. This branch is derived from the verified Apple Silicon source at a71dd3554afa8ee79f636409e6d42b433f1217b3 and its report-only successor 30e49dc931d37106de45febc00167d5cbeec2b5b. The existing ARM64 release is preserved.

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
