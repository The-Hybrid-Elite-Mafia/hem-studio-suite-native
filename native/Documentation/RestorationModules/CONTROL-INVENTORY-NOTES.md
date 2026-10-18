# Restoration pictured-control inventory

Prepared 2026-10-07 for approved faceplates 02, 06 and 09. `CONTROL-BINDINGS.csv` is the exhaustive pictured-item inventory. This is source/pixel reconciliation, not a claim that every runtime acceptance test has passed.

## Count convention and provenance

The original recovered inventory reported 35 items for 02, 40 for 06 and 20 for 09: **95 pictured items total**. That exact convention is retained here; there is no padding with decorative bolts, headings, repeated axis tick marks, trace samples or individual LED segments.

The count and original item identities are recoverable from:

- `recovery-20261006T2015/control-inventory/missing-designs-retained/retained-generation-v1.py`, the 02 / 06 / 09 definitions and helper conventions
- `recovery-20261006T2015/control-inventory/missing-designs-retained/retained-historical-tool-output.txt`, reporting 35 / 40 / 20
- `recovery-20261006T2015/restoration/RECOVERY-PROVENANCE.md`, which explicitly says the binding CSV was never authored before recovery
- `restoration-merge-audit-20261007/RESTORATION-MERGE-AUDIT.md`, the recovered implementation and outstanding-correction audit

The old generator was read, not replayed against former project paths. This CSV is newly authored from actual approved pixels plus recovered/current source; it is not claimed to be a byte-exact recovery of a lost CSV.

| Pictured kind | Rows |
|---|---:|
| Rotary knobs | 21 |
| Separate knob value apertures | 21 |
| Header buttons | 6 |
| Profile / algorithm choice buttons | 5 |
| Toggle buttons, including the second 06 Bypass | 8 |
| Meter columns / strips | 21 |
| Other numeric readout apertures | 7 |
| Plot displays | 5 |
| Clip-detected lamp | 1 |
| **Total** | **95** |

The 21 meter rows comprise 12 independent I/O L/R columns, eight originally drawn 06 section columns, and one 02 summed-reduction strip. The eight 06 section columns are deliberately reconciled to four single, explicitly LINKED runtime statistics. Both original columns in each panel remain individually accounted for; no second channel statistic is invented. The shared runtime panel clears the illustrative paired bars and paints one linked lane.

The seven non-knob numeric fields comprise four 02/06 I/O maximum-peak readouts, the 06 FOCUS OUTPUT readout, and the two 09 counters. The little live 09 I/O numeric overlays, per-lane RMS markers, 02 summed-cut numeric overlay, 06 linked-statistic values/captions, and 09 skipped-run status are supplemental painting within existing pictured regions. They do not increase the historical 95-item count.

## Parameters are not the pictured-item count

There are exactly **30 unique APVTS parameters**, not 95 parameters:

| Faceplate | Existing APVTS root | Registry identity | Unique parameters | Dials | Pictured items |
|---|---|---|---:|---:|---:|
| 02 | DYNAMIC_RESONANCE_SUPPRESSOR | dynamic-resonance-suppressor | 12 | 9 | 35 |
| 06 | BREATH_MOUTH_CONTROL | breath-mouth-control | 11 | 8 | 40 |
| 09 | CLIP_REPAIR | clip-repair | 7 | 4 | 20 |

CSV `parameter_index` is the existing zero-based specification order. Ranges, interval, unit and declared default come directly from each recovered processor constructor. Decimal default literals may require APVTS normalize/denormalize canonicalization for exact float comparisons; the CSV does not silently substitute different sonic defaults. Each knob value aperture shares its parent slider/APVTS attachment and is not a second DSP control.

- 02 order: sensitivity, low, high, selectivity, maxReduction, attack, release, mix, output, profile, soloDelta, bypass
- 06 order: breathDetection, breathReduction, breathLength, mouthDetection, mouthReduction, mouthLength, low, high, listenBreath, listenMouth, bypass
- 09 order: threshold, length, amount, output, algorithm, listenRepair, bypass

Neutral processing remains maxReduction=0 dB on 02, both reductions=0 dB on 06, and amount=0% / output=0 dB on 09. The existing delays remain part of neutral/bypass behavior. `CLIP_REPAIR.length` maps to `ClipRepairSettings::interpolationLength`.

02 profile buttons write the same `profile` parameter: Broad=0, Vocal=1, High=2. 09 choices write the same `algorithm` parameter: Fast=0, Precise=1. Both pictured 06 Bypass buttons write the same `BREATH_MOUTH_CONTROL.bypass`; component IDs are `bypass` and `bypass_1`. The 06 OUTPUT aperture displays max(L,R) measured output sample peak and has no APVTS gain parameter. No Presets or Close button is pictured or added on 09; its host Back remains a host affordance outside this artwork inventory.

Only 02 and 06 have module-local factory preset UI. Their snapshots include every parameter, with canonical values and normal host gestures. 06 previous/next are the pictured arrow buttons. A Close action must be explicitly owner-bound; an unbound Close is disabled. This inventory creates no processors, extra DSP or alternate signal paths.

## Pixel and geometry conventions

All three approved images were opened and their actual pixels inspected at 1672 x 941. Their hashes were checked against the audit:

| Image | SHA-256 |
|---|---|
| `Assets/Approved/approved_02.png` | 057709eafd0af5ae0a3083bbadb840a51ca19b61e803b1acb5df0cd4e3d38c7b |
| `Assets/Approved/approved_06.png` | 0478cd2e87c30db365c0e38edca43d8b00aae3ca9129c258371b1574910e0f2e |
| `Assets/Approved/approved_09.png` | 69ae6cd6b2b3bc8faaac836bd0596a2064af15aa2445c6f7180e98313c630253 |

Bounds are x,y,width,height in original artwork coordinates. `pictured_bounds_px` locates the actual item and is approximate for individual baked meter-column ink. `runtime_region_px` identifies the source-defined logical body/readout/panel/lane rectangle; it is not a claim that every pixel in that rectangle changes. A dial slider's component bounds are the union of body and readout, while its two CSV rows separately identify the pictured body and aperture. A value text box has no separately assigned component ID; the parent slider ID is repeated intentionally. Parent-painted I/O readouts are explicitly marked as having no separate component ID.

The live 02 trace plots are clipped inside caption-safe subregions of the listed measurement panels. The same applies to 06 history and 09 repair history. Their outer measurement labels can differ by one pixel from the paint rectangle inherited from source; `runtime_region_px` records the measurement/component rectangle, while trace clipping is defined by `paintResonance` / `history`.

All 21 dial rows use the existing `PhysicalValueSlider` and `SuiteMaterialLook` original-ring-preserving material path. The staged shared `dial()` helper sets `preserveAuthoredDialRing=true` once for every dial. The immutable approved image supplies the chassis, outer ring/ticks and static legends; the renderer clips fixed material lighting and one value-responsive pointer to the original inner body. Value readouts stay inside their existing apertures. This inventory does not replace artwork or introduce another renderer.

## Supplemental static labels and axis semantics

These labels are explicitly reconciled but are not extra rows in the historical 95-item convention:

- Shared brand/title/Ghost artwork, ornamental frames and fasteners remain static. Module titles are Dynamic Resonance Suppressor, Breath / Mouth Control and Clip Repair
- Each knob's printed legend belongs to the corresponding knob item. Readout units come from its unchanged APVTS specification
- I/O headings INPUT / OUTPUT and dBFS describe the independent L/R sample-peak segments. Added white markers are the same channel's measured 300 ms RMS. Neither is a true-peak or LUFS measurement. Numeric I/O readouts remain stereo maximum sample peak
- 02 LIVE SPECTRUM ANALYSIS uses 32 measured constant-Q bands, not an FFT. Horizontal range is the actual first/last band centre: **80 Hz to min(18000 Hz,0.42 x sample rate)**, logarithmically positioned. The low/high knobs are processing focus limits, not an instruction to fabricate a wider analyzer bank. At 8 kHz the upper centre is 3360 Hz; at 44.1 kHz and above in the tested-rate family it is 18000 Hz
- 02 spectrum amplitude is -120..0 dBFS. Spectrogram frequency rises bottom-to-top over that same band range; its color scale is -100..0 dBFS, newest column at the right, nominal 128 x 20 ms history. DELTA uses the same horizontal frequency range and vertical -120..0 dBFS, correcting the misleading original vertical Hz caption. It measures actual removed contribution after Mix/bypass weighting and before output gain. REDUCTION dB is the sum of current filter cuts, not broadband RMS attenuation
- 06 BREATHS, MOUTH TRANSIENTS and FOCUS are static section headings. DETECTION HISTORY cyan/orange show linked breath/mouth scores 0..1; green is **max(applied breath dB,applied mouth dB)/24**, not their sum. DETECTION LEVEL is a score/normalized-reduction axis. TIME (s) describes a nominal four-second buffer with the exact sampled full-buffer span in the live caption. Detection panels display percent, attenuation panels display actual dB, all explicitly LINKED
- 09 REPAIR VIEW is aligned left-channel telemetry. Orange is delayed dry, cyan is the **normal output** including gain/bypass even while auditioning repair, and green is the **applied repair contribution**, excluding gain-only changes. Green must not be described as dry minus output. Amplitude is +/-1 FS with visual-only clipping. Time (ms) is nominally a 100 ms buffer with the exact sampled full-buffer span in the live caption. The I/O output meter separately measures the actually audible output, including audition
- 09 CLIP DETECTED lamp follows new detected-run counter increments for 250 ms, including when Repair Amount is zero. Counter rollback / epoch change / stale audio clear old activity. The neighboring event field is a detected per-channel run count, and REPAIRED SAMPLES counts per-channel samples actually changed. Detection alone does not increment repaired samples. Skipped-run status belongs to the existing repair view

History timing is quantized by the existing telemetry interval: `max(1,round(Fs*requestedSeconds/512))` samples per point. A full 512-point trace spans 511 intervals. Consequently nominal 09 100 ms is 127.75 ms at 8 kHz and 95.8125 ms at 48 kHz; 06 nominal 4 s also has small rounding drift. The staged editor now derives and displays this exact sampled span (for example, 95.81 ms and 3.992 s at 48 kHz). Tooltips distinguish the nominal requested duration from that exact sampled span. No DSP timing or parameter was changed.

## Verification completed for this inventory

- Inspected actual pixels of all three approved PNGs and rechecked their SHA-256 hashes
- Read recovered inventory definitions/historical counts, original restoration recovery, merge audit, processor parameter specifications, telemetry/core semantics, and current staged editor/shared renderer
- Parsed CSV back from disk; verified exactly 95 unique item IDs, exact 35/40/20 distribution, 21 dials and 21 separately listed readouts
- Reconciled every parameter-bearing row to the 30 source specification IDs/ranges/order/interval/defaults; all 12/11/7 parameters are represented
- Verified exact choice values, shared duplicate 06 bypass mapping, absence of 06 output-gain parameter, and absence of 09 header preset/close rows

Only this CSV and these inventory notes were written. No source, DSP, test, build, approved artwork, recovery payload or integration file was modified by this inventory task. Compile, input/automation/recall, meter calibration, negative-state rasters, event-lamp behavior and immutable-ring runtime tests belong to the integration owner's acceptance evidence; source/pixel inspection is not substituted for those tests.
