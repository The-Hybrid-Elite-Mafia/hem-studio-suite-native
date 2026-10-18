# Bounded external VST3 hosting milestone

2026-10-01. This extends the preserved native-only version 2 source snapshot. The current working copy adds real external VST3 plumbing; acceptance is limited to the self-built Linux stereo effect described below. It does not certify third-party plugins, Windows or AU.

## User workflow and limits

PLUGIN VAULT now has LOAD SELECTED VST3 and RELOAD CURRENT VST3. Select one trusted .vst3 file/bundle; choose its contained effect if it exposes multiple types. Discovery and instantiation execute native code in this process. There is no automatic directory scan, download, installer, architecture bridge or crash-isolated scanner. Only trusted plugin binaries and trusted sessions should be loaded.

The first slice accepts same-architecture 32-bit-float stereo audio effects. It uses the existing eight compensated slots and the existing ceiling of ceil(actual sample rate × 0.006) samples per slot: 288 at 48 kHz, 576 at 96 kHz. An unsupported or over-budget load leaves the prior slot intact and displays the reason. Instruments, MIDI-only effects, mandatory auxiliary inputs, multi-output layouts, double-only processing, external AU and ARA integration are outside the accepted contract. No MIDI/tempo automation lane or arranger is added.

A hosted effect supplies its actual native editor, with a genuine JUCE generic parameter editor fallback. Rack bypass remains the host's aligned dry/wet bypass, separate from any vendor bypass control. Process-unique instance/change revisions from parameter and non-parameter state notifications mark session edits; reload/replacement cannot erase a pending dirty marker. The wrapper's only own measurement is actual output sample peak; no invented vendor-specific meter or native APVTS binding is claimed.

A reported latency/layout/program-metadata change or device preparation/release (including a same-rate/block restart) marks the instance unavailable. The chain uses its already delayed dry path, rather than emitting output with stale compensation. The affected slot says PAUSED, routing uses its unavailable colour, and a global status identifies the track and slot even when another track is selected. Pause, select the slot and use RELOAD CURRENT VST3 to preserve current opaque state while validating a fresh instance. Over-budget reload rejects the new instance while retaining the old state and safe dry routing.

## Lifecycle, state and export

- A message-thread host service retains each actual AudioPluginInstance. Adapter objects may retire on workers, but the last actual instance reference is collected on the message thread after workers/graphs/editors release their references
- Creation, state restore/capture, bus negotiation, preparation and native editor operations occur on the message thread. No worker synchronously asks a message thread that may be joining it to do those operations
- Device/worker calls to the adapter's prepare method only validate an already prepared context or invalidate it. They never forward VST3 preparation from the audio callback
- External state capture requires stopped playback/recording. The host gates external processing under brief callback exclusion, releases that exclusion, serializes on the message thread, then restores prior gates. Even silence callbacks cannot race the vendor state capture
- Native-only sessions remain version 1. Sessions containing external slots are version 2 and carry a descriptor, stable native identity and bounded opaque state, plus independent rack bypass. XML and JSON share strict validation. Empty vendor blobs are allowed; unsupported/malformed containers are not
- Canonical Base64, declared bytes, descriptor bounds and the existing 8 MiB total saved-session ceiling are checked before replacement. A failed/oversized save preserves the old file. A missing referenced binary aborts recall without replacing the current session
- Recall prepares native/file resources on its worker, then stages external instances on the message thread before transactional bank commit. Failure, a stale device generation or incomplete staging keeps the existing bank
- Export captures an independent snapshot, then stages one private external instance per message-timer tick. Cancellation can be handled between instances. Only processing and WAV work run on the export worker. An external fault during rendering fails the unpublished export instead of silently changing the mix
- Normal close/quit handles pending selected-file work, export preparation/rendering and session load/finalization. Completed files and source WAVs are preserved; unpublished cancelled exports are cleaned

The direct native VST3 interface is inspected through JUCE's supported ExtensionsVisitor. The host checks kSample32 support, native unsigned latency against the cap, and actual stereo bus arrangements before publication. This avoids treating a huge unsigned latency as JUCE's clamped zero.

## Verified executable coverage

Only the original in-house HEM VST3 Fixture was built or executed. No third-party binary or installed plugin location was inspected, scanned or loaded.

1. Fixture self tests: bit-exact gain/delay at 44.1/48/96 kHz, six uneven block sizes, 0/64/262144-sample modes and transitions, opaque source-state roundtrip, 57 malformed-state rejections and unprepared silence
2. ExternalHostTests: selected bundle discovery, real stereo gain, opaque capture/reload, 64-sample fixed compensation and bypass, 6 ms rejection, revisions, actual-instance editor lifetime/reopen/resize, worker adapter destruction/main collection, context/latency faults and post-process aligned-dry fallback
3. ExternalSessionTests: native v1 compatibility, external XML/JSON bytes, empty state, malformed/mixed/oversized containers, missing file, preserved destination bytes, message-thread staging callbacks, rollback and cancellation without loading a plugin binary
4. ExternalIntegrationTests: production MainComponent with a real message dispatch loop; WAV import; selected VST3 load; independent export identical to the native reference; changed-gain state saved/restored through XML/JSON and reproduced in export; editor retirement; missing/stale-load rollback; real asynchronous latency notification; exact full-chain dry alignment; reload/export at 64 samples; visible excess-latency rejection; close during export/recall; worker shutdown and owner collection

Final results, hashes, latest context-validation cases, native regressions and actual desktop observations are recorded in VERIFICATION-2026-10-01.md and raw Verification logs. The in-house fixture is test source, not a production mixing plugin release.

## Concrete compatibility boundaries

JUCE 8.0.10's public prepareToPlay and setStateInformation return void. Its VST3 implementation discards several native setup/activation and process return codes. Public capability/layout/latency checks cannot prove every vendor setup call succeeded or validate arbitrary vendor state semantics. Finite output or silence is not proof of correct processing. The fixture proves its own state behavior only. A malformed opaque vendor blob can legitimately be ignored by the vendor; this host does not mislabel that as semantic validation.

The adapter reacts when a plugin's notification arrives. It cannot predict unannounced or late vendor changes. Native crashes/hangs cannot be caught reliably by C++ exceptions; one blocking instance creation or process call can still freeze this in-process host. The export-preparation UI can cancel between instances, not interrupt arbitrary vendor code already executing.

Host routing remains bounded; arbitrary VST3 processing is not guaranteed allocation-free, lock-free or real-time safe. JUCE's VST3 wrapper itself uses a process lock. Hardware deadlines, physical playback/recording, Windows loader/editor/driver behavior, macOS/AU, broader vendors, disk-full/power loss and extended stress remain unverified. The unchanged Task #2B master limiter's separate true-peak/listening-quality limitations remain open.

No arranger timeline, external automation lane system, MIDI routing, 100 installed plugins or additional image-concept DSP is claimed. The 12 native entries remain intact, with explicit external load beside them.
