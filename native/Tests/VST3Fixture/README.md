# HEM deterministic in-house VST3 fixture

This directory contains original fixture source and a local build. It does not
discover, scan, copy, install, or run any third-party plugin. JUCE 8.0.10 supplies
the official VST3 wrapper; its existing licensing terms still apply. The fixture
is for host integration tests, not production mixing or a plugin release.

## Integration and build

The host may add `add_subdirectory(Tests/VST3Fixture)` after its JUCE
`add_subdirectory` and gate it with `HEM_BUILD_VST3_TEST_FIXTURE=ON`. The option
defaults to OFF when included, ON when this directory is the CMake source root.
The target is `HEMVST3TestFixture_VST3`. `COPY_PLUGIN_AFTER_BUILD` is OFF.

Standalone build from `HEM-Studio-Suite/native-host-vst3-integration`:

```sh
source ../native-host-offline-export/tools/cloud-build-env.sh
cmake -S Tests/VST3Fixture -B Tests/VST3Fixture/build \
  -DJUCE_PATH="$PWD/../limiter-evidence-audit/dependencies/JUCE-8.0.10" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build Tests/VST3Fixture/build \
  --target HEMVST3TestFixture_VST3 hem_vst3_fixture_tests -j 3
ctest --test-dir Tests/VST3Fixture/build --output-on-failure
```

Linux bundle (load **only this exact bundle**, not the containing directory):

```
Tests/VST3Fixture/build/HEMVST3TestFixture_artefacts/Release/VST3/HEM VST3 Fixture.vst3
```

The Linux shared library inside it is
`Contents/x86_64-linux/HEM VST3 Fixture.so`. Other operating systems require a
native build and may use different configuration/architecture subdirectories.
The local CMake only creates VST3, never VST2/AU/AAX or standalone audio devices.

## Stable test contract

- Product: `HEM VST3 Fixture`; manufacturer code `Hema`; plugin code `Hfx1`
- One stereo input and one stereo output bus; float32 only; no MIDI
- Parameter index 0 / JUCE ID `fixtureGain`: normalized `0..1` means linear gain
  `0..2`; default normalized `0.5` = unity. Continuous, no smoothing
- Parameter index 1 / JUCE ID `fixtureLatency`: normalized `0`, `0.5`, `1` means
  mode 0 / 1 / 2 and reported/actual latency `0`, `64`, `262144` samples
- `262144` samples is intentionally oversized for the current host's bounded
  compensation budget. Reject/quarantine this mode in host policy; do not grow
  the host's real-time buffers just to accept the test fixture
- Resolve actual host parameters by identifier/name. JUCE's VST3 wrapper may add
  its own bypass/program parameters; do not assume the host exposes only two
- Inputs are delayed exactly D samples, then multiplied by the gain snapshot at
  the start of the current block: `y[n] = gain(block) * x[n-D]`
- Both channels are independent. Defaults yield exact unity passthrough; using
  powers-of-two gain and binary fractions permits bit-exact sample comparisons
- Gain and latency are snapshotted together from one lock-free atomic 64-bit
  settings word. Non-finite parameter writes are ignored; finite writes clamp
  to `[0,1]`, and latency rounds to the nearest of its three modes
- Latency-mode changes reset delay history at the next audio block. The first D
  output samples after the change are zero. Mode transitions deliberately do
  not crossfade; this makes host latency-policy failures visible
- Parameter setters publish latency through JUCE's standard host notification.
  VST3 host/controller transport may apply the change at the next process call
  and deliver latency restart notifications asynchronously. Pump the normal
  host message loop and check at the host's documented safe boundary
- `prepareToPlay` allocates a fixed stereo ring of `2 * (262144 + 1)` floats.
  The fixture's `processBlock` has no allocation, lock, thread spawn, buffer
  resize, or host notification. Mode changes reset two counters rather than
  clearing the ring. Buffers larger than the prepared block size also work
- `reset` drops delay history; `releaseResources` frees the ring. An unprepared
  process call produces silence. Tail length is D / sample rate
- There are no fixture background threads. The real JUCE generic editor owns
  normal framework controls/timers and exposes both controls

## Opaque state and corruption tests

`FixtureProcessor::getStateInformation` returns exactly 24 bytes:

| Offset | Size | Value |
|---|---:|---|
| 0 | 8 | ASCII `HEMFX001` (format/version) |
| 8 | 4 | little-endian IEEE754 float32 linear gain |
| 12 | 4 | little-endian uint32 latency mode 0, 1, or 2 |
| 16 | 4 | reserved zero |
| 20 | 4 | little-endian FNV-1a uint32 over bytes 0–19 |

FNV-1a uses offset basis `2166136261` and prime `16777619`. This is an accidental
corruption detector, not a security boundary. Null input, other sizes, wrong
magic/version, nonzero reserved bytes, bad checksum, non-finite/out-of-range
gain, and invalid latency mode all leave the whole state unchanged. Successful
restores publish both controls in one atomic operation. Runtime delay history
is not persisted; reset/reprepare before comparing renders after a restore.

The VST3 host's opaque state contains JUCE wrapper framing around this 24-byte
processor payload. Save and restore the complete host-returned blob, without
assuming it is exactly 24 bytes. For corruption tests, flip a byte inside the
`HEMFX001` payload and confirm gain, latency, and reserialized payload stay
unchanged. A change to unrelated wrapper metadata is not a fixture corruption
test. `rejectedStateCount()` is available only to source-level self tests.

## Test coverage and limits

`hem_vst3_fixture_tests` checks exact stereo output with gain 0.5; dynamic modes
0/64/262144/0/64 at 44.1/48/96 kHz; uneven blocks of 1/17/64/127/256/513 samples;
state bitwise roundtrip; every-byte checksum corruption; truncated, trailing,
and null state rejection; seven checksummed but semantically invalid states;
non-finite parameter rejection; stereo-only layout;
and silence after release.

On a machine with an existing GUI display, run the same executable with
`--editor` to create, attach to desktop, show, hide, detach, and delete 32 native
generic editors. This is a source-level fixture check; tests that prove the
actual VST3 wrapper loads, exchanges parameters, restores host blobs, presents
an editor, and is unloaded cleanly belong in the host integration suite.

No third-party compatibility, crash containment, real-time deadline guarantee,
Windows build, external audio hardware, or production distribution is implied.
