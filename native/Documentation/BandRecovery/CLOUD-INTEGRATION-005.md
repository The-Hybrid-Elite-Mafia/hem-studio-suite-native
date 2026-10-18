# Band30 integration checkpoint005

Functional Linux source checkpoint, not a release. Four recovered Band engines append to exact26-entry prefix; nine legacy engines retained. Seven Band DSP files are byte-exact recovery;29 prior DSP files unchanged. Four test-only int(K) casts were disambiguated for GCC.

Source integration retains Band v1/v2 migration with all existing resource/tempo hooks and captured tail bounds. Approved artwork mapping03Dynamic,04Dynamics,13MidSide,14Imager is exact. New UI repairs add host-sized defaults, single live pointers, independent stereo segmented meters, real preset/navigation handlers and stale/release/reprepare guards. Presets reuse existing host-state binary; file writes use TemporaryFile/readback; canceled and superseded callbacks are generation-guarded.

Band-only levels publication does not start an unused analysis worker. Existing worker defaults are unchanged. Callback C++ allocation probes pass; full C/OS/DLL allocation and whole-host lock instrumentation are not claimed.

Passed:541 bindings;30 editors/90 Back-Escape cycles; exact state/audio parity;13x8 recall; Band full numerical2/2; baseline numerical10/10; state migration/export parity;224 Band controls;412 plot pixels; stereo calibration; outer callback BPM; existing legacy telemetry and Delay export regressions.

Explicit acceptance blocker: final dense13-track/eight-EQ stress misses deadlines. No real-time capacity, Windows/device/listening or release certification. Popup and native file-chooser windows remain unverified without a display.

Known visual follow-up: Multiband Dynamics history clearing leaves original yellow waveform remnants at source y879..881, x418..1394. This is documented rather than hidden by the focused plot-pixel pass; correction is deferred until after checkpoint backup. Preserve adjacent authored cyan axis/frame marker.
