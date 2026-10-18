# Selected-track spectral scanner extraction

Prepared 2026-10-16 from HEM Stem Workbench source. The original standalone Workbench remains unchanged.

## Provenance and notices

- `SpectralAudio.h` extracts only the `Audio` planar-float buffer and shape/finite-value validation from Workbench `core/AudioCore.h`. Workbench lane/session, import/export and playback implementations are not incorporated.
- `SpectralCore.h` adapts Workbench `core/Spectral.h`. FFT size, overlap-add, time/frequency taper, magnitude/side measurements and correlation calculations are retained. Changes are the local audio type, cancellable validation, additional correlation-loop cancellation points, and output allocation without a temporary channel copy.
- `SpectralPanel.h` retains the Workbench `src/SpectralPanel.h` software-rendered 2D plot/projected 3D amplitude mesh. Its UI-only host adapter uses immutable shared grid snapshots and callbacks. Workbench panel-owned jobs, synchronous destruction waits and in-memory lane commit callback are not incorporated.
- `../../Tests/SpectralCoreTests.cpp` adapts Workbench `tests/SpectralTests.cpp`, adds mono/rate and delta identity fixtures, and checks outside-selection sample bits. It is supplied for later PC execution and has not been run for MAIN.
- Workbench `README.md` states that its new project source is AGPL-3.0-or-later. The complete source license is retained as `LICENSE-AGPL-3.0.txt`; extracted/adapted code carries SPDX notices.
- JUCE code is not vendored here. The MAIN build's existing JUCE 8 dependency supplies `juce_dsp` FFT and JUCE GUI classes. No second JUCE checkout, paid API, model, GPU/OpenGL dependency, subscription or artwork is added by this extraction.
- This notice does not change or assert licensing for unrelated MAIN source/artwork, or clear public distribution. The project's release-license review still applies. A zero-cost source dependency is not a claim of unrestricted closed-source redistribution.

## Scope and limits

- Offline, selected-track, mono/stereo PCM; unchanged source sample rate, channel count and exact frame count.
- Source PCM is never modified by `analyze` or `attenuate`. Spectral edits create a separate output; samples outside the selected frame interval are copied unchanged.
- 2048-point FFT, 512-frame hop, at most 96 time columns and 64 nonempty logarithmic frequency bands. At 96 kHz, FFT spacing is 46.875 Hz; narrow selections below that resolution may have little effect. A whole-song display is a bounded overview, not an arbitrarily detailed spectrogram.
- Projected 3D height represents measured amplitude. It does not identify sound sources, repair missing sound, perform de-reverberation, or automatically detect fizz/bleed.
- Per-input sample payload is limited to 512 MiB and 30 minutes, whichever is reached first. A 96 kHz stereo float source reaches the payload bound at about 11.65 minutes. The MAIN adapter applies a stricter 512 MiB combined source/output working budget with 16 MiB reserved for grids, transport rings and staging, so its effective stereo 96 kHz source limit is approximately 5.64 minutes. It computes audition delta from synchronized reader blocks rather than allocating a third whole-file buffer. The adapter also rejects source files larger than 512 MiB before hashing.
- All decoding, validation, hashing, FFT, reconstruction and private-file staging belong to the host's background worker. No audio callback should call these functions.
- Panel source-revision changes reset selection. The host must reject stale jobs and apply only to the captured selected source identity. Cancel/Back must retire still-running jobs without blocking the message thread.
- Original, processed and original-minus-processed delta audition must share the same origin and frame length. The panel only requests audition; the host owns lock-free/prepared playback and safe publication.

## Verification status

Source inspection only for this adaptation. No compilation, build, test suite, audio-device run or GUI run was performed, as requested. Existing Workbench tests have not been rerun against MAIN. Windows/PC verification remains pending, including stale-source/cancel/reopen behavior, exact length, unchanged outside-selection samples, delta identity, stereo phase, 96 kHz playback, and callback allocation/lock checks.
