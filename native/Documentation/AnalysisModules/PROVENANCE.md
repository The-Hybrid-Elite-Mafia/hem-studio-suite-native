# Implementation provenance

Original C++20 code implemented for this project; no new third-party source, IR, sample asset or binary algorithm dependency.

Existing project components reused:
- `AudioMeters.h` for sample-peak/RMS computation
- `DspCore.h/.cpp` Reconstruction (8x, 32 taps) and Ramp
- ModuleProcessor / APVTS / ModuleEditor / host bypass architecture
- SuiteMaterialLook and HardwareLookAndFeel, plus approved artwork and existing original material sample
- VocalSpectrum's existing bounded SPSC/polling-worker architectural pattern, with new configurable kernels and capture contracts

Primary algorithm references, consulted 6 October 2026:
- ITU-R BS.1770-5 Annex 1, specified 48 kHz K-weighting coefficients, channel-energy summation, 400 ms gating blocks and two-stage thresholds: https://www.itu.int/rec/R-REC-BS.1770-5-202311-I
- EBU Tech 3341 (2023), M/S integration times and generated calibration signal descriptions: https://tech.ebu.ch/docs/tech/tech3341.pdf
- EBU Tech 3342 (2023), LRA windows, gates and percentile range: https://tech.ebu.ch/docs/tech/tech3342.pdf

Generated test waveforms follow numerical descriptions in these standards. They are not repackaged EBU audio files. Numeric pass results establish those fixtures only, not certification or public-release license clearance. The true-peak estimator remains explicitly named as an existing 8x FIR estimate. Existing project/JUCE release licensing remains unchanged and requires its existing review.
