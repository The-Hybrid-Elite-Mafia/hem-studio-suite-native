# Native/VST3 integration verification — 2026-10-01

## Result

The native Linux application compiled and linked against JUCE 8.0.10/C++20. Production-host executable tests and focused actual desktop checks passed. ELF SHA-256: `426e12df2276353ee00bfd31a81ea8b2d9b57a9a50f36121fe49c5d74adeb429`. Current source CMake configuration succeeds with the external-host targets. No full CTest aggregate, Windows build, installer or hardware acceptance is claimed.

There is passing evidence for 29 distinct test programs across all milestones: the preserved 25 native programs plus ExternalHostTests, ExternalSessionTests, ExternalIntegrationTests and the fixture self-test. This count does not mean 29 tests were rerun in one final aggregate. The native-only details are preserved in NATIVE-ONLY-VERIFICATION-2026-10-01.md.

## New external-host tests: PASS

- Fixture self-test, CTest 1/1: exact float stereo gain/delay at 44.1/48/96 kHz; uneven blocks; all three latency modes and transitions; opaque-state roundtrip; 57 malformed-state rejections; unprepared/released silence
- ExternalHostTests: actual explicit-bundle discovery, real stereo gain, opaque capture/reload, 64-sample compensation and bypass, visible 6 ms cap rejection, parameter dirtiness, actual-instance editor lifetime/reopen/resize, worker adapter destruction with message-thread collection, context/latency faults and post-process dry fallback
- ExternalSessionTests: strict native v1/external v2 XML/JSON containers, bytes/Base64/size validation, missing-file rejection, preserved save targets, complete message-thread staging before commit, missing/throwing/incomplete callback rollback and cancellation. This container test executes no plugin binary
- ExternalIntegrationTests: the production MainComponent with a real message dispatch loop. Actual WAV import and selected VST3 install; independent export bit-exact against a native reference; changed gain saved/recalled through XML/JSON and reproduced in exported audio; editor retirement; non-finite-rate/zero-block rejection before plugin preparation; stale generation/missing binary preserves old slot; real asynchronous latency notification; exact 2,304-sample full-chain dry alignment; 64-sample reload/export; 96 kHz/512-frame context change, 48 kHz offline reprepare and same-context restart quarantine; changed state followed by reload remains dirty; global warning for an unselected faulted slot; visible over-budget rejection preserving state; close during export preparation/rendering/recall; final owner collection and shutdown; source bytes unchanged

Only the self-built HEM VST3 Fixture was discovered or executed. Tests never scan installed plugin directories or third-party binaries. Raw logs are in Verification; fixture build hashes/contract are in Tests/VST3Fixture.

## Native regression cohort: PASS

After hosting integration, nine existing programs were recompiled/linked as needed and rerun: ChainTests, StemMixerTests, SessionTests, NativeHostLifecycleTests, OfflineMixTests, OfflineMixWorkerTests, NativeWorkspaceTests, ProcessorControlBindingTests and PluginMeterDisplayTests. They retain eight-slot/native routing, 13 stems, v1 sessions, no-device lifecycle/close safety, native export transactions, all 98 native parameter bindings and real calibrated meters.

Following the native-editor positioning fix and final lifecycle hardening, ExternalHostTests and ExternalIntegrationTests passed again and the application was relinked. Additional regressions verify same-context release/reprepare cannot reuse stale vendor history, and changed external state followed by reload cannot erase the unsaved marker. New device-context reads use brief callback exclusion. The native processor/meter algorithms and Task #2B bytes did not change. Old logs from the preserved native milestone remain historical evidence, not a claim that every unchanged test was rerun here.

## Actual desktop verification: PASS

At 1280×720 on the cloud Linux desktop, using the UI build identified in Verification/Native-VST3-UI.json. Subsequent nonvisual lifecycle/revision/context-read changes passed production-host tests; the editor movement/painting code was unchanged:

1. PLUGIN VAULT exposes real selected-file load/reload controls, trusted-code notice and 6 ms limit
2. The exact self-built bundle loaded through the native chooser; the real XEmbed plugin editor opened inside the one-window workspace
3. Dragging the hosted gain slider changed its real value from 1.000000 to 0.473684. Closing/reopening the editor retained that value
4. Save and close wrote a new version-2 session, preserving all state. Restart and actual chooser-based recall restored the exact displayed gain. Saved session: 11,576 bytes; hash recorded in Verification/Native-VST3-UI.json
5. Expansion initially exposed a Linux XEmbed ancestor-move bug. The fix uses public ComponentMovementWatcher and XEmbedComponent::updateEmbeddedBounds, without modifying JUCE. The final rebuilt app now passes actual expand, restore and divider-drag checks: the native child follows the pane origin correctly, with no stale-offset grey region
6. Approved ghost/header illumination and shared transport remain intact. Output meters truthfully show NO AUDIO because no physical device is available. Other open image windows/folders were untouched

No state was discarded during the restart. No physical sound, microphone capture, Windows editor, third-party vendor or large external-editor clipping/DPI matrix was tested.

## Build reproduction and limits

README.md gives the portable CMake/VS2022 route. The current CMake configuration passed. Efficient cloud validation reused unchanged JUCE module objects and byte-preserved baseline objects, but rebuilt juce_audio_processors.cpp with JUCE_PLUGINHOST_VST3=1 and compiled the changed native units. Dependency hashes and exact commands are retained. All builds use the pinned local JUCE 8.0.10 source; its supplied VST3 SDK is not swapped or included in the source ZIP.

Development flags include the recorded Release/-O1 configuration and JUCE's appended -O3 C++ flags. This is not production real-time performance certification. The in-house fixture does not establish other vendors' semantics. JUCE's public void prepare/state APIs and discarded native setup/process results leave documented compatibility limits even after direct float32/native-latency/stereo-bus checks. See VST3-INTEGRATION-STATUS.md.

Task #2B Engine.h/cpp SHA-256 remain `84a3829568bc0cb5c99280769bbf9e72f128d5015a8d48e0a3a24debf582217f` and `d409243bff6a3f646d9af5726c64c99b378017845bbf295a61f7e1de710802a4`. No experimental limiter is adopted.

Remaining gates: Windows build/packaging, physical devices/loopback/hot-plug, full-load deadlines, vendor compatibility, licensing before distribution, disk-full/power-loss/long-session stress, arranger timeline, MIDI, external AU and automation lanes. Per-file looping and the 12-entry native inventory remain as documented. The master limiter's independent reconstructed-peak/listening-quality acceptance remains open.
