# Checkpoint005a: bounded history-glass repair

Two production lines clear Multiband Dynamics history glass through y882 before painting the unchanged y879 grid baseline. This removes baked yellow waveform remnants at source rows879..881 while retaining the authored cyan frame/axis marker outside x418. No DSP, parameter, control layout, or engine changes.

BandHistoryGlassTests fails against005 renderer, then passes at1x/2x for prepared/released states. It rejects injected residue, preserves frame-marker pixels, and verifies unchanged parameter state. The existing412 plot-pixel and immediate release/reprepare guards also pass. Native raster pixels were inspected. Linux app rebuilt; not launched.

This resolves005's named artwork follow-up only. Dense real-time deadlines and Windows/device/native-dialog acceptance remain blocked/unverified as recorded in005.
