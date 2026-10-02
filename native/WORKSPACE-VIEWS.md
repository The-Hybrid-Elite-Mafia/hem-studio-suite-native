# Native one-window workspace

`Source/UI/NativeWorkspace.{h,cpp}` is a native JUCE component, embedded by the host.
It does not create a `StudioWindow`, own an audio device, start/stop/seek transport,
or substitute a different audio engine. HOME, SETTINGS and the shared transport
remain host-owned. The host selects the four workspace pages by `setPage`.

## Implemented views

- **Plugin:** the selected real track, all eight serial insert positions and their
  actual bypass states, plus the selected existing native editor. A draggable
  divider is bounded to 22–60% left-pane width. Expand/restore hides/restores the
  stem pane. Native editor dimensions are preserved inside a two-axis viewport;
  no screenshot or replacement editor is used. Stem controls scroll vertically
  if necessary. Bypass retains existing chain delay compensation.
- **Mixer:** all 13 engine track names, post-insert trim sliders with the actual
  -60 to +6 dB range and 0 dB double-click reset, numeric dB readouts, mute, solo,
  and track/editor selection. Small windows scroll rather than shrinking strips
  below 90 logical pixels per channel. Strip height follows the visible viewport
  down to 270 logical pixels: the fader/meter region compresses while numeric
  readouts and Mute/Solo/Edit keep their readable sizes. At the default host
  interior of 1046x395, all controls fit vertically; only horizontal scrolling
  is needed to reach the 13 channels. Each stereo meter reads the engine's
  `TrackEngine::signalMeters`, measured after trim/mute/solo. Its fixed -60..0
  dBFS scale shows sample peak, exponential RMS and the detector's one-second
  peak hold. L/R lanes are labeled explicitly. Click a meter or use Enter/Space
  to request clip-latch reset, consumed on the next audio publication.
  Unavailable meters are labeled; no synthetic motion is generated.
- **Plugin Vault:** exactly the 12 entries from `PluginRegistry::entries()`.
  Text search combines with categories derived from that same registry. Eleven
  entries request installation/replacement in the selected track/slot via the
  host callback. The monitoring-only EQ opens the listening processor instead.
  Hardware presence is not a prerequisite for browsing or requesting insertion.
- **Routing:** a read-only diagram of existing fixed paths: 13 source tracks,
  input gain, eight inserts each, trim/mute/solo, stereo sum/output gain,
  pre-listening analysis tap, vocal listening contribution, direct/corrected
  route, optional mono fold-down, final Task 2B limiter and device output.
  Track contributions and emitted sample peak/RMS come from actual meter
  snapshots. Limiter latency is reported by the host; gain reduction appears
  only when supplied as an actual optional measurement. No invented rewiring,
  moving pulses, extra DSP, LUFS or certified true-peak claims are present.

## Integration and ownership

Construct `hem::NativeWorkspace` with `Callbacks`. `bank` is a getter because
session recall can replace the whole bank. Get/set selected-track and selected-
slot callbacks use zero-based indices. `onOpenSlot` opens the selected existing
processor; `onInstallModule` receives the zero-based real registry index;
`onMonitor` opens the existing monitor processor. `routeTelemetry` is optional.
No processor pointer is retained by the workspace, except the unavoidable
borrow inside its currently owned `AudioProcessorEditor`.

**Before any processor or bank retirement, the host must call `clearEditor()`,
even if another page currently hides it.** `setEditor` replaces only the editor,
never the processor. Clear the workspace before host processor members die.
The workspace's timer updates real state at 20 Hz on the message thread. Bank
publication, insert replacement and retirement remain the host's responsibility;
none are performed by the UI timer. Direct trim/mute/solo and bypass changes use
existing atomic engine controls.

The views reuse `HardwareLookAndFeel` and source-drawn cyan/amber chrome. Toggle
illumination reflects selection, expansion or actual engine state. No image
asset was generated. Registry scope remains 12, not a claimed 100-entry vault.

## Validation

`Tests/NativeWorkspaceTests.cpp` uses the production component, TrackBank,
PluginRegistry and real native editor without opening a hardware device. It
checks actual callback dispatch and gain/bypass/mute/solo state, clip reset
consumption through real meter publication, all 13 mixer
strips, registry search/category combinations, monitoring-only dispatch, native
editor destruction, editor viewport dimensions, split/expand, bank replacement,
transport continuity across navigation, real meter-driven pixel changes and
raster/readability checks at 1255x489, 1046x395, 960x420 and 1882x733.
The 1046x395 regression checks fader/readout/Mute/Solo/Edit bounds against the
actual visible viewport, including scrollbar space, and verifies horizontal
scrolling fully exposes channel 13. Supported sizes require no vertical mixer
scrolling. Pass an optional
first command-line argument to the test binary to save PNGs of these real UI
rasters plus the actual embedded native editor and expanded view for inspection.

Verified in the available Linux JUCE 8.0.10 / C++20 environment:

- `python tools/build-export-reusing-verified-objects.py workspace-test` passed
- `build-export/NativeWorkspaceTests <absolute path>/build-export/workspace-renders`
  passed again and wrote 19 production-component PNGs
- Visually inspected the native S.I.P.A. editor, expanded editor, all four pages
  at design size, small-window mixer/plugin, and large-window vault. Numeric dB
  readouts and L/R lanes are intact. Narrow layouts preserve control sizes and
  expose scrollbars. Native editor overflow is accessible through its viewport
- The default-host 1046x395 mixer regression passed: gain/readout/Mute/Solo/Edit
  are wholly within the actual vertical viewport. Inspected both horizontal
  endpoints; channel 13 is fully accessible without vertical scrolling. The
  1255x489 design-size layout retains its previous geometry
- Only the existing Fontconfig “No writable cache directories” messages appeared
  at runtime; the process exited 0 with all assertions passing
- Tested executable SHA-256:
  `39a8fd2c232f94fb88cc408778193de9cb0f2166bad577fa9497f568b779c429`

Final affected-object integration validation passed on 2026-10-01, including the current plugin-meter and close-safety changes. The final runtime log is Verification/NativeWorkspaceTests.run.log. The actual final 1280×720 application was also inspected: Mute/Solo/Edit are visible without vertical scrolling. These checks do not certify physical audio devices, Windows drivers or the limiter's reconstructed true peaks.
