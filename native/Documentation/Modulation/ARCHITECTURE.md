# Catalog 23 / 24 native modulation contract

This is a new implementation in the continuing source tree. No historical implementation of these six effects was found in the recovered native baseline. It does not change the frozen twenty-processor checkpoint, existing effects, master limiter or eight-slot host latency contract.

## Real-time architecture

- Preparation accepts 8–192 kHz and allocates fixed-capacity delay lines. The callback performs bounded sample loops over two channels and fixed filter/tap counts; no allocation, deletion, locks, GUI access, file I/O, worker notification or container growth
- Engine latency is zero samples. Modulated delays are intentional effect time, not lookahead or inserted pipeline latency. The host retains its existing 6 ms compensation budget per slot
- APVTS atomics are sampled once per callback. Numeric controls, wet/bypass gain, effect selection, LFO waveform and phaser stage taps use 20 ms sample-count ramps. Callback boundaries do not restart an unchanged target ramp
- Both processing cores accept mono (`right == nullptr`) and independent stereo. The native adapters retain the existing stereo host bus contract. Reset clears delay/filter history, phase, telemetry and smoothers; the next block starts at its supplied parameter settings
- Feedback is clamped to ±0.85. Internal delay/filter/feedback states have a ±32 safety bound. Processed output is finite and bounded at ±32; finite bypass/neutral audio is returned unchanged. Nonfinite input becomes zero. This guard is not a replacement master limiter and does not change the existing limiter

## Catalog 23: Chorus / Flanger / Phaser

`ChorusFlangerPhaserProcessor`, registry `chorus-flanger-phaser`, state `CHORUS_FLANGER_PHASER`.

Chorus uses two linearly interpolated delay reads per channel (second base time 1.2×, LFO phase offset 0.33 cycles). Flanger uses one interpolated tap. Both use the DELAY control as base time and DEPTH as ±94% of that base; a one-sample minimum read delay prevents causal underflow. At large base times the flanger remains a valid comb effect, although classic short flanging requires reducing DELAY. L/R modulation has a fixed quarter-cycle offset.

Phaser uses a cascade of up to eight stable first-order allpass sections. The bilinear coefficient is `(tan(pi*f/fs)-1)/(tan(pi*f/fs)+1)`. The frequency sweeps ±2 octaves at full depth, bounded to 20 Hz–0.45×sample rate. The 2/4/6/8-stage output taps crossfade when selected. Feedback includes one previous-sample output to avoid an algebraic loop. At MIX 100% and zero feedback the static allpass chain has unity magnitude; the familiar phasing notches result when mixed with dry audio.

| Parameter | Range | Default | Meaning |
|---|---:|---:|---|
| depth | 0–100% | 0 | Delay/frequency modulation depth |
| rate | 0.02–10 Hz | 0.5 | LFO rate; cycles/quarter-note with valid sync |
| feedback | −85–85% | 0 | Signed recirculation |
| mix | 0–100% | 0 | Linear dry/wet |
| output | −24–6 dB | 0 | Post-mix output trim, excluded by bypass |
| delay | 0.1–40 ms | 12 | Chorus/flanger base delay |
| center | 20–8000 Hz | 1000 | Phaser centre before Nyquist bound |
| mode | Chorus / Flanger / Phaser | Chorus | Crossfaded engine selection |
| shape | Sine / Triangle / Soft square | Sine | Crossfaded oscillator shape |
| stages | 2 / 4 / 6 / 8 | 4 | Phaser stage count |
| sync | off/on | off | Optional live-host tempo-rate sync |
| bypass | off/on | off | Ramped dry bypass |

## Catalog 24: Rotary / Tremolo / Autopan

`RotaryTremoloAutopanProcessor`, registry `rotary-tremolo-autopan`, state `ROTARY_TREMOLO_AUTOPAN`.

Rotary is explicitly a dual-band rotating-source approximation, not a measured cabinet model, IR, or named commercial speaker emulation. An 800 Hz one-pole crossover separates drum/horn bands. Distinct modulated delay and amplitude paths approximate Doppler and radiation effects. The horn runs at SPEED in FAST, 15% in SLOW and decelerates to zero in BRAKE; the drum rate is 82% of the horn target. RAMP is the exponential horn velocity time constant; drum inertia uses 1.35×. One time constant covers about 63% of a speed change, rather than an abrupt end time.

Tremolo applies gain `1 - depth*(1-LFO)/2`; width controls L/R phase offset from zero to half a cycle. Autopan uses channel gains `sqrt(1-position*width)` and `sqrt(1+position*width)`, preserving summed squared gain for identical input and unity gain at centre. Existing stereo input stays independent; this is gain motion rather than cross-channel panning. Mono processing uses the left motion law. The oscillator waveform choices are the same as catalog 23. Depth zero is neutral for all three modes.

| Parameter | Range | Default | Meaning |
|---|---:|---:|---|
| speed | 0.02–10 Hz | 4 | LFO/fast horn rate; cycles/quarter-note with valid sync |
| depth | 0–100% | 0 | Modulation amount |
| width | 0–100% | 100 | Stereo phase/pan excursion |
| mix | 0–100% | 0 | Linear dry/wet |
| output | −24–6 dB | 0 | Post-mix trim, excluded by bypass |
| ramp | 0.05–10 s | 1 | Horn speed time constant |
| mode | Rotary / Tremolo / Autopan | Rotary | Crossfaded engine selection |
| rotary | Slow / Fast / Brake | Slow | Rotor speed target |
| shape | Sine / Triangle / Soft square | Sine | Crossfaded oscillator shape |
| sync | off/on | off | Optional live-host tempo-rate sync |
| bypass | off/on | off | Ramped dry bypass |

## Tempo and display truthfulness

Both adapters read only a supplied JUCE AudioPlayHead PositionInfo BPM, rejecting absent, nonfinite or out-of-contract (outside 20–400) BPM. Valid sync interprets RATE/SPEED as cycles per quarter-note: Hz = value × BPM / 60. The waveform remains free-running; this is tempo-rate sync, not bar/beat phase locking. Reset starts phase zero. Transport playing/stopped flags do not fabricate beat position or stop a continuing audio callback.

The current native host does not supply an authoritative playhead/BPM. Therefore its TEMPO field shows `-- BPM`, or `NO HOST BPM` when sync is requested, and the real free-Hz rate continues. A supplied real playhead activates the sync calculation and updates the tempo and rate units (`c/b` means cycles per quarter-note beat; the tooltip expands it). No hidden/manual/fixed 120 BPM is supplied.

The 800-point history records the actual modulation values used by the DSP on the audio clock at approximately 200 Hz. Cyan is left and orange right; rotary displays horn modulation. This is oscillator telemetry, not an input waveform or a predicted display. Idle artwork traces and lit meters are covered. Meters use the existing shared measured stereo input/output sample-peak telemetry, −60 to 0 dBFS display range. There is no background telemetry worker.

The UI uses the approved 1672×941 chassis, shared immutable material dial sprites and shared segmented meter painter. Only control bodies, numeric fields, measured apertures and selection indicators change. Mode-specific controls disable when not applicable. Parameter IDs, defaults, integer choices and values persist through APVTS and strict session migration.

## Primary technical references

- Julius O. Smith, *Physical Audio Signal Processing*, [Flanging](https://www.dsprelated.com/freebooks/pasp/Flanging.html) and [Phasing with First-Order Allpass Filters](https://www.dsprelated.com/freebooks/pasp/Phasing_First_Order_Allpass_Filters.html). The first-order allpass structure and bilinear coefficient are derived from this author-published technical text; this is original implementation code, not copied third-party source
- JUCE official [AudioPlayHead::PositionInfo](https://docs.juce.com/master/classjuce_1_1AudioPlayHead_1_1PositionInfo.html): optional host-provided BPM; missing timing must be handled defensively

## Validation and remaining acceptance

`Tests/ModulationTests.cpp`: standalone numerical reference/behaviour checks, mono/stereo, seven sample rates, all modes and shapes, depth/phase/rate, tempo/no-tempo, exact bypass, allpass magnitude, base delay, rotor inertia/brake, equal-power pan, finite feedback extremes, reset, block-partition equivalence and callback allocation probe.

`Tests/ModulationPluginBindingTests.cpp`: JUCE lifecycle, actual host/no-host BPM, all 23 visible APVTS control bindings, state recall/rejection, shared telemetry and unchanged real insert-chain compensation. `Tests/ModulationPluginEditorTests.cpp`: approved-image embedding, immutable dial rings, no fake idle traces/meters and 16 native rasters across sizes/modes/tempo states.

Cloud Linux verification does not establish Windows compilation, sound-device performance, full 13-stem/8-insert real-time capacity, listening quality, or production audio-thread scheduling headroom. Those remain explicit platform/hardware acceptance work.
