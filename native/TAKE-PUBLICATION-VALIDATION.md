# Vocal take publication safety

Date: 2026-10-01

## Scope and correction

The previous recorder checked whether the final WAV existed and then called JUCE `File::moveFileTo`. JUCE's move operation can remove an existing destination, so another writer creating that path between the check and move could lose its file. Metadata used `replaceWithText`, which replaced an existing `.wav.take.json` sidecar.

The recorder now uses a shared, narrowly scoped `NoReplaceFile.h` helper for both publication points. The closed, flushed sibling partial is hard-linked to the final destination. This filesystem operation either creates the new destination entry atomically or fails without replacing an existing entry. No move, replacement, or copy fallback is used. Preflight uses `symlink_status`, so a dangling symlink is recognized as an occupied destination.

This change is confined to the recording worker. The callback, fixed FIFO, input samples, frame accounting, sample format, timeline calculation, DSP, and approved final master are unchanged. The existing file stream is explicitly flushed before closing and publishing the WAV.

## Observable outcomes

- An existing WAV path, including a dangling symlink, is rejected before recording arms
- A destination created after arming is never replaced. The recorded partial WAV remains intact, and the failure message includes its recovery path
- A successfully published WAV remains `Complete` and available through `completedFile()` if metadata later fails
- An existing or raced sidecar, including a dangling symlink, is never replaced. The status explicitly says the WAV was saved but placement metadata was not saved; the newly generated JSON is retained at a separate partial path for recovery
- After successful publication, the recorder removes its own partial names. If cleanup fails, the status discloses retained duplicates
- WAV and metadata publication are individually atomic; they are not a two-file atomic transaction. A crash after WAV publication can leave a WAV without its sidecar
- Filesystems without same-filesystem hard-link support fail safely and retain the WAV partial. Support for such filesystems is not implied, and there is deliberately no overwrite-prone fallback

## Focused automated coverage

`Tests/TakeRecorderTests.cpp` covers:

1. Normal mono and stereo 96 kHz, 32-bit floating-point WAV recording; exact 10,240-frame continuity and sample values; unchanged estimated-placement metadata
2. Recorder reuse after successful completion and no partial files on the success path
3. Preexisting WAV bytes and preexisting dangling WAV symlinks, rejected without starting a recording
4. WAV files and dangling symlinks created while the recorder is armed/recording, with every captured frame retained in a readable partial WAV
5. Preexisting and armed-time sidecar files and dangling symlinks, with original entries preserved and recovered JSON validated
6. A deterministic final-check/commit race using the same publication helper called by the recorder: another writer claims the destination after availability was checked, and its bytes survive the failed commit
7. Two concurrent commit attempts to the same final path: exactly one wins, neither source is destroyed, and the winner's complete bytes are preserved
8. Truthful `Complete`, completed-file path, and explicit metadata-warning status after successful WAV publication with a sidecar conflict
9. Empty-take cleanup
10. Zero instrumented allocations during all audio `push` calls

The final-check/commit test intentionally exercises the shared filesystem commit directly rather than relying on a timing-sensitive observation of the brief `Stopping` state. Integrated recorder races create the competing entry after `Armed` and before finalization. Windows systems without symlink-creation privilege explicitly report those symlink fixtures as skipped rather than treating them as passed.

## Verification status

**PASS**: the expanded `TakeRecorderTests` executable was rebuilt and run against the existing JUCE 8.0.10 Linux objects on 2026-10-01. All cases listed above passed, including Linux dangling-symlink fixtures, exact bytes/samples, competing commits, recovered partials, and zero instrumented audio-callback allocations.

- Executable: `build-export/TakeRecorderTests`
- Runtime evidence: `Verification/TakeRecorderTests.run.log`
- Binary SHA-256: `dbfca24ebdfce8e808b185aac27c689cb91379f1314bce521a2ff756d5d50e1f`

The first test execution failed because the sentinel fixture used JUCE `replaceWithText`, which defaults to CRLF line endings even on Linux, while the expected text contained LF. Fixture creation now writes raw UTF-8 bytes with `replaceWithData` and verifies them immediately before testing the recorder. Preservation assertions compare `MemoryBlock` bytes and exact length with `memcmp`; no production assertion was relaxed and no production change was required for this correction.

No physical input device, microphone, or hardware loopback was involved. This does not establish Windows filesystem behavior, physical-device timing, disk-full behavior, power-loss durability, or real-world recording performance.
