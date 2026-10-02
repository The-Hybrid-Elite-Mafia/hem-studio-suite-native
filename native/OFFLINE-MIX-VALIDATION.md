# Offline mix renderer

## Implemented scope

- Worker-only synchronous stereo float32 WAV rendering, 48,000 Hz by default
- Exactly thirteen track snapshots and eight existing `InsertChain` slots per track
- Global input trim → per-track insert chains → track gain/mute/solo → stereo sum → output gain → the existing `CoreMasterStage`
- Existing processor registry, strict parameter restoration, saved bypass state and fixed slot-latency compensation; no new limiter implementation
- WAV source reads occur synchronously on the export worker. JUCE `ResamplingAudioSource` uses each source's actual rate; mono sources are duplicated to stereo
- The export always starts at source frame zero. Session cursor and looping are deliberately ignored
- Output duration is `ceil(max(sourceFrames/sourceRate) * outputRate) + ceil(tailSeconds * outputRate)`; tail defaults to two seconds
- Processing continues for the entire requested duration plus fixed eight-slot chain latency and the actual latency reported by `CoreMasterStage`. Those leading latency frames are discarded, rather than written as silence
- Monitor EQ, monitor calibration, hardware inputs and live vocal capture have no route into this graph

## Resource and safety contract

Sources stream in bounded blocks. The renderer never loads an entire stem into memory and uses one reusable stereo track buffer plus one stereo summing buffer. Processor/resampler histories are bounded by the fixed track/slot count, supported rate and block limits. The snapshot caller must capture an independent immutable session before starting the worker; each restored parameter tree is copied again privately.

Supported export configuration: integer sample rates 8,000–384,000 Hz, blocks 1–8,192 samples, finite tail 0–3,600 seconds, and the session's existing gain ranges of −60 to +6 dB. Source WAVs must be nonempty mono/stereo at 8,000–384,000 Hz. All declared inserts are checked, including bypassed inserts and inserts on empty tracks. Unknown and monitoring-only inserts, malformed/missing/nonfinite parameters, invalid gains, decoder failures and nonfinite audio fail the operation.

The requested file must not already exist. A same-directory temporary WAV is written, its header finalized, its stream flushed and its RIFF/float32/stereo/rate/frame count verified. A filesystem hard link then atomically publishes it with no replacement semantics; an existing file, directory or dangling symlink cannot be replaced, even if created during rendering. The temporary is removed on cancellation or failure. Filesystems without hard-link support fail safely; there is no unsafe rename-overwrite fallback. The final cancellation boundary is immediately before publication. Cancellation after successful publication does not retroactively delete the completed file.

The classic RIFF data budget is conservatively capped at `0xff000000` bytes (534,773,760 stereo float frames), leaving header headroom and refusing RF64/RIFF overflow before writing. Disk flush failure prevents publication. Progress is monotonic from zero to one; one means the file was published. Progress exceptions are ignored because the callback is observational. A cancellation callback exception fails the render safely.

The render uses fresh instances of the existing processors. Runtime learned detector/filter state and automation are not part of `SessionSnapshot`; stored controls and the processors' existing startup smoothing are reproduced. The requested tail is a fixed duration, not a claim that every possible processor has decayed to mathematical silence. JUCE's resampling filter phase is retained; no invented sample-rate-converter latency estimate is subtracted.

## Focused executable coverage

`Tests/OfflineMixTests.cpp` exercises:

1. Thirteen different stereo stems, unequal lengths, exact frame/data/header sizes, input/track/output gain, mute, multi-solo, mute precedence and all-muted silence
2. Cursor/loop exclusion and longest-loaded-source duration, even when a longest source is muted
3. Exact sample parity with the byte-preserved baseline limiter at 44.1/48/96 kHz, including first/last-frame impulses and overloads, real latency alignment and a retained tail
4. One-frame input, one-sample processing blocks, zero tail and final writes unaligned to block/latency boundaries
5. All eight insert slots, non-default restored parameters, immediate saved bypass state, and staging every registered non-monitor processor
6. Bit-identical monitor-state exclusion, including malformed monitor settings that must not enter the render graph
7. Actual 24/44.1/96 kHz → 48 kHz resampling, tone pitch/amplitude, PCM WAV decoding, mono duplication and upward rounding of fractional durations
8. Missing/corrupt/nonfinite sources, invalid gains/rates/blocks/tails, unknown/monitor inserts, invalid/missing parameters and parameters attached to an empty slot
9. Cancellation before render, during render and after the final audio block; temporary cleanup; preservation of existing destination bytes and a destination created mid-render
10. Dangling-symlink protection where supported, RIFF capacity rejection, progress completion and throwing progress callback isolation

No physical audio device is needed. These checks validate host routing, format, transactions and parity with the existing master algorithm. They do **not** certify reconstructed true peaks or change the outstanding true-peak certification status.

## Execution record

On 2026-10-01, the integration build compiled the native application and both export test executables against JUCE 8.0.10. Every native-host C++ object was recompiled after the illuminated-control look-and-feel update; only unchanged JUCE modules and baseline objects were reused after source-hash verification. Both the initial and final current-revision `OfflineMixTests` runs passed every category above, including exact sample equality with the baseline at 44.1/48/96 kHz, exact isolated impulse placement, insert order/bypass, mixed-rate duration, resampling, cancellation/cleanup, destination-race protection and RIFF rejection. The independently owned `OfflineMixWorkerTests` also passed worker busy exclusion, message-thread completion, restart, cancellation, shutdown cleanup and export-panel raster checks at 1×/2×.

Evidence: `Verification/OfflineMixTests.run.log` and `Verification/OfflineMixWorkerTests.run.log`. There are also passing final `StudioBrandingTests` and `HardwareLookAndFeelTests` for the integration's branding/control rendering.

**Final status: PASS.** The final defensive renderer revision (avoiding allocation of a failure message after a successful file publication) is compiled into the application and included in the final passing test rerun. Final native application SHA-256: `972d9471041b97d06f6a722ae997277071607fe8c8a982ecfcc41feb0f8c06eb`.

The preserved baseline files were independently checked against `native-host-integration` without modifications:

- `Source/CoreBaseline/Engine.h`: SHA-256 `84a3829568bc0cb5c99280769bbf9e72f128d5015a8d48e0a3a24debf582217f`
- `Source/CoreBaseline/Engine.cpp`: SHA-256 `d409243bff6a3f646d9af5726c64c99b378017845bbf295a61f7e1de710802a4`

Reconstructed true-peak certification remains pending; these passing tests establish unchanged algorithm parity and export behavior only.
