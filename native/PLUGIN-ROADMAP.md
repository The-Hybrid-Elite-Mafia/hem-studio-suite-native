> Historical recovered design/status document. Current implementation and verification take precedence in README.md, INTEGRATION-STATUS.md and VERIFICATION-2026-10-01.md.

# HYBRID ELITE MAFIA — Proposed next plugins

These are proposed future modules, not implemented features of version 0.9. Build original DSP and the approved heavy smoked obsidian interfaces. Commercial products below are workflow references, not source code, licenses to copy assets, or claims of equivalent performance.

## First finish the host

Before adding more processors: complete Windows runtime tests, exact faceplate mapping, Windows session recall testing and a graphical missing-file relink browser, and offline WAV export that explicitly excludes the monitoring branch. A new effect does not replace those delivery requirements.

## Priority shortlist

| Priority | Proposed HEM module | Why it earns a slot | Core implementation to build |
|---|---|---|---|
| 1 | Reference & Loudness Lab | Compare a mix with a reference at matched loudness so louder does not automatically seem better. | Separate reference playback, K-weighted gated integrated LUFS, short-term/momentary loudness, true peak, manual/automatic level-matched A/B. Monitoring comparison must not print to export. |
| 2 | Click & Plosive Repair | Address isolated clicks and vocal low-frequency bursts without broadly darkening the entire song. | Localized click detection and interpolated repair; separate transient-triggered low-band attenuation with audition and range limits. Preview edits before committing. |
| 3 | Multiband Dynamics Architect | Control uneven bass, vocal body and high-frequency energy independently. | Matched crossover design, per-band compression/expansion, attack/release/knee, stereo link and solo. Verify reconstruction and latency. |
| 4 | Kick / Bass Sidechain Architect | Temporarily reduce bass where the kick needs room. | An explicit external track sidechain input, low-band ducking and envelope controls. The current host does not yet expose an external sidechain bus. |
| 5 | Harmonic Hum Remover | Remove steady electrical hum without an indiscriminate low cut. | Adjustable fundamental and harmonic notches, frequency tracking, depth limits and removed-signal audition. |
| 6 | Spectral Repair Workbench | Fix a specific unwanted sound at a specific time/frequency location. | Offline STFT selection, spectral attenuation/interpolation, undo and audition. This is an editor workflow rather than just a realtime insert knob. |
| 7 | Clipped Peak Reconstruction | Attempt repair where clipped peaks have lost their shape. | Detect sustained clip runs; constrained interpolation/iterative reconstruction; quality comparison and bounded output. Lost audio cannot be perfectly recovered or guaranteed. |
| 8 | Room Tail Reducer | Reduce excessive reverberant tails when a vocal feels distant. | Time-frequency decay estimation or a licensed/trained model; listen tests for speech damage and musical noise. Substantially harder than EQ. |
| 9 | Tempo Delay & Ducking Space | Add controlled depth while keeping vocal words forward. | Tempo-sync delay, filtering, stereo routing and signal-triggered return ducking. |
| 10 | Convolution Room & Cabinet | Provide useful real measured acoustic spaces and guitar cabinets. | Partitioned convolution, user-loaded impulse responses, predelay, wet/dry, tail handling and latency reporting. |
| 11 | Vocal Pitch & Timing Editor | Correct chosen notes and phrases without blindly processing everything. | F0 analysis, voiced/unvoiced handling, formant-aware pitch shifting, time editing and undo. This needs a dedicated analysis/editing workflow. |
| 12 | Phase / Time Alignment Lab | Align related microphones or parallel sources more precisely. | Cross-correlation lag measurement, fractional delay, polarity audition and coherence display. Automatic results still require listening. |

My recommended first three are Reference & Loudness Lab, Click & Plosive Repair, and Multiband Dynamics Architect. They solve different problems from the existing EQ, D.H.T.A. and limiter.

## Official workflow references reviewed

- iZotope RX individual-module overview: https://support.izotope.com/hc/en-us/articles/6658241597073-Where-to-find-and-use-individual-RX-modules
  - Documents repair modules including De-clip, De-reverb and Spectral Repair; Spectral Repair is an offline/standalone workflow in that overview.
- FabFilter Pro-Q 4 spectral dynamics documentation: https://prod.fabfilter.com/help/pro-q/using/spectral-dynamics
  - Reference for frequency-selective dynamic treatment rather than indiscriminate whole-band gain changes.
- FabFilter Pro-MB overview: https://www.fabfilter.com/help/pro-mb/intro/about
  - Reference for per-band dynamics, stereo linking and sidechain workflows.
- Ableton Live audio effect reference: https://www.ableton.com/en/manual/live-audio-effect-reference/
  - Reference for practical utility, compression, sidechain and creative-effect workflows.
- iZotope reference-track guidance: https://www.izotope.com/community/blog/mix-and-master-with-reference-tracks-in-ozone
  - Reference for matched-level A/B and tonal comparison.

The roadmap is our design proposal. These sources establish examples of existing workflows, not proof that our proposed modules have been built or match those products.
