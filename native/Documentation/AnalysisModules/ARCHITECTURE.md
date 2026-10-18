# Approved modules 29–31: implementation checkpoint

6 October 2026. Scope: original C++20 code in isolated files; no original DSP, limiter, registry, editor, or host changes. Approved final images 29, 30 and 31 were visually inspected before this design.

## Ownership and thread model
- Module adapters derive from existing ModuleProcessor and preserve APVTS, preparation, meter and bypass conventions. UI/CMake/registry owner integrates these adapters.
- Meter/analyser audio paths are exact pass-through. Audio copies actual stereo samples into a fixed single-producer/single-consumer queue. No audio allocation, mutex, notification, FFT, GUI pointers or standard-library container growth.
- Analysis consumes on a polling worker. Frames carry actual sample rate, absolute sample indices and epoch. Queue discontinuities are explicit: no fabricated interpolation or silently continuous integrated loudness. UI obtains coherent worker snapshots through a nonblocking off-audio lock. Reset/pause/freeze are explicit controls.
- Dependency-free numerical kernels are separately testable. Existing AudioMeters is reused for sample peak/RMS; existing reconstruction coefficient design is reused for an explicitly labelled 8x FIR peak estimate. This is not a claim of certification.

## 29 Loudness / Metering Lab
Original implementation of stereo BS.1770 K weighting and absolute/relative gated integrated loudness; 400 ms and 3 s windows; EBU Tech 3342 LRA using >=10 Hz short-term values. Non-48 kHz coefficients are transformed from the specified 48 kHz transfer functions with inverse/forward bilinear mapping. Storage is allocated before analysis; a bounded session history (four hours) refuses further integrated/LRA accumulation at capacity and reports overflow, never silently rolls measurement history. M/S/history remain available. Discontinuities invalidate integrated/LRA until reset. True peak is an 8x/32-tap FIR estimate reused from existing DspCore, with its own fixtures, not a renamed sample peak. Correlation and crest derive from actual samples. Meter mode (peak/RMS/VU), hold, pause and reset have defined effects. VU is a 300 ms average-rectified approximation with 0 VU at -18 dBFS sine RMS, explicitly labelled; no IEC compliance claim.

## 30 Spectrum / Spectrogram
Worker FFT (radix-2, preallocated 512/1024/2048/4096/8192) with Rectangular, Hann or Blackman window. Coherent gain and DC/Nyquist calibration are explicit. Channels: stereo maximum, left, right, mid=(L+R)/2, side=(L-R)/2. RMS spectrum reports bin-centered sinusoidal RMS (peak/sqrt(2), except DC/Nyquist); it is not broadband integrated noise RMS. Averaging is exponential in linear power using actual hop time. Spectrogram frames carry individual timestamps. Max hold, freeze, reset, display range and history apply to real data. Flat/depth are presentation modes only.

## 31 Delta Monitor
Processed stream is the insert input; reference must be independently supplied by host in the same callback, consumed exactly once and checked for count/rate/timeline compatibility. Host metadata supplies upstream latency; signed manual trim adjusts relative alignment using preallocated delay rings. Delta is processed minus aligned dry. Selection processed/dry/delta, monitor gain, actual stereo peak meters, correlation and time-domain decimated extrema use real streams. Automatic alignment is an explicit off-audio normalized cross-correlation estimate with bounded lag/confidence, never an audio-thread search; ambiguous or uncorrelated material yields no update. Missing route never masquerades as a perfect null: outputs preserve processed input and mark reference and delta unavailable. Host routing integration is required separately; source menus must reflect actual routes.

## Primary references and verification boundary
- ITU-R BS.1770-5, Annex 1: https://www.itu.int/rec/R-REC-BS.1770-5-202311-I
- EBU Tech 3341 (2023): https://tech.ebu.ch/docs/tech/tech3341.pdf
- EBU Tech 3342 (2023): https://tech.ebu.ch/docs/tech/tech3342.pdf
Original implementation, no external source-code dependency or copied third-party library. Existing JUCE licensing still applies to adapters. Numerical fixtures and allocation tests do not establish full EBU/ITU certification. No Windows, ASIO, physical device or listening validation is claimed.
