# Reverb 19/20 integration contract

Recovery note: this document is reconstructed from the prior recorded implementation and handoff. All seven implementation files and five test files are separately verified byte-exact against their frozen hashes in RECOVERY-MANIFEST.json. No original comparison hash was available for this document. Original-runtime tests are historical unless a fresh recovery test log is explicitly identified.

## Classes, IDs and latency

- Catalog 19: ConvolutionReverbProcessor, state ID CONVOLUTION_REVERB, registry ID convolution-reverb
- Catalog 20: AlgorithmicReverbProcessor, state ID ALGORITHMIC_REVERB, registry ID algorithmic-reverb
- Both inherit ModuleProcessor and expose stereo JUCE buses
- Convolution reports ceil(sampleRate * 0.004) samples and delays dry by exactly that amount. Algorithmic reverb reports zero samples
- The existing ceiling ceil(sampleRate * 0.006) per insert and eight fixed compensated slots must remain unchanged
- Predelay and the acoustic onset of the source IR are effect time. The causal resampler support offset is reported processing latency, not hidden IR delay

New inserts use Mix 0%, Output 0 dB, Solo Wet and Bypass off. All frequency, size, decay and modulation-rate defaults are valid engineering values, not illegal zeroes. The original frozen adapter has a known startup canonicalization defect described in PENDING-CANONICAL-DEFAULTS.md; fix and retest it before declaring the integrated build green.

## Shared host changes

The integration owner owns these changes in the assembled native project:

1. Append both stable registry IDs and factories without renumbering existing IDs
2. Compile Source/Plugins/ReverbProcessors.cpp into the application and tests that instantiate the factory
3. Link pinned JUCE's juce_cryptography for SHA-256, alongside existing JUCE modules
4. Embed Assets/Approved/approved_19.png and approved_20.png into ApprovedArt and expose approvedFaceplate(19/20)
5. In ModuleProcessor's existing telemetry-allocation condition, also enable PluginTelemetry for CONVOLUTION_REVERB and ALGORITHMIC_REVERB
6. In SessionState::captureParameters, after collecting PARAM children, dynamic_cast to ConvolutionReverbProcessor and call captureResourceState(result)
7. In SessionState::restoreParameters, retain all existing validation. After validation and before applying parameters, dynamic_cast to ConvolutionReverbProcessor and call restoreResourceState(values); reject a failed Result
8. Register AlgorithmicReverbTests and ReverbSchedulerTests as standalone numerical targets. ReverbPluginTests requires JUCE audio_basics/core. Binding/editor tests require the actual host/processor implementation and artwork
9. Test full XML/JSON sessions using the real new registry IDs after those changes. A parameter-tree round-trip alone is not that gate

Reverb state validation rejects unknown root fields and extra PARAM fields. Convolution allows exactly its six IR resource properties together, or no resource properties for a compatible no-resource state. Algorithmic state allows no root properties. PARAM children must contain only id/value, have the expected unique IDs and count, and contain finite values within their ranges. A small float-ULP allowance recognizes APVTS serialization near integer Hz values; it does not replace the strict startup/default regression.

## Convolution DSP and ownership

The audio path does not call JUCE Convolution or JUCE FFT: pinned JUCE source audit found internal spin locks in those callbacks/backends. PreparedConvolution owns radix-2 twiddles, bit reversal, spectra and all audio storage. No callback allocates, frees, locks, performs file I/O or creates a plan.

The FIR layout is a direct 64-tap head, synchronous partitions at offsets 64-255 and 256-2047, then fully scheduled 1024/4096-sample tiers starting at offsets 2048 and 8192. Large FFTs, spectral copies/products and overlap assembly are amortized over the available lookahead; there is no whole large boundary catch-up transform. See SCHEDULER.md.

Engines are fully built off-thread. A bounded pending pointer transfers exclusive ownership to audio. Audio primes the fixed latency, crossfades for 50 ms and publishes the retired engine to another pointer. Only the worker destroys pending/retired engines. Prepare and destruction require normal host quiescence. Reset/process are serialized. Resource operations and prepare are serialized off-thread, and generation checks prevent an already-extracted old file request from overwriting a later restored/explicit resource.

Wet excitation is finite-sanitized and fenced to +/-8 while finite dry samples remain untouched. Wet predelay is fractional, 0-250 ms. Optional cascaded one-pole high/low-pass filters affect wet audio only. Mix is linear; Solo Wet selects wet; Bypass ramps excitation/contribution and gain toward latency-aligned unity dry while tails continue internally. Default/bypass endpoints preserve signed zero after the reported delay. Reset clears histories. Identity-IR load/clear crossfades are complementary and do not create a midpoint gain hole.

### Clearing while an engine is being replaced

The pending and retired mailboxes each hold one engine. Audio deliberately defers adoption of a pending engine, including a clear, while an earlier engine is fading or its retired slot has not been reclaimed. A control-side clear first updates resource metadata and queues a silent engine; it is not a synchronous audio-clear completion signal. `hasActiveIR()` changes when audio adopts that silent engine, while the audible crossfade still needs to finish.

Keep servicing `reclaimOffThread()` as audio advances. The production `ConvolutionReverbProcessor::run()` worker does this on every loop, including after file loading ends. A standalone fixture that stops its worker must explicitly continue this off-audio duty: a single reclamation before clear is insufficient if the current fade retires afterward. Merely processing more audio cannot release the ownership barrier. For deterministic testing, finish the possible in-flight 4 ms latency warmup plus 50 ms fade, reclaim off audio, then advance the queued clear through its own bounded warmup/fade. Do not delete, allocate, lock, or reclaim inside `process()`.

## IR constraints, resampling and persistence

The chooser exposes WAV, AIFF and FLAC. Enabled JUCE basic readers decode a bounded file snapshot: 1-2 channels, 8-192 kHz, nonempty, at most 10 seconds / 1,920,000 source frames and 32 MiB. Samples must be finite, magnitude <=8, and not entirely silent. Mono IR applies independently to L/R; stereo IR is L-to-L/R-to-R dual-mono, not four-channel true stereo. No room recording or preset library is bundled or invented.

IR SIZE ranges 25-200%. It changes duration and pitch by resampling. ReverbIRResampler treats coefficients as weighted impulses, with Blackman-windowed sinc kernels, 1,025 polyphase rows and linear phase interpolation. Each phase has unit sum. Complete zero-extended support is kept at both boundaries, including a one-frame IR. The fixed causal prefix is ceil(Fs * 0.004) samples. Same-rate coefficients are copied exactly with that prefix. The maximum prepared support is bounded by 3,841,536 frames at 192 kHz. No automatic source trim or energy normalization is applied.

IR SIZE is an asynchronous resource transform, not sample-accurate automation. Prepare establishes the requested size before initial rendering. An ordinary failed load retains a prior valid IR and says so. Missing/changed session resources clear unrelated old audio and remain explicitly unavailable/dry-safe. No IR, including Solo Wet, outputs aligned dry audio rather than a made-up reverb.

Resource properties on the PARAMETERS/APVTS root:

- irResourceVersion = 1
- irPath: absolute source path, or empty for no IR
- irSha256: lowercase digest of the exact byte snapshot decoded
- irSampleRate, irChannels, irFrames: original source metadata

All six must appear together. SHA-256 prevents silently accepting changed files. Inconsistent decoded metadata is rejected transactionally. Recognized foreign-platform paths are retained as unavailable. Moving a project requires carrying its IR file and locating it with LOAD IR; audio is not embedded in the session. Saving during an outstanding file load is rejected with a wait-for-loading error instead of silently capturing the previous selection.

## UI and pictured controls

All 55 pictured controls/displays are mapped in CONTROL-BINDINGS.csv. APVTS attachments bind all 22 parameters. Hall/Room/Plate are real mode buttons. LOAD IR opens a native chooser and queues preparation. Cutoff knobs show disabled state when their filters are off. The existing bipolar physical-value slider centers 0 dB without changing host normalization. Semantic double-click values are restored after attachment construction.

Approved chassis art and outer dial rings/ticks are preserved. Static meter/trace apertures, including signal glow, are cleared before drawing. Cyan convolution data is the loaded source's signed peak waveform; orange is its measured RMS, explicitly peak-normalized. Filename, source rate and scaled source duration are real. Algorithmic cyan is actual 50 ms wet RMS against cumulative audio-sample time over a 10 s window. Orange is separately labelled nominal RT60. I/O meters are real stereo sample peaks, -60 to 0 dBFS; convolution numerical fields show the stereo maximum.

## Qualification limits

Allocation/lock-free and deterministic work bounds do not guarantee a hardware deadline. Historical optimized 64-frame probes measured 480k taps at 52.5 / 193.6 / 377.5 microseconds CPU mean/p99/max; maximum support at 266.8 / 592.8 / 1,503 microseconds. The maximum-length result exceeds the 333 microsecond budget at 192 kHz, and crossfades temporarily run two engines. Long IRs receive a UI headroom warning. Windows/device/listening and extreme-load qualification remain outstanding; no glitch-free claim covers every allowed IR/rate/buffer combination.

No original limiter, baseline DSP or prior offline Band/delay/nonlinear engine is replaced by this lane. Pinned JUCE reference: 8.0.10 commit 3af3ce009f6a02f6fa651008fffb5b41743a9fab. Official API background: https://docs.juce.com/master/classjuce_1_1dsp_1_1Convolution.html . The final FFT and resampler are owned implementations; the pinned source audit explains why the original public paths were not used in the callback.
