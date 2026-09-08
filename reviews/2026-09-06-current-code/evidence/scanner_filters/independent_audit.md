# Independent audit of current review probes

Reviewed the complete current file_safety/review.cpp, results.txt, CMakeLists.txt; reran file_safety/build/review successfully. All ten assertions reproduced, captured in file_safety_independent.log. Read the associated production branches to challenge the six principal safety claims.

1. SOURCE_EDIT_LOST is a real loss of a concurrent in-place source edit in the buffered cross-volume Move branch. The environment seams merely select this production branch on one test volume. The probe changes an essence byte after copying, preserving file identity, length and UMID; final source identity verification accepts that state and deletes the source. Do not claim that Media Composer routinely performs this kind of edit, or that every move route has been reproduced.
2. CANCEL_DELETES_REPLACEMENT is real deletion of another writer's replacement destination. A path-only cleanup after cancellation removes the current destination without verifying it is the file this operation created. The source survives; the replacement does not.
3. FAILED_RECOVERY_FORGOTTEN proves lost recovery tracking and retry: the first run explicitly flags manual attention, leaves the original safely parked, and marks recovery complete; the next run removes the journal without attempting the now-possible restore. Do not describe the first failure as silent or say the parked media bytes were deleted.
4. RECOVERED_PATHS_REVERT proves stale resume paths after remount: rewritten paths exist only in memory, while the recovered fast path later uses stored paths. Stable volume identity/mount root are supplied by the intended test override. This is not an actual external drive remount test and does not prove arbitrary files on another volume are deleted; later identity checks still exist.
5. UNVERIFIED_WHOLE_COPY_ACCEPTED proves an equal-length destination with the same header identity but different essence is counted as finished during recovery. The source remains in this proof. Describe unverified/corrupt copy acceptance, not direct automatic source deletion.
6. MOVE_CANCEL_STRANDING_UNJOURNALED proves incomplete cleanup is recorded as clean cancellation. Both source and copied destination remain; the evidence demonstrates lost cleanup/recovery tracking, not loss of unique media bytes.

Additional reproduced cases: a fully balanced 4,999-item group is relocated repeatedly (non-idempotent planner); dormant Undo forgets a failed replacement-restoration tail (not enabled in current product, both versions retained); Rename can proceed after journal creation fails (warning emitted, loss of recovery guarantee, no byte loss in the proof); grouped Rename can split relatives after a member fails (partial operation, not proof media becomes offline).

# UI probe challenge

Read all ui_probe.cpp and ui_probe.log and associated production paths. All seven observations are supported by production code:

- Newly constructed QListWidgetItem selection is set before insertion, so initial/refresh/new manual-path selection is lost. Selection of an already-owned existing item is a different successful path.
- Removing model rows after move/delete does not rebuild project sidebar items/counts.
- Rebalance completion displays projected folder totals even for cancellation or failure. The UI probe supplies a cancellation result; it proves presentation error, not incorrect filesystem changes.
- Destination preview independently chooses the same '(2)' name for third and later same-name files. Execution reserves destinations, so this is a preview error, not demonstrated overwrite.
- Capacity calculation includes skipped rows; per-row conflict changes also do not recalculate summary. The probe's large file size is synthetic metadata fed to the real UI calculation; no large file copy or real full-disk condition is tested.
- The installed macOS Qt 6.5.3 accessibility guard deliberately suppresses table/list/tree children and table interface. This is a current accessibility limitation caused by a documented crash workaround; removing the guard alone is not a justified fix.
- Log migration unconditionally removes the old log directory after optionally moving the old current log. Existing history and crash reports are lost when the new current log already exists. These are log/report bytes, not media.

# Final additional test coverage

Read every line of tests/tst_pmrparser.cpp (1–1055) and tests/tst_omfparser.cpp (1–756): 1,811 lines, including all helpers, fixtures, cases and registration.

Verified test reliability weakness: tst_pmrparser.cpp192–201 writePmr ignores directory creation, file open and write results, always returning a path. Rejection cases such as wrong_magic_is_refused470–480 accept the same empty/ok=false outcome that the missing-file test explicitly checks428–430; therefore fixture-setup failure can satisfy a negative test. Use checked fixture writing with an assertion before parsing. This was established directly from control flow, not by a disk failure injection.

Small coverage mismatch: tst_pmrparser.cpp759 says keyed filename lookup is checked;760–762 only checks successful construction and size==2. No keyed lookup is performed in this case.

OMF parser-to-MDB/finalise comparisons are useful consistency tests, but share decoding/finalisation components and cannot alone independently establish the binary format interpretation. The tests also contain explicit literal fixture pins; do not dismiss the entire suite as self-comparison.
