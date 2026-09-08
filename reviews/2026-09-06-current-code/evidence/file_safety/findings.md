# File operations review — 2026-09-06

Reviewed current disk source only. No previous reviews, memory files, or historical notes were opened. Read the requested cpp-coding-standards SKILL.md; applied C++17-compatible guidance and accepted Qt parent/child ownership. No repository source or test files were changed.

## Reproduction and coverage

Standalone C++17 harness links the **unchanged production implementations** against Qt 6.5.3. Built and run on macOS arm64/APFS. Windows branches were read, but these are not claims of Windows execution. The copy harness forces the existing buffered Move leg with MEDIAMUSTER_FORCE_MOVE_COPY and MEDIAMUSTER_DISABLE_CLONEFILE; no forced success or altered production logic. Copy verification remains enabled. Real Avid header fixture bytes are padded to an 8 MiB synthetic payload for checksum/identity tests; this exercises a real parsed UMID, without claiming the padded file is a fully valid production MXF container. All filesystem fixtures are temporary.

Run:

```sh
cmake -S /Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/file_safety -B /tmp/mediamuster-review-20260906-preserved/file_safety-build -DCMAKE_PREFIX_PATH=/Users/martymclean/Qt/6.5.3/macos -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/mediamuster-review-20260906-preserved/file_safety-build -j 4
/tmp/mediamuster-review-20260906-preserved/file_safety-build/review
```

Every assertion passes. Machine-readable outcomes: `results.txt`; exact repros: `review.cpp`. The journal scenarios use OpJournal to write real schema-2 records; changing the saved process ID to zero represents a dead owner so the real live-process protection does not suppress recovery.

All lines read in these 26 production files: fileidentity.{cpp,h}, nativefile.{cpp,h}, opcopier.{cpp,h}, opjournal.{cpp,h}, opmanager.{cpp,h}, oprequest.h, oprescue.{cpp,h}, oprunner.{cpp,h}, opundo.{cpp,h}, opverify.h, parkedfile.h, trashrouter.{cpp,h}, rebalancer.{cpp,h}, rebalanceplan.h, testpause.h, pathkey.h. Also read backgroundjob.h and relevant MainWindow resume code.

All lines read in these ten test files (5,620 newline-counted lines): tst_fileidentity.cpp, tst_opjournal.cpp, tst_opmanager.cpp, tst_oprequest.cpp, tst_oprescue.cpp, tst_oprunner.cpp, tst_opundo.cpp, tst_pathkey.cpp, tst_rebalancer_parse.cpp, tst_rebalancer_plan.cpp; also testutil.h. Root ran the fresh whole test suite separately.

## FS01 — P1: Move discards same-size in-place source changes

**Location:** src/fileidentity.cpp:142-146, used by src/oprunner.cpp:748 and followed by the source removal at 846.

**Engineer:** High-confidence verification compares inode/file ID and size but intentionally ignores mtime. The UMID compares clip identity, not all file bytes. The buffered copier hashes the bytes read earlier, then checks the destination against that earlier stream. A source edit to an already-read payload byte preserves size, inode, and UMID, so both checks pass; Move then removes the edited source. This is a gap between object identity and snapshot/content stability, not a claim that FileIdentity violates its own documented definition.

**Repro:** `inplaceSourceEdit()`. After the buffered source read, the Verifying progress callback changes source offset 3 MiB from x to Z in place. The genuine parsed UMID remains unchanged. Actual output: `SOURCE_EDIT_LOST {"destinationRetainedOldByte":true,"failed":0,"sourceConfidence":2,"sourceExists":false,"sourceHadUmid":true,"succeeded":1}`. The only Z byte is gone; the reported successful destination has x.

**Plain English:** If another program changes a file without making it longer or shorter while a move is running, MediaMuster can discard that change and report success.

**Fix direction:** Capture and check content stability independently of same-object identity, including mtime for local sources. Keep a stable source handle/snapshot through verification and final disposition; refusing detected changes is safer than deleting the edited original. Add the combined Move test; the existing fileidentity test at 146 explicitly pins same-size in-place edits as Match and therefore does not cover this consequence.

## FS02 — P1: Cancel can permanently delete another writer's replacement

**Location:** src/parkedfile.h:132 and 179; src/opcopier.cpp:478-481 exercises it on cancellation.

**Engineer:** noteDestinationWritten is a permanent boolean about a pathname. Once true, restore unlinks whatever now occupies that pathname, without verifying it is the created file. NewOnly only protects creation time. A later rename/replacement invalidates pathname ownership.

**Repro:** `replacedDestinationCancel()`. Begin a buffered Copy. At Verifying, remove the engine-created destination, create a separate file at that same path containing `another editor's unique file`, then cancel. Production rollback deletes that replacement. Output: `CANCEL_DELETES_REPLACEMENT {"failed":0,"replacementExists":false,"sourceExists":true,"succeeded":0}`.

**Plain English:** A cancelled copy can erase a different file that another application put at the destination while the copy was running.

**Fix direction:** Write to an exclusively owned unique temporary destination and atomically publish it only after verification. Preserve and verify file-object identity before deleting anything during rollback; avoid tracking ownership with a boolean alone. Existing racer tests only inject the other file before the copy creates its destination.

## FS03 — P1: A failed recovery is marked complete and never retried

**Location:** src/oprescue.cpp:945-948 and 850-858.

**Engineer:** run appends a recovered marker even if flagged>0. On the next launch, the recovered branch bypasses all reversers. A complete dirty record has no resumable remainder, so that branch deletes its journal. A temporary restore failure therefore permanently loses automatic recovery and the record tying the parked file to its original path.

**Repro:** `failedRecoveryNoRetry()`. Create a finished dirty Copy-Replace journal with the old original parked. Lock its unfinished destination using macOS UF_IMMUTABLE. First run flags one failure. Remove the lock and the blocking destination, then run again. Output: `FAILED_RECOVERY_FORGOTTEN {"firstFlagged":1,"journalExists":false,"originalStillParked":true,"secondReversed":0}`. The second attempt now could restore, but never tries.

**Plain English:** If automatic repair fails once, fixing the drive or permission problem and reopening the app does not retry it. The original remains stranded and the recovery record is deleted.

**Fix direction:** Track recovered status per operation or only finalize the journal when every unresolved step succeeds. Preserve failed entries and their diagnostics across launches.

## FS04 — P2: Resume forgets remounted-volume resolution after the first recovery

**Location:** src/oprescue.cpp:850-855 and 813-825; first-pass volume resolution is at 888.

**Engineer:** resolveRecord rewrites paths only in the local Record. markRecovered persists no rewritten paths. The recovered branch and pending() call resumableFrom directly on the original on-disk paths and perform no volume resolution. Thus a first pass follows the correct remounted drive, while Resume Later/next launch reverts to its old address. The returned Resumable contains no original volume identities, so dispatch cannot perform that missing original-volume check later.

**Repro:** `recoveredPathsGoStale()`. Journal a pending Delete on volume UUID TEST-VOLUME rooted at old/. Supply the same volume mounted at new/. First run returns new/a.mxf; second run and pending() return old/a.mxf. Output: `RECOVERED_PATHS_REVERT {"firstUsesNewRoot":true,"pendingUsesOldRoot":true,"secondUsesOldRoot":true}`. Volume tables are injected through the existing test API, not physical remounts.

**Plain English:** Resume Later can send a job back to the drive's old address, so a job that initially found the remounted drive can later revert to an obsolete location. This reproduction does not establish deletion on the wrong drive.

**Fix direction:** Resolve original volume identities on every resume offer and again immediately before dispatch. Retain the recorded volume ownership in the resumable request.

## FS05 — P1: Recovery declares an unverified full-size copy finished

**Location:** src/oprescue.cpp:184-200 and 372-379, used at 920.

**Engineer:** An incomplete empty-destination Copy is classified concluded solely because destination size equals the current source size. A crash can happen after the final write but before/during readback verification, so full length is not evidence that its bytes were verified. No hash/identity check runs in this classification. The file is silently omitted from the resume offer.

**Repro:** `unverifiedWholeCopy()`. Write the real begin/plan/op records with no done record and same-length source/destination files differing at payload offset 3 MiB. Recovery leaves the different bytes, flags zero operations, and offers no resume. Output: `UNVERIFIED_WHOLE_COPY_ACCEPTED {"differentBytes":true,"opsFlagged":0,"resumeOffers":0,"sameSize":true}`. This directly represents the persisted state if interrupted before checksum comparison; it is not a forced physical crash test.

**Plain English:** After a crash, a damaged copy can be accepted as finished simply because its length looks right.

**Fix direction:** If source is available, compare it with the candidate before calling it concluded. Otherwise require a durable verified/committed checkpoint or report uncertainty and preserve both files.

## FS06 — P2: Move loses the inner partial-copy rollback failure

**Location:** src/oprunner.cpp:666-670, 691-701 and 704-714; same omission appears at 752-764 and 801-812.

**Engineer:** Cross-volume Move has an outer park for the replaced original and inner partial park for newly written bytes. On cancel/failure the copier restores the inner park, but the runner checks only outer park.isStranded(). When no original was parked, outer restore succeeds even if the inner restore failed. The operation finishes with a clean journal and an unverified destination still present. The inner destructor retries without updating the journal.

**Repro:** `innerMoveRollbackLost()`. Force buffered Move and arm the existing TestPause seam to make the first-chunk progress deterministic; after 4 MiB of an 8 MiB source, lock the destination using UF_IMMUTABLE and cancel. Output: `MOVE_CANCEL_STRANDING_UNJOURNALED {"destinationBytes":4194304,"destinationLeftBehind":true,"failed":0,"journalComplete":true,"journalDirty":false,"sourceBytes":8388608}`. An actually truncated destination survived with no dirty recovery marker. The test unlocks the fixture for cleanup.

**Plain English:** A cancelled move can leave an unfinished or unchecked destination file behind while recording that cleanup finished normally.

**Fix direction:** Consolidate the nested park results into one explicit rollback result and journal every surviving artifact. Disarm both guards only after the final dirty state has been recorded. Test both a parked original and an initially empty destination.

## FS07 — P2: A full relatives group is moved again on every rebalance

**Location:** src/rebalancer.cpp:416-430.

**Engineer:** The >=4999 branch computes free space without subtracting members already in their home folder. A group of exactly 4999 already correctly colocated in a 4999-file folder has slackHome=0, so it allocates a new folder and moves all 4999. The next plan repeats, allocating another new folder. The later normal-group no-op check is unreachable for this size.

**Repro:** `endlessRebalance()`. Stage exactly 4999 same-master files in folder 1. First plan moves all to 2. Apply those renames and rescan-equivalent model updates; second plan moves all to 3. Output: `BALANCED_FULL_GROUP_MOVES_FOREVER {"firstDestination":"2","firstMoves":4999,"folderTarget":4999,"secondDestination":"3","secondMoves":4999}`.

**Plain English:** A folder that is already correctly packed can be reorganized over and over, moving thousands of files and forcing fresh Avid database rebuilds without improving the layout.

**Fix direction:** Perform the already-balanced check before oversized-group handling and account for group members already occupying a candidate folder. Test idempotence: running the planner after applying its plan should return no changes.

## FS08 — P2, dormant feature: Retry of Undo Move-Replace abandons the replaced original

**Location:** src/opundo.cpp:334-343, 494-509, and 202-203. MainWindow kUndoEnabled is false; this is a verified latent engine bug, not a currently available UI action.

**Engineer:** If the moved file gets home but restoring the replaced original from trash fails, the first undo reports failure and retains candidacy. On retry, srcExists short-circuits to already-undone without retrying restoreReplacedOriginal. With no other failures the original journal is stamped undone, permanently retiring the unfinished tail.

**Repro:** `undoReplaceRetryLosesTail()`. Run a real Move-Replace; make the parked original in fallback trash immutable; undo; unlock and undo again. Output: `DORMANT_UNDO_FORGETS_REPLACE_TAIL {"destinationStillMissing":true,"firstFailed":1,"journalUndone":true,"replacedOriginalStillInTrash":true,"secondSkipped":1}`.

**Plain English:** When Undo becomes available, retrying a partially failed undo can claim completion while leaving the file that was replaced in the trash.

**Fix direction:** Model the move-home and replaced-original restoration as separate resumable steps. Only mark an item already undone after checking both postconditions.

## FS09 — P2: Rebalance moves files when its journal cannot be created

**Location:** src/oprunner.cpp:1004-1007 and 1068-1070; src/rebalancer.cpp:637-642. Root reviewed the UI confirmation bypass at rebalancedialog.cpp:785 onward.

**Engineer:** Rename logs a journal-open warning and continues. The Rebalancer path dispatches straight to its private engine, bypassing MainWindow's unavailable-crash-protection confirmation. The resulting rename still succeeds without a journal.

**Repro:** `renameUnavailableJournal()`. Supply a regular file as journalDir, making mkpath/open impossible, and execute Rename. Output: `RENAME_WITHOUT_JOURNAL_SUCCEEDS {"destinationExists":true,"journalCount":0,"sourceExists":false,"succeeded":1}`.

**Plain English:** Rebalance can proceed without its crash-recovery safety net, without the confirmation used for other media operations.

**Fix direction:** Put journal availability/explicit degraded-operation authorization in the shared dispatch layer so Rebalance, resume and normal operations enforce the same rule.

## FS10 — P2: A failure splits the clip relatives Rebalance promises to keep together

**Location:** src/oprunner.cpp:1056-1079; grouping is only used for cancellation at 1038-1043. Root confirmed user-visible promise at rebalancedialog.cpp:584-585.

**Engineer:** groupKey does not provide a transactional execution boundary. One member's occupied destination or rename failure is counted and skipped, while later members of the same group continue moving. Donor-folder preflight does not test individual media locks or destination conflicts.

**Repro:** `relativesSplitOnFailure()`. Video and audio share one groupKey in folder 1; folder 2 already contains video.mxf. Rename refuses video but successfully moves audio. Output: `RELATIVES_SPLIT_ON_FAILURE {"audioMovedToFolder2":true,"failed":1,"succeeded":1,"videoStayedInFolder1":true}`.

**Plain English:** A single failed file move can split a clip's video and audio between folders despite the dialog's assurance.

**Fix direction:** Preflight group destination conflicts and apply a group transaction with rollback/recovery, or explicitly change the product promise and report incomplete groups.

## Smaller verified issues and extraction opportunities

- **Time-dependent tests:** tst_oprescue.cpp:24-28 fixes begin.started to 2026-08-29; production retention at oprescue.cpp:870-874 expires >7-day journals. This explains the three root-observed failures on 2026-09-06: clean_journal_is_kept_as_undo_candidate, superseded_finished_journals_are_pruned, cancelled_run_is_not_resumable_but_is_undo_candidate. Use a controlled clock or derive fresh test timestamps; keep the explicitly aged test separate. This is a test-fixture defect, not a production retention regression.
- **Dead state:** OpRequest::undoesJournalPath (oprequest.h:171) is never read or written anywhere outside its declaration. OpJournal::m_finished (opjournal.h:296) is assigned at opjournal.cpp:247 and never read. TrashRouter::Landing::usedMediaMusterTrash (trashrouter.h:32, trashrouter.cpp:133) is never read by any caller. Repository-wide rg over src/tests proves these current facts.
- **Volume-resolution drift:** resolvePath (oprescue.cpp:737-743) chooses the longest matching recorded root, but production resolveRecord (681-691) separately rewrites the first matching root. resolvePath has no production caller; only its own tests exercise it. Extract one owner-selection/rewrite implementation and have the actual bulk resolver use it. No physical nested-mount failure was tested; report the duplication/inconsistent logic as verified refactoring debt, not a claimed field incident.
- **Rollback abstraction:** ParkedFile is a useful RAII foundation, but failure information is split between its booleans, copier Result, inner/outer guards, dirty journaling, and destructive destructors. FS02/FS03/FS06 demonstrate actual drift. Prefer a typed rollback outcome carrying each surviving path and file identity, consumed once by the journal and UI.
- **Rename policy extraction:** sameVolumeForRename gates forward Move and Undo Move, while Rename, recovery and TrashRouter call QFile::rename directly. Those call sites have different implicit same-volume assumptions. A native no-copy-fallback rename primitive would make the safety contract explicit; no cross-volume filesystem failure was reproduced here, so this is extraction advice, not an extra data-loss finding.
- **Repeated sink test fixture:** tst_oprunner.cpp and tst_opundo.cpp repeat SinkItem, TestSink and error collection. Sharing the recording sink would allow the new deterministic callback fault scenarios without more divergent test scaffolding.

## Root questions checked

- Active journal owner protection **does exist**: oprescue.cpp:881-883 skips ownerStillAlive; the Unix implementation treats kill(pid,0)==0 and EPERM as alive. Do not report that recovery blindly sweeps an active same-host operation. Two activities can still target media belonging to an old journal, but no concurrent startup/new-operation repro was performed.
- **Resume handoff gap, code-derived only:** MainWindow::offerResume removes the old journal immediately after dispatchRequest returns (mainwindow.cpp:2063-2064). OpManager::execute/startRun only starts a QThread; the worker later constructs OpRunner and creates/writes the new journal (opmanager.cpp:94-103; backgroundjob.h:61-68; oprunner.cpp:471-478). A termination after old-journal removal but before the new worker reaches its plan loses the resumable plan. This is a scheduling window established by source ordering, not included among the ten executed repro cases. Retire the old journal only after the new durable plan is acknowledged. The root can include this as a code-path proof if desired.
