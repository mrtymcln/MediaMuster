# UI, build and test review — 6 September 2026

These findings refer to the unchanged source on disk at commit `7fcfe7b24afa96069a04a07d119ff68138934905`. They supplement the file-operation, parser and scanner reviews. P2 means a correctness or reliability issue to fix; P3 means a smaller defect or maintenance problem.

The [UI probe](evidence/ui_probe.cpp) links the freshly built production objects, excluding the application entry point. It uses `MainWindow::StartupMode::UiOnly`, Qt's offscreen platform, and `-fno-access-control` to inspect internal state. Rebalance's implementation is included in that probe to inspect its folder cards. This does not modify production code. The [output](evidence/ui_probe.log) records real Qt widget/model behavior. Synthetic completion signals and sizes are identified below; they establish UI behavior, not successful execution of a filesystem operation. An independent reviewer also checked the production call paths: [audit](evidence/scanner_filters/independent_audit.md).

## UI01 — P2: adding or refreshing volumes loses their selection

**Source:** [mainwindow.cpp:1397](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1397), [mainwindow.cpp:1434](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1434).

**Engineer:** Both `addVolumePath()` and `rebuildVolumeList()` call `QListWidgetItem::setSelected()` before adding the new item to the list. The detached item has no list selection model, so the selection request has no effect. This breaks initial automatic selection, preservation of existing selections, and selection of manually added folders.

**Proof:** With a synthetic detected Avid volume, the actual `rebuildVolumeList()` leaves zero selected rows. Manually selecting its attached item and rebuilding again leaves zero selected rows. Calling the actual `addVolumePath()` for an existing temporary directory also leaves zero selected rows. The three `VOLUME` lines in the probe show `selected=0 expected=1`.

**Plain English:** The app forgets which drives you selected when the list refreshes, and a folder you add is not automatically selected for scanning.

**Fix:** Attach the item before selecting it. Also separate “previously known” from “previously selected” volumes: the current `newAvidVolume` expression uses only selection history, so it cannot distinguish an old deliberately deselected drive from a newly mounted drive. That second point is a code-derived issue to address alongside the proven ordering defect, not a claim that the current broken selection path reselects drives.

## UI02 — P2: cancelled or failed Rebalance shows planned counts as actual counts

**Source:** [rebalancedialog.cpp:916](/Users/martymclean/Developer/MediaMuster/src/rebalancedialog.cpp:916), [rebalancedialog.cpp:318](/Users/martymclean/Developer/MediaMuster/src/rebalancedialog.cpp:318).

**Engineer:** `onFinished()` receives succeeded, failed and cancelled, but calls `markFinished()` on every card regardless. `markFinished()` assigns `m_currentCount = m_projectedCount`. Consequently the cards display the fully applied plan even after cancellation before the first move. The summary also takes affected/new folder counts from the plan.

**Proof:** Build the real dialog's existing Small demo plan, prime its live state, and invoke `onFinished(0, 0, true)`. A source card changes from 3,380 files to its projected 3,333, despite zero successful moves: `REBALANCE cancelled moved=0 source-before=3380 projected=3333 displayed-after=3333`. This intentionally simulates the completion callback; it does not physically move 47 files.

**Plain English:** After you cancel, the folder diagram can say the reorganization happened even though none of the files moved. Its numbers disagree with “0 moved.”

**Fix:** Update counts from acknowledged successful operations, or rescan the affected folders. Keep planned values visibly separate until actual results are known. Do not derive completed counts from the attempted-operation progress counter.

## UI03 — P2: skipped conflicts still block Copy for insufficient space

**Source:** [managemediadialog.cpp:616](/Users/martymclean/Developer/MediaMuster/src/managemediadialog.cpp:616), [managemediadialog.cpp:306](/Users/martymclean/Developer/MediaMuster/src/managemediadialog.cpp:306).

**Engineer:** The capacity gate begins with all selected bytes, and its Move adjustment still iterates all files. It never excludes `ConflictPolicy::Skip`. Conflict-policy changes update row previews without recalculating the summary/capacity decision. Thus an intentionally skipped file can disable execution of the remaining work.

**Proof:** Supply two model rows: a conflicting file whose recorded size is destination free space plus one byte, and a nonconflicting one-byte file. The destination conflict is a real temporary file; the huge source size is synthetic metadata, not an allocated huge file. After selecting global Skip, the actual dialog returns Skip for the conflict but its execute button remains disabled. Output: `SPACE-SKIP policy=1 execute-enabled=0 remaining-required=1`, with hundreds of gigabytes available.

**Plain English:** You can skip the big file to make a copy fit, but the app still refuses to copy the small file that fits easily.

**Fix:** Derive required capacity from the effective operation plan after conflict policies. Recalculate when policies change. Use the same plan for preview, totals and execution, with a final execution-time conflict check.

## UI04 — P2: log migration deletes unmigrated history and crash reports

**Source:** [logfile.cpp:123](/Users/martymclean/Developer/MediaMuster/src/logfile.cpp:123).

**Engineer:** The old log is renamed only if the new log does not exist, and that rename's result is ignored. The entire old `logs/` directory is then removed recursively regardless. This deletes unique old history when a current log already exists, and also deletes other files in the old directory even when the log migration succeeds.

**Proof:** The probe creates a process-unique QStandardPaths test application directory, with a current log, an old log containing unique history, and an old `.ips` file containing unique crash information. It runs the actual `AppLog::install()`. Both old files are gone: `LOG-MIGRATION old-history-exists=0 crash-report-exists=0`. Only that disposable test directory is touched.

**Plain English:** Starting the app can erase diagnostic information that was supposed to be preserved during an upgrade. This is loss of logs/crash evidence, not loss of MXF media.

**Fix:** Preserve or merge the old log when the new one exists. Check migration success, move known crash reports individually, and remove the old directory only when empty. A recursive delete is too broad for a one-file migration.

## UI05 — P3: project names and totals remain stale after Move/Delete

**Source:** [mainwindow.cpp:933](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:933), [mainwindow.cpp:1587](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1587).

**Engineer:** Operation completion removes successful paths from the model and updates filter/status counts. It does not rebuild project aggregation. The sidebar's project names and file/byte tooltips are rebuilt by `onScanFinished()` only.

**Proof:** Feed a one-row project to `onScanFinished()`, then deliver the actual connected operation-completion signal with that path marked successful. The model has zero rows, but the sidebar retains one project with tooltip `1 files, 1 B`: `PROJECT rows-after-remove=0 sidebar-projects=1`. This exercises the UI completion path without deleting a real media file.

**Plain English:** After moving or deleting a project's last file, the sidebar still says the project has media until you scan again. Partial operations also leave old totals.

**Fix:** Centralize project aggregation from current model contents and refresh it after relevant model changes. Store project identity separately from the display label, addressing SF3 in the same helper.

## UI06 — P3: destination preview repeats names that execution must disambiguate

**Source:** [managemediadialog.cpp:446](/Users/martymclean/Developer/MediaMuster/src/managemediadialog.cpp:446). Compare destination reservation in [oprunner.cpp:350](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:350), `claimDestination()`.

**Engineer:** Rename previews call `generateRenamePath()` independently for each row. They check existing disk names but do not reserve names already chosen for other rows in this batch. The operation runner separately tracks claimed destinations, so its choices can differ from the preview.

**Proof:** Three selected rows have the basename `duplicate.mxf` and distinct source directories. An empty real destination produces `duplicate.mxf`, `duplicate (2).mxf`, `duplicate (2).mxf` in the actual preview. The runner's reservation logic prevents treating those duplicate preview names as the final execution plan. The probe demonstrates an inaccurate preview, not an overwrite by the runner.

**Plain English:** The preview promises the same destination filename to two files. Actual filenames must differ, so you cannot fully trust the preview of where your files will go.

**Fix:** Extract a destination planner that accepts both filesystem occupancy and a set of names reserved earlier in the plan. Recheck conditions at execution while reporting any changed decision.

## BT01 — P2: three recovery tests fail as their fixed date ages

**Source:** [tst_oprescue.cpp:24](/Users/martymclean/Developer/MediaMuster/tests/tst_oprescue.cpp:24), retention at [oprescue.cpp:870](/Users/martymclean/Developer/MediaMuster/src/oprescue.cpp:870).

**Engineer:** The common fixture's start timestamp is fixed at 29 August 2026. Production intentionally prunes completed undo candidates older than seven days. By this review date, three cases expecting a fresh retained candidate instead exercise aged retention.

**Proof:** The clean C++17 build succeeds, but [CTest output](evidence/ctest.log) records failures at test lines 238, 255 and 759: `clean_journal_is_kept_as_undo_candidate`, `superseded_finished_journals_are_pruned`, and `cancelled_run_is_not_resumable_but_is_undo_candidate`. All three use that shared timestamp and the retention branch. `tst_oprescue` has 39 passing cases and three failures; all other 30 CTest executables pass.

**Plain English:** The test suite has started failing because the calendar advanced. These failures do not establish a bug in the app's intended seven-day retention policy.

**Fix:** Use a controlled clock, or fresh timestamps for fresh-journal cases and explicitly old timestamps only for aging tests. Do not extend production retention to make these fixtures pass.

## BT02 — P3: failed fixture writes can make malformed-input tests pass accidentally

**Source:** [tst_mxfparser.cpp:130](/Users/martymclean/Developer/MediaMuster/tests/tst_mxfparser.cpp:130), [tst_mdbparser.cpp:29](/Users/martymclean/Developer/MediaMuster/tests/tst_mdbparser.cpp:29), [tst_pmrparser.cpp:192](/Users/martymclean/Developer/MediaMuster/tests/tst_pmrparser.cpp:192).

**Engineer:** These helpers ignore failed open/short write and return the intended path anyway. A negative parser test that checks only invalid/empty output can then succeed because the file is missing, without testing the intended malformed bytes.

**Proof:** Each helper's return path is unconditional. PMR's missing-file case at lines 428–430 expects empty/not-ok, the same broad outcome used by its wrong-magic test at lines 470–480. Therefore the wrong-magic test's assertions do not distinguish a failed fixture creation from a successfully created wrong-magic file. This is a source-derived test weakness; normal test-fixture creation succeeded during this review.

**Plain English:** Some tests can show green even when they never created the file they meant to test.

**Fix:** Share a checked fixture writer that verifies open, exact write count and close/flush as appropriate, and makes failure fail the test immediately. Reuse the existing checked `tryWriteFile` approach where applicable.

## BT03 — P3: three test names promise checks their assertions do not perform

**Engineer and proof:** These are narrow, source-verifiable coverage gaps, not additional parser failures:

- [tst_mxfparser.cpp:815](/Users/martymclean/Developer/MediaMuster/tests/tst_mxfparser.cpp:815): the corpus “no unknowns” test rejects only empty codecs and `unknown variant`. The separate test at [line 367](/Users/martymclean/Developer/MediaMuster/tests/tst_mxfparser.cpp:367) establishes another fallback, `Unknown (hex)`, which the corpus predicate accepts. There is no claim that the current corpus contains this fallback.
- [tst_mdbparser.cpp:1368](/Users/martymclean/Developer/MediaMuster/tests/tst_mdbparser.cpp:1368): “TIFF respects own byte order” uses the same `big` variable for both the OMF container and TIFF payload. It exercises little/little and big/big, but never opposite byte orders. It cannot distinguish a parser that incorrectly uses container byte order for TIFF.
- [tst_pmrparser.cpp:759](/Users/martymclean/Developer/MediaMuster/tests/tst_pmrparser.cpp:759): the OMF audio fixture case's comment says filename lookup is checked, but the assertions check construction and `index.size()==2`; this case never performs a keyed lookup.

**Plain English:** These tests check less than their names or comments suggest. They give useful coverage, but leave specific mistakes undetected.

**Fix:** Reject both unknown-codec forms, independently vary the two byte orders, and actually look up each expected filename. Also keep the negative OMF identity case from P01 separate from tests that construct expected IDs with the production conversion helper.

## BT04 — P3: an impossible expiry date passes configuration and disables expiry

**Source:** [CMakeLists.txt:43](/Users/martymclean/Developer/MediaMuster/CMakeLists.txt:43), [main.cpp:36](/Users/martymclean/Developer/MediaMuster/src/main.cpp:36).

**Engineer:** CMake validates only date shape, month 1–12 and day 1–31. It accepts an impossible calendar date. At runtime `QDate::fromString()` returns invalid, and `expiry.isValid() && ...` prevents expiry from being enforced.

**Proof:** A separate configure with `-DSELF_DESTRUCT=ON -DSELF_DESTRUCT_DATE=2027-02-31` succeeds and prints that it expires on that date: [configure log](evidence/invalid-expiry.log). The actual Qt date parser returns invalid in the UI probe: `EXPIRY configured=2027-02-31 QDate-valid=0`.

**Plain English:** A typo such as February 31 silently creates a beta that never expires, despite the build saying expiry is enabled. The current default date is valid, so this is conditional on an invalid supplied date.

**Fix:** Validate a real Gregorian date when configuring, or fail explicitly at startup if expiry is enabled with an invalid date. This does not require changing the C++17 language standard.

## BT05 — P3: Python optimization removes extractor input-integrity checks

**Source:** [extract.py:83](/Users/martymclean/Developer/MediaMuster/tools/avid_effects/extract.py:83), [extract.py:112](/Users/martymclean/Developer/MediaMuster/tools/avid_effects/extract.py:112).

**Engineer:** The pinned binary digest check and supplied-disassembly byte check use Python `assert`. They validate external inputs rather than programmer-only invariants, but Python removes them under `-O`, `-OO`, or corresponding optimization settings.

**Proof:** [tooling_probe.py](evidence/tooling_probe.py) compiles the exact extractor source at optimization levels 0, 1 and 2 and inspects all nested code objects. [Output](evidence/tooling_probe.log): eight assertion-error instructions at level 0; zero at levels 1 and 2. AST locations include both input checks. The extractor was not run against altered Avid binaries; no claim is made that every changed binary would otherwise extract successfully.

**Plain English:** An optimized Python run can bypass the checks intended to stop the effect catalogue being generated from the wrong binary or stale disassembly.

**Fix:** Use explicit conditional checks with a clear exception or fatal error for external-input validation. Reserve `assert` for internal conditions whose removal is acceptable.

## Verified product limitation: macOS item rows are hidden from accessibility

**Source:** [macaccessibilityguard.cpp:11](/Users/martymclean/Developer/MediaMuster/src/macaccessibilityguard.cpp:11), [main.cpp:28](/Users/martymclean/Developer/MediaMuster/src/main.cpp:28), exact Qt pin at [CMakeLists.txt:74](/Users/martymclean/Developer/MediaMuster/CMakeLists.txt:74).

This is an intentional workaround with a material product cost, not an accidental omission. On the pinned macOS Qt 6.5.3 build, the installed factory replaces list/table/tree accessibility interfaces with a childless widget. The real probe creates a list with one row and obtains `accessible-children=0 table-interface=0`. The same factory covers the media table and other item views. Screen readers cannot enumerate their rows through those interfaces.

The code says the workaround addresses a Qt bridge issue; this review did not reproduce that original issue or prove a safe framework upgrade. Do not simply remove the guard. Validate a fixed supported Qt version or a narrower workaround, then verify row navigation and selection with VoiceOver. Windows does not enter this compile-time branch. This limitation is excluded from the accidental-defect count.

## FS12 — P2: Resume deletes its old journal before the replacement plan is durable

**Source:** [mainwindow.cpp:2063](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:2063), [opmanager.cpp:94](/Users/martymclean/Developer/MediaMuster/src/opmanager.cpp:94), [backgroundjob.h:61](/Users/martymclean/Developer/MediaMuster/src/backgroundjob.h:61), [oprunner.cpp:471](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:471).

**Engineer:** `offerResume()` removes the old journal after `dispatchRequest()` returns true. Dispatch starts a worker thread; it does not await the new durable journal/plan. The worker constructs the new journal later. There is therefore a permissible schedule in which the old journal is deleted before the new worker writes its plan.

**Proof category: source ordering, not a killed-process reproduction.** The foreground's remove follows thread start, while the replacement journal write belongs to that worker. No synchronization or durable-plan acknowledgement orders the new write before the remove. Termination in that interval loses the saved resume plan. This finding does not claim completed media was deleted in that interval.

**Plain English:** If the app crashes or is forcibly terminated just as you resume a job, it can forget the unfinished job because the old recovery record is already gone and the new one has not been saved yet. Graceful shutdown joins the worker and is not the failure schedule demonstrated here.

**Fix:** Have the worker acknowledge durable acceptance of the replacement plan before retiring the old journal, with an idempotent handoff marker for interruptions between those steps.
