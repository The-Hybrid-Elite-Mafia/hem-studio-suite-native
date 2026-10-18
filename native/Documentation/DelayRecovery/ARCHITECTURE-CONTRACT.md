# Multitap Delay and Tape Delay: pre-implementation return

SUPERSEDED FOR REVIEW: use REVIEW-1-REFERENCE-PARAMETERS.md, REVIEW-2-DSP-MATH-TAIL.md and REVIEW-3-HOST-STATE-VALIDATION.md. Revision 2 adopts the parent-authorized real host BPM control with explicit 120 BPM legacy default, acknowledges task-3's 3600-second tail UI, and specifies a finite 2400-second module tail. This original document is retained only as history.

Status: reference inspection and isolated source preparation complete; implementation, compilation and execution have NOT begun. This is the requested architecture/contract return before implementation, not an acceptance claim. Parent owns host integration decisions, later merge and acceptance.

## Evidence and preserved baseline

- Baseline: `%USERPROFILE%\Documents\Codex\2026-10-03\task-2\source` (the requested foundation baseline, not source-v2 or source-faithful).
- New isolated copy: `%USERPROFILE%\Documents\Codex\2026-10-04\task-2\delay-source`.
- Excluded build/build-* and .git directories and obj/pdb/exe/dll/ilk files. No baseline edits or install changes. COPY-MANIFEST.json compares every copied file with its source by SHA-256; STATUS.json records the result.
- Examined both actual PNGs at 1672 x 941 and the corresponding full prompt paragraphs in `2026-10-03\task\artwork-reference-2026-10-04\01-ALL-GENERATION-PROMPTS.txt`. Exact copies of the PNGs accompany this contract.
- Multitap PNG SHA-256: EAA249800420DC851CF3F66D3DAE1AD23EC623E6AC55C067C40F7384B8A0E9C4.
- Tape PNG SHA-256: 7446919D39D3D961E63AE8EE6101FCCD86D93CD38BCBD6EBB182E7D1B279CEB2.
- No applicable AGENTS.md found in baseline/workspace or ancestor searches. No listed skill is needed for reading artwork and designing C++ DSP; no image generation/editing was performed.
- Bundled dependencies/JUCE/CMakeLists.txt declares JUCE 8.0.10. native/CMakeLists.txt requires C++20 and CMake >=3.22.

## Original-reference facts (not inferred ranges)

Multitap has exactly eight tap columns. Each has TIME (-- ms), FEEDBACK (0%), PAN (0), LEVEL (0.0 dB), ON, SOLO and MUTE. That is 32 per-tap rotaries and 24 per-tap buttons. SELECTED TAP FILTERS has LOW CUT (-- Hz), HIGH CUT (-- Hz), and a separate ENABLE button for each. The shared editor edits the selected tap; filters are therefore per-tap state, not two global filters. Global controls are MIX (0%), OUTPUT (0.0 dB), TEMPO SYNC and BYPASS, plus a tempo readout (-- BPM). TAP TIMELINE has an inactive grid and time-ms label. The actual image additionally prints LEVEL (dB) and tiny generated numeric markings; these are decorative source pixels, not calibrated DSP evidence.

Tape has exactly nine rotaries: DELAY TIME (-- ms), FEEDBACK (0%), WOW (0%), FLUTTER (0%), DRIVE (0.0 dB), LOW CUT (-- Hz), HIGH CUT (-- Hz), MIX (0%), OUTPUT (0.0 dB). Buttons: TEMPO SYNC, TAP TEMPO, LOW CUT ON, HIGH CUT ON and BYPASS. Tempo readout is -- BPM. TAPE ECHO is an empty grid with time-ms axis. No reel animation or wow/flutter meters are specified.

Both have blank INPUT/OUTPUT dBFS apertures. All functional buttons are dark. The neutral artwork pointers point down; this is explicitly an art pose, not proof that a physical parameter is at its minimum. Branding, illumination and reflections must remain fixed. The prompts specify no delay range, feedback maximum, sync divisions, wow/flutter rates/depth scaling, filter order, default configured time, pan law, or tape model. All such choices below are proposed engineering contracts and must not be described as recovered original specifications.

## Genuine integration dependency: tempo

The baseline's entire native/Source tree contains no whole-word bpm/tempo or getPlayHead/setPlayHead implementation. InsertChain::run supplies only an AudioBuffer and empty MIDI buffer to processBlock. SessionSnapshot stores positionSeconds and looping but no tempo, tempo map or beat phase. A native processor cannot obtain real host BPM from this baseline. Inventing 120 BPM behind an enabled TEMPO SYNC button would misrepresent the control.

Recommended shared host contract, to be coordinated with parent before implementing interfaces:

1. A value-type transport context supplied by the host on the audio thread: validTempo, finite bpm, absolute sample position, playing, and a discontinuity generation. No clock reads or locks in DSP. Tempo events within a block carry sample offsets and cause bounded sub-block processing. Constant-tempo sessions may initially publish one value per block; this is not sample-accurate tempo-map automation.
2. Persist project tempo and validity in SessionSnapshot/SessionState. Legacy sessions have invalid/unconfigured tempo, not fabricated BPM. A parent-owned host tempo editor establishes BPM. The offline renderer uses the same immutable tempo snapshot and sample-position progression. Neither processor independently decides the session tempo.
3. Sync requests without valid host tempo remain visibly unavailable, retain the request in state, display -- BPM and explain why. Free-time processing continues using the stored manual delay. Offline rendering of a saved requested-sync state without valid tempo must fail with an actionable message rather than silently render at a guessed BPM.
4. Recommended division choice lives on the EXISTING TIME control when sync is active, avoiding an invented faceplate control. Persist free milliseconds and beat division separately. Proposed choices: 1/64, 1/32, 1/16, 1/8, 1/4, 1/2 and whole note, with straight/dotted/triplet variants; readout shows division and tooltip shows effective ms. A division exceeding prepared storage is explicitly unavailable, not silently clamped.
5. Tape TAP TEMPO uses a message-thread monotonic clock, validates successive intervals and writes the manual delay through APVTS. Proposed accepted interval range 100..4000 ms, reset after 4 seconds or a reversed timestamp. First tap arms; subsequent valid intervals establish one quarter-note interval. In host-sync mode the button is disabled with explanation; it must not change global tempo without a host command contract. No audio-thread GUI-clock access.
6. Delay recurrence uses sample counts, not transport phase. Start/stop/seek do not quantize or restart repeats. Explicit reset/reprepare flushes buffers. This policy avoids making a simple delay depend on beat-phase alignment while preserving a future tempo-map path.

Host timing ownership is the principal blocker to a complete truthful implementation. The missing path is implementable, but crosses shared host/session/export code owned by concurrent tasks. Parent should select or supply the shared context rather than allowing multiple module batches to invent incompatible APIs.

## Proposed module architecture and parameter contract

Separate dependency-free `DSP/DelayCore.h`, JUCE `Plugins/DelayProcessors.{h,cpp}`, original-faceplate `Plugins/DelayEditors.h`, and dedicated numerical/integration tests. Append stable registry IDs `multitap-delay` and `tape-delay`, APVTS root types `MULTITAP_DELAY` and `TAPE_DELAY`. Preserve all existing 17 IDs, types and menu order. The baseline does not already contain all 31 modules; parent merges other isolated module batches.

Proposed free delay range 1..4000 ms, continuous fractional samples, default 250 ms for Tape and 250*(tap index+1) ms for Multitap. The dormant readout can show -- ms while a tap is off, but enabling it exposes a valid stored value. These time defaults are proposals; only mix/feedback/pan/level/output neutral values come from the original reference.

| Control | Proposed stored range/default | Semantics |
|---|---|---|
| Feedback | 0..95%, default 0 | Independent recirculation per tap; no cross-feedback or hidden summed feedback |
| Pan | -100..100, default 0 | Stereo balance, unity both sides at center; attenuate opposite side toward endpoints |
| Level | -60..+12 dB, default 0 | Per-tap output gain; outside feedback loop |
| Tap ON/SOLO/MUTE | Boolean, all default false | ON gates injection and audibility. MUTE wins over SOLO; solo selection only among ON, unmuted taps. Controls fade; no discontinuous buffer clearing in callback |
| Low/high cut | 20..20000 Hz, defaults 20/20000 | Independent enables default false; effective frequency <=0.45*sample rate; selected tap editor binds only its own state |
| Mix | 0..100%, default 0 | Linear dry/wet blend; neutral must null against dry after declared algorithmic latency |
| Output | -24..+12 dB, default 0 | Final gain; independent of feedback |
| Wow/flutter | 0..100%, both default 0 | Proposed deterministic sine modulation at 0.5/6 Hz, maximum +/-5/+/-0.5 ms; publish these choices in help |
| Drive | 0..24 dB, default 0 | Zero gives transparent delayed wet audio, not default saturation |
| Sync/bypass | Boolean, default false | Sync behavior requires host contract above; bypass fades to aligned dry |

No hidden normalization of eight unity taps: coherent wet outputs can sum above 0 dBFS and meters must report this honestly. Safety bounds handle pathological values without a hidden ordinary-signal limiter. Maximum wet gain is included in the tail bound. Every tap stores its own filter settings even though only one editor pair is shown. Selected tap is UI state and must not retarget an audio parameter based on transient component focus.

## DSP, allocation and latency

- Validate finite 8..384 kHz preparation rate. Allocate all stereo circular buffers only in prepare, including any scratch buffers, filter state, fade heads and dry compensation. Do not resize/free/reset large buffers from process. At 384 kHz, eight 4-second stereo float rings alone require about 98.3 MB per Multitap instance; preparation must surface allocation failure and the host must avoid uncontrolled instance proliferation.
- One independent stereo ring per Multitap tap, one stereo ring for Tape. Read-before-write convention and explicit unit tests establish that an integer N-sample setting emits its first impulse at exactly N, including across arbitrary block boundaries.
- Linear fractional interpolation is bounded and has no overshoot; document its high-frequency attenuation. Smooth gain, feedback, enable, mute, solo and mix over 20 ms. Multitap time changes use bounded dual read heads and a 20 ms crossfade; coalesce further targets rather than allocating additional heads. Tape changes slew the read position for an audible tape-speed transition; bound maximum read delay including modulation and never index outside storage.
- Feedback uses sanitized input and convex interpolated delayed samples with coefficient <=0.95. Keep filters, output gain, pan and initial tape coloration OUTSIDE this recurrence to retain a simple contraction proof. This is an explicitly simplified tape-echo model, not a physical tape-machine emulation. Repeated nonlinear coloration inside the feedback loop would require a separate stability/tail proof.
- Reuse the existing FinishingCore OversampledSaturator only as a wet-output stage. Its documented FIR group delay is 16 base-rate samples. Delay the dry path by exactly 16 for Tape, report 16 intrinsic samples, and place wet coloring after the echo ring so feedback periods do not acquire a hidden 16-sample extension. Drive zero still uses its aligned dry path. Multitap reports zero intrinsic latency. Intentional echo time is never reported as host compensation latency.
- Preserve host slot compensation: InsertChain already budgets 6 ms per slot and compensates reported processor latency. No new latency changes while processing. The 16-sample Tape latency fits even the 8 kHz preparation floor.
- Sanitize nonfinite parameters and input samples before indexing or writing history. Use double accumulation for tap sums, bounded finite ring writes, explicit tiny-state zeroing, and ScopedNoDenormals. Recover an affected channel's finite filter/interpolator states without locks, allocation or whole-ring clearing. Document the chosen overflow ceiling; test recovery under NaN, infinity and huge finite input.
- Meters use actual input and output samples with lock-free telemetry and prepared/processed/valid flags. No decorative animation is data. Audio callback performs no locks, I/O, XML, state copies, allocation, logging or object destruction.

## State/reprepare contract

Persist every numeric and toggle control plus separate manual-time/division state. Version new module state; stable IDs never depend on array indices. Validate type, completeness, duplicates, unknown fields, finite/range values and exact Boolean domains before atomic off-thread commit. A rejected state leaves previous controls unchanged. Do not serialize delay history, tap timestamp history, meter values or LFO transient state. Preparation resets history, phases, smoothing and meter validity deterministically from restored parameter values; immediate reprepare matches a fresh instance with the same state. State restoration follows existing staged session ownership, never an audio callback lock taken from inside DSP.

## Tail and export contract

Feedback recurrence has a provable finite decay bound to a stated threshold, not a genuinely finite mathematical impulse response. Define a conservative -180 dBFS residual target relative to a documented maximum history amplitude. With feedback f<=0.95 and maximum effective delay D, use complete D-sized history windows and k=ceil(log(epsilon/A)/log(f)); reserve at least (k+1)*D, plus filter decay, transition time and FIR support. f=0 still reserves the longest stored echo time. Include worst-case eight-tap summation, tap/output gain, filter transient bounds and modulation headroom in A/D. A fixed safety clamp alone is not a proof of this bound; numerical tests must corroborate the analytical calculation.

Prefer a conservative maximum-per-module reported bound that remains valid for automation and history generated under earlier settings. Computing tail only from current feedback/mix/on values can under-report buffered older audio. Native bypass does not necessarily erase history. For export from a freshly staged snapshot, a tighter snapshot-derived bound is possible only when its assumptions are explicit.

Baseline OfflineMixRenderer::requiredFoundationTail hardcodes only dc-subsonic/plosive-control/hum-buzz; it never asks the new modules for their bounds. OfflineMixPanel exposes 0..120 seconds while worker/renderer accept <=3600. Extend preflight to both new stable IDs and sum conservative serial tails for each audible track, then take the maximum. Warn/reject too-short requested tails unless the EXISTING allowTailTruncation option is explicitly enabled. Expand the UI's selectable range consistently; if a chain's required bound exceeds the supported limit, report that fact and require explicit truncation rather than silently cap a reported tail. Include the shared tempo snapshot in export. This is the second mandatory shared-code integration change.

## Original artwork interaction contract

Use bundled original PNG pixels as fixed background. Build per-control hit geometry and independent APVTS bindings for every knob/button. Cover source value windows with live text; never rotate whole knob cutouts because that rotates the baked lighting. Preserve stationary knob shading and draw only the moving pointer, removing its baked neutral pointer with a local deterministic pixel treatment subject to visual review. This can be implemented through code-native masking/rendering without a generated replacement image. Filters rebind on selected tap; tap header/column selection needs a clear highlight and keyboard access, without adding a ninth tap. Show only truthful computed tap positions or captured sample data on graphs. Input/output meter apertures receive real scale and sample telemetry. Font/asset licensing stays within existing resources.

## Validation and build gate

Required numerical tests: exact integer/fractional impulse timing, variable block partition equivalence, independent tap feedback and filters, pan/solo/mute precedence, dry neutrality at declared latency, smooth time transitions, tempo loss/change/division handling, tap-tempo input validation, oversampling alignment, filter-disabled null, bounded worst feedback, long-tail decay, denormal and overflow recovery, reproducible reprepare, and process allocation instrumentation. Integration tests cover strict state roundtrips/rejection, unchanged legacy registry IDs, session tempo/live-offline equivalence, real meter gating and tail preflight/truncation behavior. Validate visual hit regions and stationary lighting after editor implementation.

Compile focused core/module targets only, using existing MSVC2019, /MT, C++20, bundled JUCE8.0.10, CMAKE_OBJECT_PATH_MAX=200 and concurrency 1. No network fetches/new dependencies. Coordinate with parent before an all-target build. Do not invoke CTest or run newly built executables until specific user approval; successful compilation cannot be reported as runtime verification. No build was performed in this contract stage.
