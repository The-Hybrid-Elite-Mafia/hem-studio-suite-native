> Current status: numerical checks and native raster tests passed, including the final-source rerun. See Verification/HardwareLookAndFeelTests.run.log and VERIFICATION-2026-10-01.md. Earlier pending notes below are historical.

# Live controls and final-output meters

## Source changes

- `Source/HardwareLookAndFeel.h`: real dial faces, value-dependent rotary pointers and arcs in both ordinary and artwork modes; moving horizontal/vertical fader thumbs; stateful text/toggle button illumination; disabled and keyboard-focus feedback. Labels and numerical value components are never transformed or rotated.
- `Source/UI/ControlGeometry.h`: bounds-local rotary and linear geometry, independent of JUCE and audio processing.
- `Source/Systems/AudioMeters.h`: independent L/R sample-peak and exponential RMS detectors. The audio-owned state never allocates or locks in `push`; the UI sees lock-free atomic publications. One block may publish several times if the host chunks its callback, but values depend on samples and actual sample rate, not callback partition size.
- `Source/Systems/CalibratedMeterView.h`: opaque native panel with an absolute 0/-6/-12/-24/-48/-60 dBFS scale, separate L/R bars, numeric sample-peak and RMS readings, amber peak-hold markers, red clip lamps and invalid-sample indication. This covers any static artwork under the meter.

These are display/control-layer changes. Existing APVTS attachments and host gain callbacks remain responsible for the corresponding real parameters. No synthetic activity or new DSP is claimed.

## Parent integration contract

1. Assign non-empty visible text to the seven artwork host buttons (`PLAY`, `STOP`, `REWIND`, `+10s`, `LOOP`, `FILE`, `STUDIO`). The new button background intentionally conceals the old printed icon under each actual control.
2. Reflect actual engine state with `play.setToggleState(playing(), juce::dontSendNotification)` and `loop.setToggleState(looping, juce::dontSendNotification)` in the UI refresh. Do not enable click-to-toggle; successful engine state is the source of truth. Remove the old parent-painted play/loop outline indicators to avoid duplicate borders.
3. Keep the existing fixed input/output labels and parameter attachment callbacks. Do not add value text inside the dial body.
4. Own `hem::AudioMeters outputMeters;` before `hem::CalibratedMeterView outputMeterView{outputMeters};`; add the view as a child. Place it with the same `map` used for the artwork, approximately `(1320, 282, 182, 328)` as adjusted by the integration layout. Never compute a second independent screen transform in the meter.
5. Call `outputMeters.prepare(actualRate)` during audio preparation. Push the exact emitted L/R samples after the final limiter. For actual mono output, pass the final emitted mono signal for both channel pointers. Do not pass a pre-limiter, pre-monitor or unplayed scratch buffer.
6. Remove the old single-channel parent-painted meter. The view owns its opaque background, truthful scale, bars and 25 Hz display refresh.
7. The parent integrated the meter and geometry test targets into CMake. Native JUCE raster tests require the GUI build. The owner subsequently authorized continued testing without per-test pauses.

## Meter definitions

- Sample peak: absolute sample detector, immediate attack, 24 dB/s exponential release. It is not an intersample/true-peak measurement.
- RMS: square-root of a sample-driven exponential mean square, 300 ms time constant; a settled sine measures amplitude / sqrt(2). It is not LUFS, loudness normalization, or a rectangular-window RMS.
- Held peak: refreshes when a sample equals/exceeds the held value; holds 1 second, then falls 18 dB/s.
- Clip: independent latch at finite |sample| >= 1.0 (0 dBFS). Final-output limiting may mean it never lights during normal operation.
- Non-finite samples: NaN/infinity are counted and treated as zero only inside the detector. The meter never edits audio.
- Silence: numerical readings floor at -120 dBFS; the drawn scale bottoms at -60 dBFS.
- Reset: click the panel or press Enter/Space while focused. The request clears clip latches and the invalid-sample counter at the next audio push. New clipping in that callback re-latches. It does not erase signal levels or reset a device.

## Verification executed on 2026-10-01

At 08:24 UTC, after the owner authorized uninterrupted testing, both dependency-free executables compiled and passed with `-Wall -Wextra -pedantic` and no compiler warnings:

- `hem_audio_meter_tests`: DC and sine calibration at 44.1/48/96 kHz; independent L/R levels; silence; callback partition invariance; exact release and hold timing; clip threshold/latch/reset; non-finite input counts; extreme finite input; rate reprepare; zero instrumented callback allocations.
- `hem_control_geometry_tests`: local-coordinate dial rotation; pointer endpoints; correct translated/scaled bounds at 0.5x through 2x; invalid dimensions/positions; clamped fader positions.

No source fix was required by either run. These passes do not establish native widget rendering, actual device routing or physical-audio acceptance. Native JUCE raster/full-host execution is coordinated by the parent task.

Dependency-free meter executable:

```sh
mkdir -p build-ui-meter
c++ -std=c++20 -O2 -Wall -Wextra -pedantic Tests/AudioMetersTests.cpp -o build-ui-meter/hem_audio_meter_tests && build-ui-meter/hem_audio_meter_tests
```

Dependency-free control geometry executable:

```sh
c++ -std=c++17 -O2 -Wall -Wextra -pedantic Tests/ControlGeometryTests.cpp -o build-ui-meter/hem_control_geometry_tests && build-ui-meter/hem_control_geometry_tests
```

`Tests/HardwareLookAndFeelTests.cpp` is the native JUCE raster-rendering test. It now paints the actual `CalibratedMeterView` against real post-push detector snapshots: first-paint state; fixed 0/-6/-12/-24/-48/-60 dBFS grid/bar geometry at 1x and 2x; stereo separation and channel swap; RMS fill; opaque background and true silence; independent clip lamps and audio-thread reset. The view captures a source snapshot in its constructor, then continues normal 25 Hz snapshot updates. This test needs the actual JUCE graphics/GUI target and generated `JuceHeader.h`; its native execution is still pending. Numerical tests alone do not establish native widget rendering or pointer interaction.

## GUI/device acceptance still needed

- Drag input/output and plugin dials through minimum/midpoint/maximum; verify pointer, fixed number and actual gain/parameter move together; double-click restores the parameter default.
- Drag the vocal monitor and placement sliders; verify thumb/value and the already-bound setting agree. Keyboard control and repeated edits must still work.
- Play and Loop show active light only after the engine accepts the action; press/release of momentary buttons does not latch them. Mute/Solo/Bypass/toggle checkboxes follow true state and stay visibly off otherwise.
- Resize to 16:9, wide and tall windows; mapped controls and meter stay over the letterboxed artwork, without duplicate static symbols or indicators.
- Feed settled stereo sine/DC/silence signals; check correct independent numeric levels, fixed scale, release and hold. Confirm meter silence when the emitted device samples are silent.
- On actual mono hardware, confirm both meters match the single emitted signal. Perform physical listening at safe volume; meter tests do not validate hardware routing or acoustic loudness.

## Later native verification

The expanded HardwareLookAndFeelTests was compiled against the real native JUCE objects and passed on 2026-10-01, covering both controls and the actual CalibratedMeterView at 1x/2x. The host also compiled, launched, and passed focused mouse/value/Loop checks. See VERIFICATION-2026-10-01.md.
