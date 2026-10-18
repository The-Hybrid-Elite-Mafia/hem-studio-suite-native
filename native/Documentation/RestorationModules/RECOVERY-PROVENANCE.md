# Restoration files recovered after workspace reset

Recovered 2026-10-06 after the active integration filesystem vanished at approximately20:12 UTC. These are isolated new-module files only, not a recovered complete application. Original project paths were not rewritten.

## This agent's exact source-payload recovery
- native/Source/DSP/RestorationTelemetry.h: original successful full-file tool payload
- native/Source/DSP/RestorationCore.h: original successful full-file payload plus successful lastLow reset, selectivity smoothing, mouth warm-up and invalid-rate patches
- native/Source/Plugins/RestorationProcessors.h: original successful full-file payload
- native/Source/Plugins/RestorationProcessors.cpp: original successful full-file payload plus successful help-text clarification patch
- native/Tests/RestorationCoreTests.cpp: original successful full-file payload

Before the reset, RestorationCoreTests passed1,825,181 assertions, including six sample rates, neutral/latency/reset, profile/focus/onset/delta behavior, partition invariance, nonfinite recovery, and zero callback allocations/frees. The original binary/log vanished. No pre-reset source hashes were taken, so equality to the lost files cannot be independently hash-proven. Recovered files require fresh verification.

## Pending attempted changes, never executed before reset
- canonical APVTS constructor-default helper
- Tests/RestorationBindingTests.cpp and evidence/run-staged-tests.py
The attempted combined command failed at process creation. These are not part of the previously verified source and will be stored separately if recovered.

## Incomplete acceptance
The new processor wrappers/native editors were not compiled before reset. Shared registry/CMake/artwork embedding and host close/back callback still belong to the integration owner. No Windows/hardware/integrated build acceptance is claimed. UI control inventory CSV had not been authored. Additional parameter-behavior, CPU timing, sanitizer and raster checks remain necessary.

## Pending payloads preserved separately
pending/RestorationBindingTests.cpp, pending/run-staged-tests.py and pending/canonical-defaults-patch.py preserve the never-executed command's intended work. They have not run. The build helper's original placement was evidence/restoration under the full integration project; it must not run from this recovery package without path review.

## Clip-worker provenance clarification
The recovered ClipRepairCore.h is reconstructed executable-equivalent with some formatting/comments condensed, not claimed byte-exact. ClipRepairCoreTests.cpp was restored from the original full-file payload, without intentional edits; no original hash exists. ClipRepair.md was newly authored from preserved implementation/test facts after reset. Its historical310,552-check result is not a fresh recovered-build result. Full retesting remains blocked on the exact shared dependencies.

## UI-worker provenance and pending corrections
- RestorationEditors.h: reconstructed original successful full write plus exact successful factory/close patch; no native compile was run
- RestorationEditorTests.cpp: exact authored source from a failed process-start call; never previously written or run
- CONTROL-BINDINGS.csv: never authored, still pending95 pictured items

Intentionally unapplied UI corrections:02 axes must derive80Hz..min(18kHz,0.42Fs) from actual band centres;09 legend must say aligned normal output/applied repair delta and detection lamp should follow events-counter increments, not applied-repair activity; factory snapshots/comparisons need canonical APVTS values; unavailable plot text should be gray for idle-raster assertions. Additional compiler/geometry failures may remain because this UI is uncompiled.

## Primary algorithm reference checked before coding
The02/06 bandpass and peaking filter math was checked against W3C Audio EQ Cookbook,08June2021: https://www.w3.org/TR/audio-eq-cookbook/ . The adaptive detection/budget/onset and breath/mouth heuristics are original implementation, with no claim of matching a commercial processor or perfect semantic detection.
