> Historical recovered design/status document. Current implementation and verification take precedence in README.md, INTEGRATION-STATUS.md and VERIFICATION-2026-10-01.md.

# Version 0.8 verification — September 29, 2026

Executed in this environment:

- MonitoringRouterTests compiled and ran: exact direct output, -9 dB measured correction, original master unchanged, return to unity and device-reset mode at 44.1 / 48 / 96 kHz.
- AnalysisTests compiled and linked against actual JUCE core, audio_basics and dsp modules, then ran: -6.0206 dBFS left and -12.0412 dBFS right FFT-bin calibration; waveform extrema; overflow counter; sample-rate epoch reset; no allocations in the instrumented audio push.
- MonitorProfileTests compiled and linked against actual JUCE core, then ran: calibration JSON save/load; bell, low-shelf and high-shelf response; operative shelf Q; invalid sample-rate/file rejection. File tests use the writable working directory.
- New/changed host, session, preset, analysis, calibration and test C++ sources passed syntax checks against JUCE headers.

Pending on the desktop:

- Full application link/run in Visual Studio, UI and device switching tests.
- hem_session_tests including the added all-processor JSON session round-trip, listing, invalid-rename and delete assertions. Source syntax passed; this target has not been linked/run here.
- Real-time performance at maximum insert count, disk/read-ahead stress, and physical Bluetooth/wired monitoring.

No tested synthetic correction is represented as a measured Sony profile. Final-output limiter checks from v0.7 remain applicable; that limiter is unchanged. No recording or export subsystem is newly claimed.
