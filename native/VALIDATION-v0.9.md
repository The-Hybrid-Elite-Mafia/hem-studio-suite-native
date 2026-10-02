> Historical recovered design/status document. Current implementation and verification take precedence in README.md, INTEGRATION-STATUS.md and VERIFICATION-2026-10-01.md.

# Version 0.9 validation — September 29, 2026

Executed against actual JUCE core/audio_basics/audio_formats implementations:
- TakeRecorderTests: mono and stereo 96 kHz 32-bit float WAVs; exact samples; 10,240-frame continuity; background finalization; latency-placement sidecar; zero allocations during instrumented push.
- C++ syntax checks: TakeRecorder, TrackingInsertChain/LiveVocalManager, VocalTrackingPanel, MainComponent and recording/pipeline test source.

Pending Windows runtime:
- Microphone permission/device/channel selection and duplex driver behavior.
- Thirteen backing stems plus live recording under realistic disk/CPU load.
- Actual loopback alignment; physical direct monitoring; monitor feedback checks.
- hem_vocal_pipeline_tests runtime: dry/insert/direct return paths, input channel switching, natural live-rack delay.
- Disk-full/device-loss/cancellation stress tests and UI acceptance.

No physical audio device was used in these authoring tests. Software monitoring includes I/O latency and the final master limiter. Hardware direct monitoring cannot be universally enabled through JUCE.
