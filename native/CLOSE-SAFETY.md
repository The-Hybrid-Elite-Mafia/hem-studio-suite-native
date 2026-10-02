# Session close safeguards

Closing the native window or receiving the application's normal quit request checks the current session and active work before exiting. No autosave or history system is implied.

- Session dirtiness compares the actual serializable track sources, insert identities/parameters/bypass, gains, mute/solo, loop and monitor parameters against the last successful save/recall. Transport position alone does not count as an edit.
- Unsaved edits offer Save and close, Discard and close, or Keep open. Active work without unsaved edits offers Stop work and close or Keep open.
- Keep open does not stop playback, cancel export, clear tracks or write a session.
- After approval, playback/recording stop, pending import/export are cancelled, and an in-progress session operation finishes. The UI waits asynchronously for worker finalization and processor retirement before approving the quit callback.
- A device-free fallback clears playing transports without waiting for acknowledgements from a nonexistent callback.
- Save and close uses the background session worker and records the saved snapshot only on a typed successful completion. A failed save keeps the window open, re-enables controls, retains dirty state and shows the error. Cancelling the save filename chooser also keeps the window open.
- A recording finalization failure prevents closing and leaves its recovery-path error visible. Completed WAVs and source files are never removed by close cancellation. An explicitly cancelled, unpublished mix export is cleaned by its existing transactional worker.
- Successful saves already in flight are not needlessly repeated when the current snapshot still matches them.

The safeguard does not cover forced process termination, OS crashes or power loss. Save sessions explicitly; this is not a crash-recovery journal.

## Focused validation

NativeHostLifecycleTests drives the production close state machine with an injected decision presenter, so tests require no modal desktop or human response. It verifies Keep open preserves the session, failed save cannot approve close, successful save precedes close approval, Keep open preserves an active export, and an approved close safely cancels that export without publishing a final file. The real desktop dialog is checked separately. Execution status is recorded in the current verification report and raw test log.

Final verification: PASS. NativeHostLifecycleTests passed all listed cases. The actual desktop Keep open and New/Cancel dialogs also preserved the inserted processor, and confirmed New cleared only the disposable QA session. See Verification/NativeHostLifecycleTests.run.log.

## External VST3 state

Dirty checks use the immutable external descriptor and process-unique instance/change revision tokens, not repeated serialization of large or nondeterministic opaque blobs. Replacement/reload is conservatively dirty, so changing a parameter and then reloading cannot reset the marker back to a previously saved counter. Successful save/recall records the current tokens. Vendor changes that are never reported through host notifications cannot be inferred reliably. Actual save/export capture gates external processing, establishes callback quiescence, and obtains the full opaque state on the message thread while transport/recording are stopped.
