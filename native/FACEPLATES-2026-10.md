# 2026-10 faceplates (Studio Suite 0.12.0)

One new plugin and three replacements, each on Steven's new faceplate artwork (`Assets/Faceplates2026/`).
The artwork is the background only. Every knob, button, slider, meter, value box and display is drawn in code on top of it and is wired to a real parameter or a live measurement.

| Registry ID | Plugin | Status |
|---|---|---|
| `vocal-de-reverb` | Vocal De-Reverb | **New** |
| `sipa` | S.I.P.A. / Stereo Image & Phase Architect | Replaces the previous S.I.P.A. (Stereo Phase Lab faceplate) |
| `transient` | Transient Shaper | Replaces the previous Transient Shaper & Attack Enhancer |
| `breath-mouth-control` | Breath / Mouth Control | Replaces the previous Breath / Mouth Control |

Registry IDs and state types are unchanged. Sessions saved with the previous S.I.P.A., Transient and Breath controls load and are converted (`migrateParameterState`):

- **S.I.P.A.:** the old L/R Balance has no equivalent and is dropped.
- **Transient:** the old attack/sustain dB values become ±% of 18 dB.
- **Breath:** the old breath/mouth pair becomes one Detection choice using the larger reduction.

The previous classes remain in the source for their own tests but are no longer registered.

## What each control does

**Vocal De-Reverb** (STFT; latency = one FFT frame: 1023 samples at 44.1/48 kHz, 2047 at 96 kHz, 4095 at 192 kHz)
- **How it works:** late reverb is predicted from the signal 50 ms earlier using the decay time (T60); early reflections are predicted from the previous frame. A spectral-subtraction gain with a floor removes the prediction.
- **Reverb Reduction:** overall amount.
- **Early Reflections / Tail Reduction:** how much of each part is removed. 50% removes the modelled part once; 100% removes it twice.
- **Ambience:** floor, from −36 to −3 dB.
- **Room Size:** T60 from 0.15 to 3 s. In Adaptive and Learned it scales the measured T60 by ×0.5 to ×2.
- **Damping:** shorter high-frequency decay.
- **Clarity:** protects consonant onsets.
- **Presence:** up to +6 dB at 4 kHz.
- **Dry/Wet** and **Output** (±24 dB).
- **Mode:**
  - Natural.
  - Aggressive: stronger subtraction, faster gain.
  - Transparent: gentler, smoother.
- **Algorithm:**
  - Spectral: T60 from Room Size.
  - Adaptive: T60 measured from the material's own decays.
  - Learned: the room captured while LEARN is on.
  - The faceplate art said "AI Assist". The button reads **LEARNED** because there is no AI or neural model in this plugin.
- **Frequency Focus:** everything below it is left untouched (one-octave fade).
- **Detection Sensitivity:** ×0.5 to ×2 subtraction.
- **Stereo Processing:** Mid, Side, or Mid/Side (both).
- **Learn / Reset / Bypass.**
- **Displays:**
  - Before/after spectrum.
  - Last 3 s of energy (before/after).
  - Goniometer with BEFORE/AFTER/BOTH.
  - T60 and live reduction readout.

**S.I.P.A.** (zero latency)
- **Width:** 0–200% side gain.
- **Pan Law:** center (mid) level, ±6 dB.
- **Center Focus:** removes side energy around 1 kHz.
- **Depth:** a 12 ms delayed, darkened copy of the mid added to the side.
- **Mono Bass:** 0 = off, up to 200 Hz, LR4.
- **Output.**
- **Phase Control:** Normal / Invert L / Invert R / Swap.
- **Phase Rotate:** ±180°. R is rotated against L with a Hilbert pair (verified 90° from 30 Hz to 20 kHz).
- **Stereo Modes:** Stereo / Mid-Side (M on L, S on R) / Side Only / Mono.
- **Mid/Side Balance.**
- **Harmonic Width:** harmonics of the upper mid added to the side.
- **Air Enhance:** side shelf above 9 kHz, up to +9 dB.
- **HPF/LPF Side.**
- **Dynamic Width:** narrower on transients, wider on sustain.
- **Output Trim:** applied after Mono Check.
- **Mono Check.**
- **Width presets:** Narrow / Normal / Wide / Max set Width to 50 / 100 / 150 / 200%.
- **Displays:**
  - Field: Vector / Polar / Correlation history.
  - Phase scope: Left / Both / Right; the phase readout is acos(correlation).
  - Measured width fan.
  - Correlation meter.
  - Output meter (peak + RMS).
- **Back** returns to the rack. **Presets** opens five starting points.

**Transient Shaper** (zero latency)
- **Attack / Sustain:** ±100% = ±18 dB on onsets and decays.
- **Sensitivity:** detector contrast ×0.25 to ×4.
- **Transient Length:** holds the attack gain (0 = AUTO).
- **Low / High Focus:** scale the effect below 200 Hz and above 4 kHz.
- **Clip Guard:** soft knee into a −0.1 dBFS ceiling, as the last stage.
- **Mix** and **Output.**
- **Detection Mode:** Transient / Sustain / Adaptive (follows the crest factor).
- **Focus Range:** the detector band (Low 40–250 Hz, Mid 250 Hz–4 kHz, High 4–16 kHz).
- **Process Mode:**
  - Clean.
  - Punch: adds harmonics on onsets.
  - Smooth: 5 ms gain smoothing.
  - Aggressive.
- **Solo:** detector band only.
- **Delta:** only what changed.
- **Bypass** and **Reset** (all controls to default).
- **Displays:** transient/sustain lanes, colored waveform detail, real-time monitor bars.

**Breath / Mouth Control** (2 ms lookahead latency)
- **Detection:**
  - Breath: noise-like sound 12–50 dB below the recent vocal peak.
  - Mouth Noise: impulses above 5.5 kHz.
  - Sibilance: high share of 4.5–10 kHz energy.
  - All.
  - These are heuristic detectors, not a trained classifier.
- **Frequency Focus:** where the breath detector listens (Low / Mid / High).
- **Mode:** detector speed (Vocal / Speech / Rap).
- **Reduction stage:** a dynamic bell cut at Frequency / Q, with Threshold, Attack, Release, Reduction (up to 30 dB), Mix and Output.
- **Process Order buttons** switch stages:
  - Detect off: the cut is always applied.
  - Filter off: the cut is broadband.
  - Dynamic off: full Reduction whenever detected.
  - Output off: Mix and Output are ignored.
- **Listen:** Normal / Reduction (hear what is removed).
- **Solo:** the filter band only.
- **Bypass.**
- **Tabs and the View menu** switch the display: Spectrum with band overlay / Waveform / Spectrogram / Reduction history / Info.
- **Sidechain External is shown but disabled:** Studio Suite has no track-to-track sidechain routing yet.

**All four:**
- Input/output meters.
- Double-click a knob to reset it.
- A/B, Undo and Redo where the faceplate has them. A/B: B starts as a copy of A.

## Real-time safety

- All buffers are sized in `prepareToPlay`.
- `processBlock` does not allocate, lock or block. `Tests/StudioFaceplateDspTests.cpp` counts allocations during every process call.
- Displays read a lock-free single-producer/single-consumer tap on a 30 Hz message-thread timer.
- The UI changes the audio only through APVTS parameters.

## Tests

- **`hem_studio_faceplate_dsp_tests`** (no JUCE) checks:
  - De-reverb transparency at 0% and latency at 44.1–192 kHz.
  - De-reverb tail reduction on a synthetic room in all three algorithms.
  - The adaptive T60 estimate and Learn.
  - The S.I.P.A. Hilbert pair.
  - Every S.I.P.A. mid/side control.
  - Transient attack/sustain/length/clip-guard/bypass/delta.
  - Breath / mouth / sibilance detection and reduction.
  - Zero allocations.
- **`hem_studio_faceplate_editor_tests`:**
  - Every parameter of all four has a visible control that reaches its APVTS value (knobs, toggles, choice buttons).
  - Double-click defaults.
  - Undo / Redo / A-B.
  - Old-session migration.
  - Renders a PNG of each editor with live audio (`%TEMP%/hem-faceplates`). The Windows workflow uploads these.
