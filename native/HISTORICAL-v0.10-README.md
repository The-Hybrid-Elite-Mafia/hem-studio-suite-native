# Historical recovered v0.10 documentation

This is archived background, not the current build/acceptance status. Read README.md, INTEGRATION-STATUS.md and VERIFICATION-2026-10-01.md first.

# HYBRID ELITE MAFIA — Native Studio Suite 0.10.0

This is the complete C++ source staging package for the JUCE standalone host, twelve registered processors (eleven track effects plus one optional, device-neutral monitor EQ), dedicated plugin windows and per-track insert racks. It is not a compiled Windows binary. `SOURCE-CODE.md` contains the complete source in copyable code blocks.

## Build in Visual Studio 2022

1. Install Visual Studio 2022 with Desktop development with C++, MSVC x64, Windows SDK and C++ CMake tools.
2. Obtain JUCE 8 from https://juce.com/ or https://github.com/juce-framework/JUCE and extract to `C:/SDK/JUCE`. JUCE licensing applies.
3. Extract this project, open Developer PowerShell in the project directory, and run:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DJUCE_PATH="C:/SDK/JUCE"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Open `build/HEMStudioSuite.sln`, set HEMStudioSuite as startup project, and run. The executable is under `build/HEMStudioSuite_artefacts/Release/`.

CMake adds your local JUCE checkout; generates JuceHeader.h; embeds `Assets/studio_suite_master.png` into BinaryData; compiles all sources; links audio utilities and processor modules; and builds fourteen CTest targets. No web stack or external runtime service is required. `HEM_DSP_TESTS_ONLY=ON` builds the five numerical test targets without JUCE.

## Native visual architecture

The supplied native master faceplate remains the letterboxed 16:9 background. The two host rotary hitboxes are transparent and mapped to that artwork. Other controls, the insert rack and dedicated plugin windows use heavy smoked obsidian colours, cyan outlines, amber active controls and fixed numerical readouts. All displayed parameters are attached to processor state.

The plugin windows are functional native-drawn editors, not exact coordinate overlays of the earlier plugin render images. The newly shown mastering-hub reference is not embedded as a second static UI. Exact per-plugin faceplate artwork integration and visual acceptance remain a separate step; this source package must not be represented as that completed visual integration.

## Added in version 0.9 — live vocal tracking

Open **VOCAL TRACKING** in the master bay. The new native panel has an actual enabled-input channel selector, device setup, recording controls, monitoring modes, a vocal-only eight-slot rack, a live input meter, and monitor level. It runs alongside all thirteen existing stem players without replacing or consuming a stem track.

### Input and simultaneous playback

The host requests up to two microphone input channels and stereo output from AudioDeviceManager. Select your available audio interface or USB microphone through INPUT / OUTPUT DEVICE; the channel selector lists the driver's enabled input names. AudioSourcePlayer compacts enabled hardware inputs into the buffer delivered to the host. The host captures those inputs BEFORE overwriting that buffer with playback output. Input channels crossfade over 10 ms when changed while idle; selection is locked during a take.

Changing the physical device can restart the driver. The host therefore permits hardware setup only while playback is paused/stopped and recording is finished. Selecting another already-enabled input channel does not restart the thirteen backing tracks. Device choices and full-duplex combinations depend on the OS/driver; this code cannot force incompatible input/output devices to share a clock. Start the Suno backing stems, then RECORD NEW TAKE. You may also record with backing stopped; starting backing playback during such a take is blocked. Pause/stop/seek ends the take to avoid silently creating discontinuous placement metadata. Turn off per-file backing loops before recording. Source replacement and session recall are blocked during recording.

### Recording and disk safety

`TakeRecorder` copies capture blocks into a fixed SPSC queue and writes WAV on its own background thread. Opening, file allocation, header writes and finalization do not run on the audio callback. The queue has 511 usable 256-frame packets (about 1.36 seconds at 96 kHz). If storage cannot keep up, recording stops with an error and retains a recoverable partial WAV; it does not silently omit a block and compress the take's timeline. A write/flush failure also retains the partial file. Takes stop before the legacy RIFF size limit; continuous file splitting is not implemented.

The default take is **mono 32-bit float WAV at the actual device rate**, taken before vocal inserts, monitor gain, calibration and the master limiter. PRINT INSERTS records stereo 32-bit float from the vocal insert chain. Choose this before arming; it is locked during recording. Monitor gain never changes the recorded take. New recordings never overwrite an existing file. STOP / FINALIZE TAKE drains queued audio and closes the header. The status reports completion or a concrete error. Closing the panel leaves recording active; the host VOCAL / ACTIVE button indicates activity. Stop the take explicitly or stop transport.

Each finished take has a `.wav.take.json` sidecar containing rate, channels, frame count, input device/channel, plugin state at arm time, driver input/output delays, backing/printed DSP latency, initial transport position and an estimated placement time. Formula: first callback timeline minus reported input + output + backing DSP + printed DSP delays, then plus the manual placement correction. Positive manual correction moves the take later. These are estimates, not measured round-trip compensation. Verify actual recording alignment with a physical loopback test on the chosen device. The current host does not automatically place a take onto a timeline or import it over an existing stem. Raw/printed takes are saved separately.

### Monitoring modes and actual latency

- MONITOR OFF: records normally with no software vocal return after the 10 ms fade.
- DRY SOFTWARE MONITOR: bypasses the vocal rack; still has hardware input/output and buffering latency plus the mandatory final 5 ms limiter.
- INSERT SOFTWARE MONITOR: serial vocal processing through the eight slots, then monitor routing and final limiter. Displays the sum of the actual processors' reported latency.
- INTERFACE DIRECT / SOFTWARE OFF: suppresses the app's vocal return. Enable hardware direct monitoring on the interface itself. JUCE has no universal switch that enables every manufacturer's direct-monitor mixer. This mode avoids doubling a separately enabled hardware return; it does not fabricate one or remove physical converter latency.

`TrackingInsertChain` is separate from the playback tracks' fixed-latency rack. Empty live slots add zero DSP delay. A zero-latency EQ or compressor adds no reserved per-slot delay; a 5 ms lookahead limiter adds its real 5 ms, and a saturation processor adds its reported oversampling delay. Bypass retains that processor's latency and crossfades over 10 ms. Structural insert/remove operations require vocal monitoring OFF/interface-direct and recording idle, but backing playback can continue. Knob parameters and bypass remain live. Editors close before a replaced processor is destroyed outside the callback lock. A higher-priority worker or smaller audio buffer cannot remove algorithmic lookahead or Bluetooth transport latency.

### Master and take isolation

The master sum and master analyser still contain the thirteen background tracks. The vocal return is added to a separate listening copy, then direct/calibrated routing and the mandatory final limiter feed the device. Dry recording taps the microphone, not that listening copy. No calibration is printed into the vocal WAV or existing master stems. Native session presets still serialize the thirteen playback tracks; the temporary vocal rack is captured in take metadata, not automatically recalled as a fourteenth session track.

### Validation

Real JUCE WAV writer tests ran here: mono/stereo float format, exact sample values and frame continuity, background close/finalization, placement JSON, and zero allocations in the instrumented audio push. New source and native pipeline-test code passed JUCE syntax checks. `hem_vocal_pipeline_tests` covers selected-input mapping, interface-direct software muting, empty/real insert latency and neutral insert monitoring; its linked Windows runtime run remains pending. Actual microphone/ASIO/WASAPI/USB operation, simultaneous thirteen-track performance, physical loopback alignment, disk failure stress and Windows UI testing still need verification on the desktop. This is complete source, not a compiled installer or a claim of zero-latency software monitoring.

## Carried forward from version 0.8 — background workstation systems

This release keeps the complete v0.7 host and adds three native systems. `BACKGROUND-SYSTEMS.md` contains their full source in copyable code blocks; `SOURCE-CODE.md` contains the entire application source.

### XML and JSON presets

`Systems/PresetManager.h/.cpp` saves and loads the same validated session model in either `.xml` or `.json`. The host SAVE SESSION / LOAD SESSION picker now accepts both. Specify the extension when saving. Every track path, trim, mute, solo and all 104 ordered insert IDs/parameters/bypass states are preserved. Relative source paths and absolute fallbacks remain intact. Save writes a sibling temporary file before replacing the target. Unknown processors, invalid parameter ranges, missing audio and unsupported schemas reject recall before commit. `list`, `rename` and `remove` are complete backend directory-management APIs; the current host uses the file picker, rather than showing additional preset-browser buttons. Invoke disk APIs from a worker, never the audio callback. SessionWorker performs the integrated save/load work off-thread.

JSON uses `{type, properties, children}` to represent the JUCE ValueTree without losing parameter values or slot ordering. Both formats use the same schema/version/range validation and all-or-nothing bank handoff. WAVs remain external files, not embedded audio. Learned restorer noise profiles and transient DSP histories are not preset parameters and still reset on recall.

### Background waveform and spectrum

`Systems/AnalysisWorker.h/.cpp` is a single-producer/single-consumer analyser. Audio pushes bounded 256-frame stereo packets into a 128-packet AbstractFifo; it never waits for the worker. JUCE FIFO reserves one packet internally. Overflow drops display samples and increments a counter, not audio playback. Device-rate epochs and frame indices identify discontinuities and reset the analysis window. All queue storage is allocated before playback.

The worker computes an 8192-point Hann-windowed stereo FFT with 75% overlap (2048-frame hop). At 96 kHz this yields 11.71875 Hz bin spacing and a nominal 46.875 spectrum updates/second after the first 85.33 ms window. Bins are calibrated to coherent sine peak amplitude in dBFS, not LUFS or a PSD estimate. Left/right transforms are separate so opposite-polarity material does not disappear through a mono sum. Waveform buckets retain L/R minimum and maximum over 64 frames and keep the latest 2048 buckets (1.365 seconds at 96 kHz). This is a live rolling waveform, not an entire-file overview.

The worker publishes a preallocated snapshot. GUI access uses try-lock and skips a busy frame; the audio thread never touches that snapshot mutex. `AnalysisView` renders snapshots at 20 Hz. Open ANALYSIS in the central bay. Data is taken from the pre-monitor master sum; display overload is not the final limited device signal.

### Independent listening matrix and calibration

`DSP/MonitoringRouter.h` separates the immutable master input from the listening output. DIRECT OUTPUT bypasses calibration and manual monitor EQ with a 20 ms crossfade. HEADPHONE CORRECTION enables the independently processed listening branch. LOAD CALIBRATION imports a JSON file through `MonitorProfileLoader` on a background thread; loading does not automatically enable correction. MONITOR EQ opens the existing manual listening EQ and selects the correction route. The mandatory master limiter remains last in both routes. Session recall and device restart select direct mode. Device restart clears imported coefficients; reload the matching profile. Imported calibration is device-local and is not carried into a musical preset or future export.

Up to twelve RBJ bell/low-shelf/high-shelf filters, explicit headroom, and smoothly switched profile banks are supported. The profile loader rejects bands too close to or above the current Nyquist frequency instead of silently changing a measured curve. Shelf Q uses `alpha = sin(w)/(2Q)`. Metadata includes device, connection, operating mode and measurement source; naming a source is provenance supplied by the profile author, not automatic verification of the measurement.

No measured XM5 curve was supplied, so no guessed Sony correction is included. Import coefficients based on your chosen measured response/target, matching Bluetooth versus wired use, ANC/power mode and EQ settings. Model matching is user-selected; this router does not detect headphones, force LDAC, control Bluetooth quality or open multiple hardware endpoints. Select the actual output through AUDIO OUTPUT / the OS. It works with whichever device the selected JUCE driver exposes.

Profile JSON format (this is a valid flat reference, explicitly not a measured calibration):

```json
{
  "version": 1,
  "name": "Flat reference",
  "device": "Any output",
  "connection": "User-selected output",
  "deviceMode": "Uncorrected",
  "measurementSource": "Identity response; not a measured calibration",
  "preampDb": 0,
  "bands": []
}
```

Each measured band has `type` (`bell`, `lowShelf`, `highShelf`), `hz`, `q`, and `db`. Allowed numeric ranges: frequency 10–40000 Hz and below 0.49 times the active sample rate; Q 0.1–10; gain -18 to +12 dB; preamp -36 to 0 dB. Changing profiles during an unfinished crossfade is rejected; retry once it completes.

**Master separation:** sum -> analyser tap; sum -> calibration/manual monitor EQ -> route crossfade -> final limiter -> selected hardware. The original sum is never written by calibration. A future recorder/exporter must read that original sum and apply its own export ceiling, not record the listening buffer. The offline mix exporter and editing timeline remain separate release work; v0.9 adds standalone vocal-take recording described above.

**Validation:** monitoring router tests passed at 44.1/48/96 kHz (direct exact, -9 dB combined test correction, unchanged source); real JUCE FFT worker tests passed (stereo levels/bins, extrema, bounded overflow, device-rate changes, allocation-free audio push); calibration JSON/coefficients/range checks passed. Updated host and preset test sources passed JUCE syntax checks. Full Windows GUI/device testing and the linked all-processor XML/JSON session round-trip test remain pending; do not label the full application production-certified before those tests.

## Carried forward from version 0.7 — sessions and mandatory final limiter

`Source/SessionState.h/.cpp` contains the snapshot model, JUCE ValueTree XML codec and background SessionWorker. `Source/DSP/MasterLimiter.h` wraps the existing mathematical limiter kernel as an always-on final output stage. MainComponent wires both features into the application.

**Save:** use SAVE SESSION. It snapshots all 13 tracks, all 104 ordered insert positions, stable registry IDs, every exposed plugin parameter, slot bypass, track gain/mute/solo, source paths, names, input/output gain, loop setting and playhead. Snapshotting reads parameters on the message thread; XML creation and disk writes run on the worker. Saving waits for any hot-swap to finish rather than recording half a transition. Files are written to a sibling temporary file and then replace the destination using JUCE TemporaryFile. WAV audio is referenced, not embedded. Keep the session and WAV folder together when moving projects; relative paths are tried before saved absolute paths.

**Recall:** pause/stop, Apply or Clear pending stem imports, then LOAD SESSION. A background worker validates XML, source files, processor IDs and parameter ranges, builds a fresh TrackBank, prepares all readers/resamplers and reconstructs every serial chain. Only a complete bank can replace the live one. Commit uses one ownership swap under brief callback exclusion. The old bank is retired off-thread. A missing/corrupt file, unknown plugin, unsupported schema or changed audio-device generation rejects recall without replacing the mixer. Transport remains paused at the saved common position. Plugin editors referring to the old bank close before retirement.

**Output safety:** optional monitor EQ settings are recalled but its enable switch is forced OFF, so an old listening correction cannot unexpectedly affect a different device. Device/driver selection is not serialized. The master ceiling is fixed at -1.0 dBTP and cannot be bypassed by session data or insert bypass. The output telemetry tooltip includes measured peak and reduction. This remains device-neutral.

**State scope:** every exposed setting is stored; DSP delay/envelope histories and the restorer's learned runtime noise profile restart. Individual WAV looping remains per-file, so unequal-length loops are not a shared arrangement loop. Missing files produce a track/path error; restore that path or move the referenced audio folder alongside the session and retry. A graphical relink browser is a future convenience.

**Validation:** the new dependency-free master test ran at 44.1/48/96 kHz with thirteen full-level summed stems and +12 dB downstream boost, independent 8x reconstructed-output scanning, NaN/Inf containment, exact quiet-signal latency and allocation instrumentation. All passed; measured overload peak was -3.45979 dBTP (the conservative bound may attenuate more than conventional limiters). Full host and session-test C++ syntax checks against JUCE passed. `hem_session_tests` covers XML round-trip across 13 x 8 positions, parameter recall, schema/range rejection, unknown plugins, missing files and staged chain installation; its linked Windows run remains pending.

The peak ceiling is defined by the supplied 8x, 32-tap reconstruction filter. It is not certification against every possible reconstruction filter, analog DAC or OS enhancement. Output boosts outside the application remain outside this limiter's control.

## Carried forward from version 0.6 — background WAV drop importer

`StemFileImporter.h/.cpp` implements juce::FileDragAndDropTarget as a dedicated heavy smoked obsidian drop zone in the central bay. Click Browse WAVs or drop from Windows Explorer. Only mono/stereo WAV is accepted. A batch of 1–13 files maps in the order JUCE receives them to tracks 1–N. No name sorting or guessing is applied. The ready status shows the destination range; hover its text to inspect the complete filename-to-track mapping.

**Preparation:** a JUCE worker thread validates WAV headers, builds each reader/transport, configures source-rate correction and pre-fills 65,536-frame read-ahead buffers. Files stream from disk rather than decoding entire songs into RAM. A single batch is prepared at a time. Unsupported, corrupt, empty or >2-channel WAVs fail the whole batch; more than 13 files is rejected rather than truncated. Cancel leaves original tracks untouched. Track slots after N retain their existing sources.

**Apply:** current playback can continue during preparation. When READY, pause or stop and press APPLY. The host validates the captured device generation and loop mode, then swaps all prepared source pointers under brief device-callback exclusion. Original insert chains, gains, mute and solo states remain attached to their tracks. All tracks rewind to zero; Play starts them together. This explicit Apply step prevents an import from unexpectedly replacing audible material. Changed device/loop settings invalidate the staged batch: clear and re-import.

**Cleanup:** replaced transports are transferred back to the batch and destroyed on the worker, never in the audio callback or commit lock. No file-opening, reader construction, resampler preparation or source destruction takes place in that commit lock. Shutdown joins the worker before stopping the shared disk thread.

**Existing picker:** the top File action loads one WAV into the selected track through the same background path; the drop-zone Browse action selects multiple files for tracks 1–N.

**Sample-rate conversion:** AudioTransportSource receives each WAV's source sample rate and is prepared at the actual output-device rate, enabling JUCE ResamplingAudioSource. The requested engine rate is 96 kHz, subject to hardware support. This does not force the hardware rate or rewrite the original files.

**Test status:** importer, host integration and test source passed C++ syntax checks against JUCE headers. The supplied hem_import_tests target covers file count/type rejection, malformed WAVs, background reader construction and 44.1/48/96 kHz to 96 kHz pitch/duration checks. It has NOT been linked/run here. Real Windows Explorer drops, cancellation/device-change races and physical audio streaming still need runtime testing on the PC. This design avoids synchronous import work on the UI/audio threads; it cannot guarantee against device/disk overload or OS scheduling underruns.

`Source/StemPlayback.h` owns the preprepared source; `TrackEngine` now owns it through a stable heap object so batch handoff only swaps unique_ptrs. `SOURCE-CODE.md` contains the complete class and integration code in copyable blocks.

## Changes carried forward from version 0.5

- 13 stereo stem tracks, each retaining its own eight-slot insert chain.
- Device-neutral **AUDIO OUTPUT** selector and actual driver device-name telemetry.
- Optional Monitor EQ, flat and disabled by default. No Sony, soundbar, headphone or LDAC requirement.
- Monitor EQ automatically disables when a device change/reprepare is observed. No old listening adjustment is automatically re-enabled on the new device.
- A PAUSE control that captures the common position, stops the players outside the device lock, then restores every stem to that position. Resume starts the loaded sources on the same callback boundary. The longest loaded source anchors the session clock, so an ended short stem cannot reset the whole timeline.
- Master output/headroom remains an actual smoothed gain control (-60 to +6 dB). It is not automatic normalization or overload prevention.
- A JUCE integration test for 13-stem summing, oversized-block chunking, mute/solo position continuity and common seeking is supplied for Windows execution.

Choose whichever output Windows and the selected JUCE driver expose: computer speakers, studio monitors via an audio interface, headphones, HDMI/USB devices or a connected soundbar. Your Sony WH-1000XM5 and Sony soundbar can each be selected when available to that driver. This app selects one output device at a time; it does not promise simultaneous playback, force a Bluetooth codec, or identify an unconnected device. The 96 kHz request remains subject to the selected driver's supported rates.

Mute/solo/gain are atomic. Muted and solo-excluded sources still render and advance. Mixing storage is allocated during prepareToPlay, and the host chunks larger callbacks. Native JUCE playback/UI tests have not been executed here; compilation checks do not establish physical-device compatibility.

## Processor additions carried forward from version 0.4

- Vocal Presence & Resonance Architect: six tone controls and a dynamic resonance attenuation band.
- Triode Tube Saturator: 4x oversampled asymmetric soft saturation, tone, mix and output.
- Tape Saturator: 4x oversampled symmetric soft saturation, low-frequency bump and high-frequency loss.
- Optional Monitor EQ: an independent listening-only path; flat and disabled at startup. It is not a measured/calibrated headphone profile and does not detect a connected device or change Bluetooth settings.

Use **MONITOR EQ** in the central bay to open the listening EQ. Selecting Monitor EQ from the registry opens that window instead of replacing a track insert. InsertChain also rejects monitor-only processors at the routing layer. The master sum is copied into a separate monitor buffer; only that copy is equalized before playback. A future exporter must use the pre-monitor master path. Export itself remains outstanding.

`PLUGIN-ROADMAP.md` ranks twelve proposed modules; these proposals are not implemented in this package.

## Use the track racks

1. Choose TRACK 1 in the central bay, click File on the faceplate, and load a song or stem.
2. Choose another track and load its file. There are thirteen stereo file tracks; loading replaces only the selected track and stops/rewinds the session.
3. Play starts all loaded transports while the device callback is excluded, so they start on the same callback boundary. Stop rewinds all; rewind and forward seek all. Loop repeats each file at its own length, not at a global song boundary.
4. Each of the thirteen tracks has eight independent serial inserts. Select a slot on the left, choose a registry module, and Insert. Open Plugin launches its own resizable window. Each track has trim, mute and solo.
5. Insert Rack opens a separate management window. Select the track, select a slot with OPEN (empty slots select without launching a processor), then insert/replace. BYP, REMOVE, UP and DOWN are per-slot controls.
6. UP/DOWN ordering requires stopped playback and idle insert transitions. Processor identity and parameters move with the slot. Bypass and replacement use the original aligned fades during playback.
7. Studio opens device settings. The app requests 96 kHz and 256 frames; it displays the actual negotiated device format. Processing is float PCM. Hardware/driver support determines the physical output format; the app does not claim or force LDAC connectivity.

## Registry and processor modules

`Source/Plugins/PluginRegistry.h` is the centralized native registry. Stable IDs are separate from UI menu indices. The previous createModule(int) now delegates to this registry. Factories and UI changes run on the message thread. No registry mutation runs inside the callback.

| ID | Processor / editor | Processing |
|---|---|---|
| sipa | SipaProcessor / SipaEditor | Mid/side width, optional side HPF for mono bass, L/R balance, mono, polarity, swap, correlation |
| surgical-eq | SurgicalEqProcessor / SurgicalEqEditor | Optional cascaded HPF, three parametric bells, air shelf |
| vca | VcaProcessor / VcaEditor | Stereo-linked compression, sidechain HPF, soft knee, timing, makeup and parallel mix |
| true-peak | TruePeakProcessor / TruePeakEditor | 5 ms conservative lookahead limiter, defined 8x FIR peak bound |
| dhta | DhtaProcessor / DhtaEditor | Band-focused de-essing/dynamic attenuation, linked detector, wide mode, delta audition |
| transient | TransientProcessor / TransientEditor | Fast/slow envelope contrast attack and decay shaping |
| sub-bass | SubBassProcessor / SubBassEditor | Low-band level, low-band saturation, mono blend, optional sub-cut |
| spectral-restorer | RestorerProcessor / RestorerEditor | Eight-band downward expansion with explicit noise-only learning and transient preservation |
| vocal-presence | VocalProcessor / VocalEditor | HPF, five tone bands, dynamic resonance attenuation |
| triode-tube | TubeProcessor / TubeEditor | 4x oversampled biased tanh saturation, DC removal, tone and mix |
| tape | TapeProcessor / TapeEditor | 4x oversampled symmetric tanh saturation, low bump and HF loss |
| monitor-eq | MonitorProcessor / MonitorEditor | User-adjustable monitor EQ; prohibited in track inserts |

S.I.P.A. is reused rather than duplicated. Editors are concrete classes sharing ModuleEditor's parameter-linked construction. APVTS attachments connect rotary/toggle controls; cached atomic raw parameter pointers feed the callback; audio-thread-owned 10 ms ramps smooth changes. State serialization is implemented for every processor. The host exposes SAVE SESSION and LOAD SESSION for the complete 13-track configuration.

### New processor controls

Vocal: optional 20–300 Hz high-pass (80 Hz, disabled), Body at 150 Hz/Q0.7, Mud at 350 Hz/Q1.2, Presence at 2.5 kHz/Q0.8, Brightness at 5 kHz/Q0.8, and Air shelf at 12 kHz. All gains are -12 to +12 dB and start at 0 dB. Dynamic resonance: 1–10 kHz (2.8 kHz), Q0.5–8 (2), threshold -60–0 dB (0), maximum cut 0–18 dB (0), attack 0.1–30 ms (5), release 10–500 ms (120), output -24–6 dB (0). A 0 dB maximum cut is neutral; there is no automatic vocal analysis/preset.

Tube: drive 0–24 dB (0), asymmetry -50–50% (0), 6 kHz tone shelf -6–6 dB (0), mix 0–100% (100), output -24–6 dB (0). Tape: same drive/mix/output, 80 Hz low bump -6–6 dB (0), 10 kHz high shelf -12–0 dB (0). Both use 65-tap FIR interpolation/decimation at 4x and report exactly 16 base-rate samples of latency. Neutral output is the precisely delayed dry signal. At active drive, filters reduce aliasing but do not make arbitrary nonlinear input alias-free. Tube and Tape are inspired transfer functions, not measured models of named physical machines; Tape does not implement magnetic hysteresis, wow/flutter or tape hiss.

Optional Monitor EQ: 80 Hz/Q0.7, 250 Hz/Q1, 3 kHz/Q1, and 10 kHz shelf, each -12–6 dB (0). Monitor headroom -24–0 dB (0); enable OFF. Positive boosts may require negative headroom. There is no built-in device-specific calibration curve; these are manually adjustable monitoring bands. All settings are neutral until the user changes and enables them.

### Neutral startup and exact controls

D.H.T.A.: frequency 1–16 kHz (initial 6.8 kHz), Q 0.5–8 (2), threshold -60–0 dB (0), maximum cut 0–24 dB (0), attack 0.1–20 ms (2), release 10–500 ms (80), mix 0–100% (100), output -24–6 dB (0). Wide and delta are off. Reduction is a fixed-frequency dynamic band/notch, not an automatic six-node resonance detector. Delta emits the actual removed component.

Transient: attack and sustain amounts -12–12 dB (both 0), attack detection 0.1–20 ms (2), recovery 20–500 ms (100), mix 100%, output 0 dB. It shapes onset and decay envelope contrast; it is not merely a master gain control. Sustained steady tones converge toward no envelope contrast.

808: low crossover 40–250 Hz (100), bass level -12–12 dB (0), sub-cut 15–50 Hz (25, disabled), low saturation drive 0–12 dB (0), low mono 0–100% (0), mix 100%, output 0 dB. The low-pass and subtractive residual reconstruct unity when unchanged. Mono blending is frequency-dependent; it is not a brickwall crossover. No subharmonic generator is claimed. Nonlinear low-band processing is not oversampled, so aggressive settings can alias.

Restorer: maximum reduction 0–18 dB (0), noise floor -90–-20 dBFS (-60), release 20–500 ms (100), transient preserve 100%, mix 100%, output 0 dB. Band boundaries are 125, 250, 500, 1000, 2000, 4000 and 8000 Hz. For learning, play a noise-only passage and enable LEARN NOISE ONLY, then disable it before program audio. Learning uses a smoothed absolute-amplitude estimate; the learned profile is runtime state, not persisted by APVTS. Reloading/repreparing resets it. This is deterministic multiband DSP, not a trained AI model or a general-purpose artifact classifier.

EQ, stereo, compressor and specialty processors start neutral. The limiter starts at -1 dBTP. All controls expose their actual units and measured meters; there are no simulated activity meters.

## Audio-thread ownership and latency

Each track has its own reader/transport, preallocated stereo scratch buffer and InsertChain. Tracks sum into a preallocated stereo master buffer. The global input is before each track chain; track trim/mute/solo are after its chain; global output is after summing. A copy then passes through optional monitor EQ, the mandatory final MasterLimiter, and an unchanged copy to the audio device. The pre-monitoring sum remains separate for future export integration.

The mandatory MasterLimiter follows the entire sum, output gain and monitor EQ, so monitor boosts cannot exceed its defined -1.0 dBTP ceiling. Its fixed 5 ms lookahead and conservative 8x reconstruction budget protect the emitted samples. No gain or DSP follows it. Non-finite upstream samples are replaced with silence and counted. Extremely large finite inputs are bounded before internal float calculations. This is limiting, not loudness normalization.

Each insert reserves 6 ms of compensated latency, even empty/bypassed slots: approximately 48 ms per parallel track, not 48 ms multiplied by track count. The mandatory master limiter adds 5 ms, giving approximately 53 ms plus device buffering. Replacement warms the new node then crossfades over 20 ms; bypass ramps over 10 ms. The chain callback does not allocate or destroy DSP nodes. Ownership publication and collection use brief device callback exclusion. JUCE transport uses its own synchronization, so the full host is not claimed lock-free.

Dedicated editor windows are closed before retired processors are destroyed on the message thread. Rack reordering swaps processor ownership while playback is stopped and callback access is excluded. The new node's state survives device-rate reprepare through APVTS; audio histories reset.

The limiter ceiling guarantee is specifically for its finite 8x, 32-tap windowed-sinc reconstruction model. The conservative triangle bound may reduce more than a conventional oversampled limiter; it is not ITU certification or a guarantee for every physical DAC.

## Verification and release status

Executed in the authoring environment:
- Numerical specialty tests at 44.1, 48 and 96 kHz: unity startup, 6 dB D.H.T.A. centre attenuation, delta+processed reconstruction, bass gain, low-side suppression, attack enhancement, low-level restoration and finite automation.
- No allocations during instrumented specialty DSP process calls.
- Existing numerical core suite for stereo/EQ/compressor/limiter.
- Finishing numerical tests at 44.1/48/96 kHz: Vocal Body +6 dB measured +6 dB; dynamic resonance at a 6 dB range measured approximately -5.995 dB; tube generated measured even/odd harmonics and tape generated odd harmonics; monitor body -6 dB measured -6 dB. Neutral/dry, disabled-monitor, 16-sample saturation delay and finite-automation tests passed with zero allocations in the instrumented DSP calls.
- C++ syntax checks against JUCE headers for host, processors, editors and native chain and 13-stem tests.

The linked Visual Studio application and native JUCE chain tests still require execution on Windows. The full thirteen-track transport timing, disk read-ahead performance, maximum processor load, UI mapping and device changes are not runtime-verified here. CTest includes the native routing test for the original eight registry modules plus the three new track processors and the monitor-only rejection rule, state round-trip, reorder identity, bypass, swap, retirement and rate restart. Complete those checks and listen at safe monitoring levels before release distribution.

This update provides track playback, insert processing and separate live vocal takes. Timeline editing, offline mix export and external VST scanning remain outside this build. Existing source versions remain separate; use this complete folder for the new build rather than mixing source versions.

## 0.9.1 — visible monitoring guidance

The vocal tracking window now has a persistent cyan-framed amber notice above the input and monitoring controls. It follows the actual monitoring mode, including resets after device changes. Interface Direct explicitly instructs users to enable Direct Monitor on their interface; software monitoring remains off by default. Software modes explain buffer/plugin latency and avoiding doubled returns. No recording or audio-routing behavior changed. JUCE source syntax check passed; Windows visual/device acceptance is still pending.

## 0.10.0 — driver-reported hardware and channel routing

Open AUDIO DEVICE / I/O. Select the available platform driver, input and output device, active hardware channels, sample rate and buffer size. RESCAN refreshes the driver device lists. Device enumeration/hot-plug notifications use JUCE's native selector; the selector scrolls internally on large interfaces. USB is a connection type, not a separate driver backend. Windows builds default to WASAPI. Optional ASIO requires HEM_ENABLE_ASIO=ON plus HEM_ASIO_SDK_PATH pointing to a separately supplied valid Steinberg SDK; missing headers fail configuration. macOS uses CoreAudio. Devices must have a compatible installed driver and OS microphone permissions. No brand/device names are hardcoded.

Startup opens playback with microphone inputs disabled; enable the desired inputs in device settings. 96 kHz and 256 frames are requested only when the current device advertises them. Otherwise its working settings are retained. Close settings by selecting a host insert slot before starting playback or vocal tracking. Changing hardware while playback/recording runs is blocked by the UI; unplugging hardware still forces the driver's reset behavior and stops the vocal take.

The vocal input menu follows enabled physical channel names and their one-based hardware channel numbers. Capture uses JUCE's compact active-channel ordering and supports up to 128 enabled inputs (AudioSourcePlayer limit), removing the previous two-input cap. Device reinitialization selects the first enabled input and switches software monitoring off. Check the visible input selection after changing devices. Only the chosen microphone is recorded; this is not 128-channel simultaneous recording.

Output supports one selected mono channel or two selected stereo channels, including non-default physical outputs. Mono folds L/R at 0.5 each before the mandatory final limiter. The mixer itself remains stereo. Separate input/output devices are offered only where the chosen backend supports them; this is not arbitrary cross-driver aggregation.

Validation: changed host, panel, capture and test translation units passed JUCE 8 C++ syntax checks. Added DeviceInputTests for input 16, switching to input 5, missing-input silence, monitoring reset and mono bounds. Its local runtime link was blocked by unavailable JUCE GUI/processor link objects; it is included as a CTest target for the desktop build. Windows ASIO/WASAPI and macOS CoreAudio hardware, hot-plug and GUI runtime acceptance remain pending. This source update does not establish universal public-release compatibility.
