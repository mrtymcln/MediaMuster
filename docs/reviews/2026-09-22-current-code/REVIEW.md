# MediaMuster codebase review — 22 September 2026

Originally reviewed clean commit `1007954dcd902eac82c16f57c469b9a04c431aee` without changing application code or existing tests. The dated follow-ups record subsequent implementation; the original findings and captured logs describe pre-fix behavior.

The most useful next work is fixing cancellation and metadata correctness. Broad naming/style changes can follow in separate small commits. The current Qt ownership model, explicit source lists, C++17 baseline and agreed flat source layout do not need wholesale replacement.

Follow-up, 23 September: the user deferred CSV work and confirmed quarantine folders are flat. The nested-quarantine reproduction is outside that supported layout; remove recursion rather than add nested database support. The disconnected MXF master finding is demonstrated with authored inconsistent metadata; no affected real Avid file has been established. The audio timing defect also reproduces through an MXF scan with OMF disabled; see the [follow-up evidence](evidence/parsers/followup-20260923/README.md).

Implementation follow-up, 23 September: the user authorized flat quarantine scanning and the failed-Trash correction. Both are now applied in the working tree, with current scope docs and regression tests updated. Scanner validation: 119 passed, one filesystem-specific skip. File-operation validation: 127 passed; the new changed-source/native-Trash-failure regression was observed failing before the fix. The other findings below remain review proposals, and the original evidence records the pre-fix behavior.

Further implementation follow-up, 23 September: Rebalance now checks its existing cancellation flag before the queued engine start, and MXF parsing rejects a sole material package when its declared graph does not connect to the selected file package. The Rebalance regression reproduces the original failure without sleeps; 128 file-operation checks now pass. Seven MXF ownership/recovery cases were added; 98 parser checks pass. Scanner, MDB, OMF and operation-UI integration suites also pass (the pre-existing external-toolkit and case-sensitive-filesystem skips remain). Existing standalone/graphless recovery is preserved. The [genuine-fixture identity audit](evidence/parsers/identity-audit-20260923/README.md) found no real missing-ID example among 825 MXF fixture paths and 82 OMF/audio files. Audio duration repair and new audio columns remain proposals; this turn changes neither.

Table/CSV follow-up, 23 September: Sample Rate and Bit Depth are now implemented after FPS, with shared kHz formatting, numeric sorting, recorded audio/video depths and blank unknown values. Both CSV modes include the columns. Six affected UI/export suites pass (156 checks, no failures/skips). The duration calculation remains unchanged. The parser safeguard was retained after measuring its cost: net five production lines (all comments), no net executable-line increase, plus 63 regression-test lines; its graph machinery predates the safeguard.

Latest follow-up, 23 September: the disconnected-master recommendation is withdrawn. It was overstated as an actionable Avid bug: only deliberately inconsistent generated metadata reproduced it, and no affected genuine Avid sample was found. At the user's request, the added safeguard and its tests have been removed; MXF parser source/tests are back to the reviewed baseline. The audio duration correction is now implemented: the MDB/OMF descriptor reader preserves the known nominal edit rate before rounding the frame count. Regressions failed before the fix and pass after it for 25 fps and 23.976 fps, including an MDB-backed MXF scan with OMF disabled. MDB validation: 76 passed, one optional external-corpus skip; OMF: 37 passed; scanner: 120 passed, one filesystem-specific skip. Rebalance cancellation, flat quarantine scanning, failed-Trash row retention and the two audio columns remain implemented. The CSV write-error fix remains deferred.

The restored MXF parser and operation-UI integration suites also pass after the final changes (2/2 CTest suites).

## Original priority list and implementation status

Sidebar follow-up: the user accepts the existing volume-refresh selection
behaviour, so U1 is not queued for a change. U5 is fixed: confirmed source removals
now rebuild project totals and filter chips, preserving remaining project
selections and clearing filters for projects which no longer have files. The
status bar already recalculated counts and sizes, but the stale project filter
could leave it showing zero visible files while another project remained.
Operation-UI regressions reproduce the stale sidebar count and the misleading
`0 files (filtered from 1)` result before the fix, and cover partial, whole-project
and complete-inventory removal afterward.

Undo follow-up, 24 September: the shared `refreshEverything()` now refreshes
project totals, filter counts/chips and status totals after inventory changes.
Undo previously restored files on disk without notifying the UI. Operation
results now carry confirmed restored paths, reusing the existing restoration
rescan path; Undo also prunes rows for removed copies or relocated inverse
sources. Regressions cover an empty inventory, another scanned location, Undo
Delete, failed Undo and resumed Undo Copy.

P2 means a substantive correctness issue worth fixing; P3 means a smaller presentation, contract or maintenance issue. Ordering reflects practical impact rather than whether a defect is new. Reproductions use current code and disposable data, not historical audit claims.

| Priority | Finding and location | Suggested fix / missing regression |
| --- | --- | --- |
| P2 | **Rebalance can move files after Cancel during the queued handoff.** [rebalancer.cpp:90](/Users/martymclean/Developer/MediaMuster/src/rebalancer.cpp:90). Reproduced one file moving after cancellation before engine dispatch. | Recheck cancellation in the GUI callback before starting the engine; reject stale generations if restart remains supported. Test precisely between preparation and dispatch. [O1](operations.md#o1--p2-rebalance-can-move-files-after-cancel-before-execution-has-even-started). |
| P2 | **CSV reports success even when zero bytes were written.** [mediacsv.cpp:100](/Users/martymclean/Developer/MediaMuster/src/mediacsv.cpp:100). Stream status is read before the destructor flushes. | Explicitly flush/check errors; use `QSaveFile::commit()` to preserve an existing export on failure. Add write/flush-failure coverage. [Details](scanner-filters.md#verified-findings). |
| P3 | **Quarantine scanning recurses outside the supported flat layout.** [mediascanner.cpp:795](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:795). An artificially nested child inherits root database metadata. | Enumerate only direct files, retaining quarantine flags and warnings. Nested fixtures should verify exclusion. This supersedes the original recommendation to support per-child databases. [Details](scanner-filters.md#verified-findings). |
| P2 | **Failed Trash can hide an edited file that still exists.** [oprunner.cpp:705](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:705). “Changed” becomes `sourceRemoved=true`. | Separate uncertain identity/metadata from proven removal. Retain the row and report the uncertain result. Test in-place editing during a failed native Trash call. [O2](operations.md#o2--p2-a-failed-trash-attempt-can-hide-a-file-which-still-exists). |
| P2 | **MDB-backed audio can get the wrong duration timecode rate, including MXF with OMF disabled.** [omfobjects.cpp:797](/Users/martymclean/Developer/MediaMuster/src/omfobjects.cpp:797). A recorded 25 fps rate becomes base 26 after reconstructing it from rounded frames. | Carry the known rate directly into metadata. Test short and non-frame-aligned sample counts. [Parser finding 1](parsers.md#reproduced-bugs). |
| P2 | **Rejected Rebalance preparation reports “0 moved, 0 failed.”** [rebalancer.cpp:71](/Users/martymclean/Developer/MediaMuster/src/rebalancer.cpp:71). A vanished media root produces an empty request which completes normally. | Return a preparation error and emit `aborted`; distinguish a valid empty plan from a rejected nonempty one. [O4](operations.md#o4--p2-rejected-rebalance-preparation-reports-success-with-zero-failures). |
| P2 | **Cancelled/failed Rebalance shows the complete projected folder counts.** [rebalancedialog.cpp:934](/Users/martymclean/Developer/MediaMuster/src/rebalancedialog.cpp:934). Immediate cancellation reports zero moves while folder counts still change. | Drive counts from confirmed per-file outcomes or a rescan. Do not interpret an upcoming item index as completion or force projected counts after cancellation. [O3](operations.md#o3--p2-cancelledfailed-rebalance-shows-the-fully-completed-plan-in-folder-cards). |
| P2 | **Volume refresh and manual additions lose selection.** [mainwindow.cpp:1395](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1395), [1432](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1432). `setSelected()` runs before list insertion. | Insert before selecting; also distinguish newly discovered volumes from existing deliberately deselected volumes. [U1](ui-build-docs.md#u1--p2-volume-selection-is-lost-on-insertion-and-refresh). |
| P2 | **Log migration deletes old history and crash reports.** [logfile.cpp:124](/Users/martymclean/Developer/MediaMuster/src/logfile.cpp:124). The old directory is deleted even when the log was not migrated. | Check migration results and preserve collisions; remove only an empty old directory. [U2](ui-build-docs.md#u2--p2-log-migration-deletes-history-without-preserving-it). |
| P2 | **An impossible configured date disables beta expiry.** [CMakeLists.txt:58](/Users/martymclean/Developer/MediaMuster/CMakeLists.txt:58), [main.cpp:37](/Users/martymclean/Developer/MediaMuster/src/main.cpp:37). `2026-02-31` configures successfully but never expires. | Validate the complete calendar date and reject invalid compiled dates. The checked-in default is valid. [U3](ui-build-docs.md#u3--p2-impossible-expiry-dates-silently-disable-the-expiry). |

## Smaller correctness and presentation fixes

| Priority | Finding | Suggested change |
| --- | --- | --- |
| P3 | [mediatablemodel.cpp:11](/Users/martymclean/Developer/MediaMuster/src/mediatablemodel.cpp:11): valid parents incorrectly have table-sized row/column counts; Qt's model tester reports failures. | Return zero for valid parents; add `QAbstractItemModelTester` to model mutation tests. No current visible table crash was established. |
| P3 | [mediafilterproxy.cpp:273](/Users/martymclean/Developer/MediaMuster/src/mediafilterproxy.cpp:273): sorting puts displayed DF `00;10;00;00` before NDF `00:09:59:20`. | Decide whether order means displayed timecode or actual running time, then use one consistent numeric representation. The comment currently promises displayed order. |
| P3 | [mainwindow.cpp:856](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:856): project rows and tooltip totals remain after Move/Delete removes files. | Rebuild project statistics and associated chips after source removal. [U5](ui-build-docs.md#u5--p3-project-sidebar-totals-outlive-removed-media). |
| P3 | [revealinfinder.cpp:42](/Users/martymclean/Developer/MediaMuster/src/revealinfinder.cpp:42): a launched but failing command prevents the advertised fallback. | Observe asynchronous process completion, not just successful launch. Live Finder/network failure was not induced. [U4](ui-build-docs.md#u4--p3-the-macos-reveal-fallback-observes-process-launch-not-success). |
| P3 | [fileoperationcontroller.cpp:69](/Users/martymclean/Developer/MediaMuster/src/fileoperationcontroller.cpp:69): declining Trash fallback logs “Copied; source retained” despite no copy. | Use “Source retained,” or distinguish a published copy from a retained-only outcome. [O5](operations.md#o5--p3-declined-trash-fallback-is-logged-as-a-successful-copy). |

Two additional items need a narrower follow-up: Windows discovery skips the actual system drive but adds literal `C:/` locations, omitting boot-root media on non-C: installations; and `PathKey` promises Unicode equivalence its leaf keys do not provide. The former is established by static Windows-branch inspection, not a Windows run. The latter is reproduced on APFS, but its full operation-level consequences were not established. See [platform/key notes](scanner-filters.md#platform-issue-established-by-code-not-windows-execution); do not blindly normalize paths on filesystems that distinguish Unicode forms.

## Dead code and redundant state

- [BentoFile setters](/Users/martymclean/Developer/MediaMuster/src/bentofile.h:59): remove unused `setMetadataBigEndian` and `setOmf2References`. Reader initialization already determines the state.
- [Bento handle readers](/Users/martymclean/Developer/MediaMuster/src/bentofile.h:65): retire context-free `handleValue`/`handlesValue` after moving useful malformed-reference tests onto production `ref`/`refs` APIs.
- [Rebalance prefix election](/Users/martymclean/Developer/MediaMuster/src/rebalanceplanner.cpp:250): remove the histogram/sort; grouping already includes the workstation prefix, so valid groups contain one prefix.
- [Oversized-group home branch](/Users/martymclean/Developer/MediaMuster/src/rebalanceplanner.cpp:380): simplify the unreachable whole-group-fit condition, or deliberately redesign packing if preserving home occupancy is desired.
- [MediaFilterProxy::m_search](/Users/martymclean/Developer/MediaMuster/src/mediafilterproxy.h:76): keep one normalized search string; the extra original string only provides `isEmpty()`.
- [MediaFile::effectInstance](/Users/martymclean/Developer/MediaMuster/src/mediafile.h:83) and `extension`: remove redundant stored inventory state where there is no actual UI/export/operation consumer. Preserve local parsing needed to derive the visible effect name.
- [BinFilterDialog::m_btnDone](/Users/martymclean/Developer/MediaMuster/src/binfilterdialog.h:167): make this a local parent-owned widget pointer; no later member access needs it.
- [Rebalancer::aborted](/Users/martymclean/Developer/MediaMuster/src/rebalancer.h:53) has no producer. Restore it for rejected preparation rather than deleting the error path.

The detailed reports also identify a duplicate MXF property row, an unused final `seenPaths` insertion and private CSV helper linkage. Retained journal provenance and historical evidence are not automatically dead code.

## Names worth changing

| Current | Proposed | Why |
| --- | --- | --- |
| `EffectFilterDialog`, `effectfilterdialog.*`, associated UI members | `PrecomputeFilterDialog`, `precomputefilterdialog.*` | The dialog selects precompute branches and volume; filter types and UI already use this term. |
| `MediaFile::mobId` | `fileMobId` | Makes file/master identity unambiguous and matches parser naming. |
| `MediaFile::omfEra` | `mediaFamily` using the shared enum, or `isOmfMediaFamily` | It means a managed-layout family, not the age of the media. |
| `MediaFile::volumePath` | `sourceLocationPath` or `scanBasePath` | It can be an added media-tree/search base rather than a physical mount root. |
| `ScanTask::folderNumber` | `mediaFolderName` | Values also include workstation names, OMFI roots and quarantine names. |
| `FolderName::n`, `FolderName` | `number`, `MxfFolderId` | It represents a parsed MXF workstation/number pair. |
| `RenameOp::dest` | `RebalanceMove::destinationFolder` | This changes directories and stores a folder identifier, not a full file destination path. |
| `OpStamp::size`, `modified` | `sizeBytes`, `nativeModifiedTime` | Exposes byte units and the deliberately platform-specific timestamp. |
| `RebalancePlan::totalFiles()` | `moveCount()` | Returns planned operations, not the total scanned inventory. |
| `MdbMasterMob::bin` | `originalBin` | Recorded origin is distinct from current bin membership. |
| `MediaFile::Kind`, `Type` | `EssenceKind`, `ClipClassification` | Distinguishes audio/video from media/precompute classification. |
| `MainWindow::openManageMedia(int)` | Accept the operation enum directly | Removes enum-to-int-to-enum conversion and invalid values. |

More role-specific parser names and string-to-enum opportunities are in [parsers](parsers.md#naming-and-structure-recommendations) and [operations](operations.md#naming-suggestions-with-concrete-value). Preserve persisted schema strings unless deliberately changing the journal contract. Keep established domain terms such as MOB, PMR, MDB, Bento and `RevealInFinder`.

## Comments and docs

The detailed reports contain exact locations and suggested corrections. The most misleading are:

- MDB comments equate file usage code 0 with ordinary media even though the retained evidence includes precomputes with that file code. Master usage determines classification.
- PMR comments claim version 2 omits project data; only version 1 omits that stored field.
- OMF audio comments promise an exact rate reconstruction from rounded frames; that is the reproduced timing bug.
- Scanner comments say “first non-empty wins” while claiming source order is irrelevant, and wrongly limit MDB clip-name use to header-read failures.
- `findSourceMob` promises the first match but actually requires a unique source.
- `readDuration` promises more than eight bytes while the implementation rejects them.
- Recovery docs still name Cancel Job and the previous dialog introduction; the UI now uses Stop.
- The accessibility log filter claims to drop exactly two messages but drops an entire logging category.
- The contributor-guide link in `docs/file-operations-native-api-validation.md:72` points one directory too high.

Keep historical reports, old validation totals and archived checksum evidence clearly labeled as historical. Do not rewrite captured evidence to resemble today's code. Current architecture and behavior docs are useful and generally already distinguish those records correctly.

## Validation and scope

- Reviewed all 115 files under `src/`, including headers, comments, platform code and generated-data integration; reviewed current documentation, CI/build scripts and relevant test coverage. Binary fixtures and archived investigations were treated as regression/evidence assets, not independently re-certified byte by byte. Long test suites received scenario and relevant assertion review, not an independent audit of every assertion.
- **27/27 CTest suites passed** in **38.64 seconds**. Three cases skipped: two optional external-toolkit corpora and one case-sensitive-filesystem scenario. Passing existing tests does not cover the new reproductions.
- **67/67 headers compiled standalone** against C++17 / Qt 6.5.3 on macOS.
- All **887 effect catalogue rows and translated aliases** match the retained catalogue evidence.
- App/test compilation and linking completed, but post-build app signing failed: **“A timestamp was expected but was not found.”** This was not a successful signed-app build. The build log is retained.
- New probes demonstrate the cancellation, Trash, CSV, quarantine, parser, selection, logging, date and model issues. Rebalance's false post-cancel counts also have before/after images.
- Windows runtime, real NEXIS/network faults, release packaging/notarization and live accessibility were not retested.

Detailed reports: [operations](operations.md), [scanner/filter/model/export](scanner-filters.md), [parsers](parsers.md), [UI/build/docs](ui-build-docs.md). Reproduction notes and logs are in [evidence](evidence/README.md).
