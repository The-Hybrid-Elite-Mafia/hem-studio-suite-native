> Historical recovered design/status document. Current implementation and verification take precedence in README.md, INTEGRATION-STATUS.md and VERIFICATION-2026-10-01.md.

# Version 0.7 verification record — September 29, 2026

Executed:
- `g++ -O2 -std=c++17 Tests/MasterLimiterTests.cpp Source/DSP/DspCore.cpp` and resulting executable.
- 44,100 / 48,000 / 96,000 Hz: thirteen-stem signal with +12 dB extra gain; measured peak -3.45979 dBTP in the specified 8x/32-tap reconstruction, below the fixed -1 dBTP ceiling.
- Non-finite input handling, quiet impulse latency, no allocations during limiter process calls: passed.
- `g++ -std=c++17 -fsyntax-only` against available JUCE headers: Main, MainComponent, SessionState, StemFileImporter, InsertChain, all processor/editor implementation units and SessionTests: passed.

Supplied but not executed here:
- Linked native `hem_session_tests` with XML state and routing checks.
- Windows application build/runtime, simultaneous tracks, real-time performance under full 104-processor load, disk/device changes, XML save/recall stress tests.

CMake builds all eight test targets for the desktop. No Windows executable or installation package is claimed. Existing v0.6 numerical test results remain applicable to unchanged DSP kernels; see README. The new master test additionally validates their final master wrapper.
