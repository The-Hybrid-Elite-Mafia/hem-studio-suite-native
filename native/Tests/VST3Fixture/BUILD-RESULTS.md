# Fixture build and self-test receipt

Verified 2026-10-01 on Linux x86_64 using GCC 14.2.0, CMake 4.4, and the existing
official JUCE 8.0.10 checkout. The existing cloud build environment was sourced;
no downloads, installs, purchases, third-party plugin discovery, scans, or runs
were performed.

## Result

- `HEMVST3TestFixture_VST3`: built successfully, including JUCE's generated
  VST3 module manifest
- `hem_vst3_fixture_tests`: built successfully
- CTest: 1/1 passed, 0 failures, 0.01 seconds on the final run
- Final test output:

```
PASS: stereo float, parameter defaults, exact gain/delay at 3 rates and variable blocks,
      0/64/262144 dynamic latency, bit-exact state, 57 corrupt-state rejections, unprepared silence
```

Final build and test output is retained in `build/final-build.log` and
`build/final-test.log`. CTest also retains `build/Testing/Temporary/LastTest.log`.

## Verified artifacts

Bundle:

```
/workspace/scratch/f1bbaa5cb1b9/HEM-Studio-Suite/native-host-vst3-integration/Tests/VST3Fixture/build/HEMVST3TestFixture_artefacts/Release/VST3/HEM VST3 Fixture.vst3
```

Shared library relative to the bundle:
`Contents/x86_64-linux/HEM VST3 Fixture.so`

SHA-256:
`896ab63e31b71d4ed1162dc2bcb7bc1a16080b37e54f9bf4186285e6cf61940e`

Fixture self-test executable: `build/hem_vst3_fixture_tests`

SHA-256:
`ad19f962581803659b9277c05d1205913470e0bb71899abf6a00bfd8d2440199`

## Limits

The native generic editor is implemented and compiled, but desktop editor
create/show/close/delete cycles were not executed: this shell has no `DISPLAY`.
The optional `--editor` self-test mode requires an existing GUI display. Actual
host adapter, VST3 controller, host state-envelope, dynamic-rejection and editor
ownership integration tests are separate from these direct processor tests.

No Windows/macOS build, third-party compatibility, physical audio device,
real-time deadline benchmark, sanitizer run, or production distribution is
claimed. Changes were confined to `Tests/VST3Fixture/`; host CMake, application,
immutable Task #2B, and existing tests were not modified by this fixture work.
