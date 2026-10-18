# MAIN Studio checkpoint 6: Instrumental Builder integration

2026-10-16. Cumulative source-only, unbuilt and untested candidate. This supersedes checkpoints4 and5 for continued development and includes its spectral, UI-navigation, utility-size and EOF source repairs plus the copied/adapted 2.0.0 Instrumental Builder. This is not an installer or a replacement for the working Windows application.

Restore a separate dated copy from original base Drive 1Xs6e0N_0l407ACWY52lmZegAoBd9UQbg, then 0.11.3 source overlay 1Soqu3epqc5boCJoJTidVRDHnq-46r99_, then this cumulative patch. PATCH-MANIFEST-CHECKPOINT6.json lists baseline/current SHA256 for all changed/new files. Review conflicts before overwriting independently changed PC source. All earlier Drive checkpoints remain retained.

Read INSTRUMENTAL-INTEGRATION-CHECKPOINT.md for the copied donor source, implemented offline workflow, sample/output bounds, license limitation and pending Windows checks. Read MAIN-STUDIO-CHECKPOINT4.md for the existing spectral/navigation repairs and their verification boundaries.

Checkpoint6 additionally bounds pattern JSON nesting, publishes clearer partial-file diagnostics, and preserves drafts across navigation with cancellation and worker-side retirement. A prepared host regression checks retained BPM, repetitions and step masks after reopening; it has not been run.

Key boundary: stage rendered stems only into empty targets, then explicitly APPLY through the existing importer. This sets the project BPM to the rendered pattern BPM. Samples and completed renders remain separate durable files. The builder draft and loaded samples now survive navigation and reopening while Studio stays open. Save the separate pattern before closing Studio; builder edits are not part of the MAIN session file.

The original 41-file HybridStudio2 donor snapshot was rechecked unchanged after extraction. No builds or tests were executed for this integration. Source/manifest/ZIP integrity checks only protect backup correctness; they are not evidence of functional correctness. The old Windows issues (Back mouse clicks, Vault typing, maximized Export interaction, plugin insertion and full module audit) still require actual PC verification.

No phone-app files, credentials, signing keys, user sessions, temporary build caches, intermediate objects, hidden system folders or unrelated repositories are included. No public sharing permissions were added.
