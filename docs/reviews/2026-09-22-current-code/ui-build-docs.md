# Main window, utilities, build and documentation review

Reviewed clean commit `1007954` on 22 September 2026. No application code was changed.

## U1 — P2: volume selection is lost on insertion and refresh

Locations: `src/mainwindow.cpp:1395`, `src/mainwindow.cpp:1432`.

Both paths call `QListWidgetItem::setSelected()` before adding the item to the list. At that point it has no list selection model, so the selection request has no effect. Startup does not select Avid volumes; manually added folders do not become selected; refreshing or hot-mount reconstruction forgets existing selections. Scan Selected becomes disabled until the user selects again.

A UI-only check avoided host volume discovery and journal recovery. Both initial volumes were unselected, Scan Selected was disabled, and refresh and manual additions also produced no selected volumes.

Add each item before selecting it. Also fix the selection policy in the same small change: `newAvidVolume` currently means any unselected Avid volume, not a newly discovered volume. Snapshot all previously listed paths separately from selected paths, and represent initial population explicitly. Otherwise moving `addItem()` alone will cause refresh to reselect deliberately deselected volumes. Preserve the unselected state of existing manual entries too. Cover initial population, selected and deselected existing volumes, a newly mounted volume, and manual additions.

Confidence: high, reproduced.

## U2 — P2: log migration deletes history without preserving it

Location: `src/logfile.cpp:124`.

The old log is renamed only when the new destination does not exist; the entire old `logs/` directory is then removed recursively regardless of whether the rename ran or succeeded. A machine containing both paths loses the old log, and any crash reports in the old directory are also deleted. This contradicts the adjacent “keeping history” comment.

An isolated application-data test created a current log, a distinct old log, and an old crash report. After `install()`, both old files were gone and the current log did not contain the old history. The probe used a unique application name in Qt's test data directory and removed only its own data afterward.

Migrate known files with checked results and a collision policy that preserves both. Remove the old directory only if it is empty. Keep the old files when migration fails. Add install/migration tests; the current logger tests cover message formatting only.

Confidence: high, reproduced.

## U3 — P2: impossible expiry dates silently disable the expiry

Locations: `CMakeLists.txt:58`, `src/main.cpp:37`.

CMake checks month 1–12 and day 1–31 independently. It accepts `2026-02-31`, prints an enabled expiry, and successfully generates the project. Qt parses that date as invalid, and the `expiry.isValid() && ...` branch then never expires that build. This affects custom invalid dates, not the valid checked-in default.

Validate calendar month lengths, leap years and a valid year at configuration time. A build explicitly configured to expire should also reject an invalid compiled date at startup rather than silently omitting the restriction. Add valid leap-day and invalid-date configuration cases.

Confidence: high, reproduced configuration and date parsing.

## U4 — P3: the macOS reveal fallback observes process launch, not success

Location: `src/revealinfinder.cpp:42`.

`QProcess::startDetached("open", ...)` returning true establishes that the command launched; it does not establish that `open -R` revealed the file. The function immediately returns, so a launched command that exits unsuccessfully cannot trigger the advertised AppleScript or parent-folder fallback. The AppleScript tier has the same issue. A probe with `/usr/bin/false` returns true from `startDetached`, confirming the API distinction. A live Finder/network failure was not induced.

Use an asynchronously owned `QProcess`, inspect its exit result and run the next tier on failure, with a bounded timeout. Alternatively document and deliberately accept launch-only behavior, but the existing claim that this recovers network reveal failures is unsupported by the control flow. Retain the agreed cross-platform name `RevealInFinder`.

Confidence: high for the fallback defect; field occurrence unmeasured.

Follow-up, 28 September: replaced the macOS command/AppleScript chain with
`NSWorkspace`'s `selectFile:inFileViewerRootedAtPath:`. At the user's request,
macOS no longer falls back to opening the parent; a native failure is logged.
Windows is unchanged. The app builds and the operation-UI suite passes. Live
Finder checks selected ordinary and quoted/Unicode filenames; a missing file
logged failure without opening its parent. Network and Windows execution were
not tested.

## U5 — P3: project sidebar totals outlive removed media

Location: `src/mainwindow.cpp:856`.

The handler updates the model, tab counts and status bar, but never rebuilds the project list. Moving/deleting a project's final file leaves a project row whose tooltip still describes removed media. Partial removals also leave stale counts and byte totals. The probe begins with one project/one file; after the source-removal signal, the table has zero rows and the sidebar still has one project.

Rebuild project statistics after source removal, preserving remaining project selections and refreshing the chips when selected projects disappear. Test both partial and complete project removal. This restores the whole-inventory promise in `docs/current-behaviour.md:169–171` and `docs/architecture.md`.

Confidence: high, reproduced.

## Comments and documentation

| Location | Suggested correction |
| --- | --- |
| `src/managemediadialog.h:26` | “Drives OpManager” predates the controller boundary. It collects choices; MainWindow/controller dispatch the operation. |
| `src/managemediadialog.h:60` | An unapproved late conflict fails the item; it is not the explicit Skip outcome. Match the current runner and behavior guide. |
| `src/managemediadialog.cpp:289` | Remove the obsolete Delete-key shortcut example; MainWindow explicitly has no Delete shortcut. |
| `src/mainwindow.cpp:490` | Auto-fit runs after every scan completion, not just once after the first scan. |
| `src/mainwindow.cpp:1700` | Remove the unsupported `O(N × log N)` claim for `mapToSource`; explain the real reason for debouncing: walking the selected rows. |
| `src/mainwindow.cpp:1991` | Coalescing reduces selection ranges; it does not emit one Qt signal per range. The caller submits a whole `QItemSelection` in one call. The analogous helper comment also needs narrowing. |
| `src/icons.h:10` | Path probing occurs only when `volumeType` is empty, not for every non-network type. |
| `src/main.cpp:23` | Correct “needes” to “needs,” or state that app metadata must be installed before the log path is chosen. |
| `docs/operation-recovery-cleanup.md:35` | Its current-looking recovery description still names Cancel Job and says there is no introduction. The dialog now says Stop and asks “Resume the interrupted job?”. Update the front description or mark it historical explicitly. |
| `docs/file-operations-native-api-validation.md:72` | The contributor-guide link is broken: replace `../CONTRIBUTING.md` with `CONTRIBUTING.md`. This was the only nonexistent relative file target detected in the top-level Markdown documents. |

Historical review reports are already marked as historical. Their removed checksum features, old filenames, old test totals and preserved captured paths are not automatically documentation defects. Keep recorded evidence intact; correct current guides and current-looking descriptions separately. A small root README linking `docs/README.md` would make the repository easier to enter; this is optional discoverability work.

## Names and types worth improving

- `MainWindow::openManageMedia(int)` (`src/mainwindow.h:117`): accept the operation enum directly. The current call sites turn an enum into an integer only to cast it back. This removes an unnecessary invalid-value path.
- `onFilterByEffects`, `m_btnEffectFilter`, `m_effectFilterAct` and their object names: rename with the proposed `PrecomputeFilterDialog` so UI code, filter types and visible terminology agree. Do not rename the real effect category/name fields; those remain meaningful subfields.
- `SELF_DESTRUCT` (`CMakeLists.txt:51`): `BETA_EXPIRY_ENABLED` and `BETA_EXPIRY_DATE` describe what the options actually do. Coordinate cache/CI/docs names if adopted; this is separate from U3.
- `m_inFilterRestore`: use `m_restoringSelection` and a scoped rollback guard if touching this area, so nested preservation calls restore the incoming state. No currently exercised nested-call failure was established, so this is maintenance advice rather than a bug finding.

No broad replacement of parent-owned Qt pointers with smart pointers is recommended. The intentional process-lifetime logger allocation and worker join ordering have stated ownership reasons. Keep those reasons and test their behavior rather than applying a mechanical “no new/delete” checklist.

## Validation and coverage

- Reviewed the remaining production main-window/dialog, formatting, logging, crash collection, reveal, layout, icon, enum, feature/version/resource and worker helpers; checked application/test CMake source lists and all six CI CMake recipes plus the workflow.
- Cross-checked current behavior, architecture, release gates, contributor guide, documentation indexes and status banners in historical audits. Test inspection checked relevant scenarios and gaps; it was not an independent audit of every assertion or every binary fixture byte.
- `cmake --build build -j 6` compiled and linked the app and tests. The app's post-build Developer ID signing failed with `A timestamp was expected but was not found`. No compiler diagnostic was recorded. Do not call this a successful signed application build.
- `ctest --test-dir build -C Debug --parallel 1 --output-on-failure`: all 27 suites passed in 38.64 seconds. Optional external-corpus cases may skip within passing suites.
- All 67 public source headers compiled standalone as C++17 against the installed macOS Qt 6.5.3 headers. This establishes Mac include self-containment, not Windows branch compilation.
- Focused validation used the reviewed source or compiled application objects and disposable data. No production code, existing tests, user media, or historical reports were edited.

Actual Windows execution, network/NEXIS fault behavior, release packaging/notarization, live VoiceOver and live Finder failures were not exercised. Historical platform results were treated as dated evidence.
