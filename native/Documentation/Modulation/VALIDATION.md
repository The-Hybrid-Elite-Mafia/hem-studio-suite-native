# Catalog 23 / 24 validation status

Date: 6 October 2026. Linux cloud source validation; no Windows or audio-hardware acceptance claimed.

## Passed

- `ModulationTests`: 2,909,386 assertions with `-std=c++20 -O2 -Wall -Wextra -Werror`
- The same core suite under AddressSanitizer + UndefinedBehaviorSanitizer, with LeakSanitizer disabled for the environment limitation below
- Staged JUCE binding gate: both real adapters and editors, actual APVTS bindings, all pictured switches/choices, strict state roundtrip/rejection, sample-rate/block lifecycle, actual host/no-host BPM, truthful shared input/output meters, and unchanged real insert-chain compensation. Final assertion total is in the staged run log
- Staged native raster gate: 16 PNGs covering idle at 1000/1280/1672 widths, three live modes each, no-host-sync and real-host-sync. Pixel checks preserve the authored outer dial rings and require dark idle plots/meters and a real moving LFO trace. Actual PNGs are visually reviewed; final synced/free-rate unit formatting has dedicated exact-text assertions
- Audio-thread allocation probes: zero core allocations and zero JUCE callback allocations/frees in the tested callback paths

## Staged versus integrated

The isolated JUCE binaries compile the actual new source files against the current complete native app objects and approved artwork library. They replace only `Processors.cpp.o` with an explicitly labelled evidence-only copy containing the two new shared telemetry IDs. Production registry/CMake/telemetry wiring and the full registered suite remain the integrator's separate gate. These staged passes are not a claim that all 24/31 modules have been integrated or that the full historical suite has passed against a new app binary.

## Evidence

- `evidence/modulation/dsp-test.log`
- `evidence/modulation/dsp-asan-test-no-lsan.log`
- `evidence/modulation/ModulationPluginBindingTests/run.log`
- `evidence/modulation/ModulationPluginEditorTests/run.log`
- `evidence/modulation/ModulationPluginEditorTests/rasters/`
- `evidence/modulation/build-staged.py` and each staged test's `commands.txt`
- `Documentation/Modulation/CONTROL-BINDINGS.csv`: every one of the 54 pictured controls/displays, retaining the source inventory IDs

Paths under evidence are relative to the continuing integration workspace above `native` and are verification artifacts, not runtime dependencies.

## Limits

LeakSanitizer aborts in this execution environment because ptrace is unsupported for its process inspection. That diagnostic is preserved in `dsp-asan-test.log`; the successful run explicitly uses `ASAN_OPTIONS=detect_leaks=0`. This is not a passed leak-sanitizer run.

The host currently does not provide a playhead/BPM. TEMPO SYNC truthfully shows unavailable and keeps the selected free-Hz rate; actual host BPM is covered with a real JUCE AudioPlayHead test provider. No manual or invented 120 BPM is installed.

Windows compilation, physical audio devices, listening approval, and worst-case 13-stem/eight-slot real-time capacity remain untested. Rotary is an explicitly documented physical approximation, not a measured cabinet emulation. Baseline/master/limiter algorithms and existing test thresholds were not changed.
