# Scanner, table, filters and shared utilities review — current disk, 2026-09-06

Read current source and corresponding tests, not prior notes or reviews. No repository source edits. Review applied requested cpp-coding-standards skill with C++17 ceiling; Qt parent ownership is valid RAII. All line numbers below refer to `/Users/martymclean/Developer/MediaMuster/`.

Reproduction source: `probes.cpp` in this directory, compiled directly with current source in C++17 Release against installed Qt 6.5.3. Evidence: `probes.log` and strengthened/repeated `probes_v2.log`.

Reproduce all probes:

```sh
cmake -S /Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/scanner_filters -B /tmp/mediamuster-review-20260906-preserved/scanner_filters-build -G Ninja -DCMAKE_PREFIX_PATH=/Users/martymclean/Qt/6.5.3/macos -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/mediamuster-review-20260906-preserved/scanner_filters-build -j 6
/tmp/mediamuster-review-20260906-preserved/scanner_filters-build/probes
```

Only disposable QTemporaryDir copies of fixtures and generated files are touched. File-size limit affects only the probe process and is restored.

## Verified findings and one unresolved scope checkpoint

### SF1 — [P2, export-file loss] CSV reports success after failed final write and destroys an existing export

Location: `src/mediacsv.cpp:85-100`, narrow anchor 92-100. Application consumes this boolean at `src/mainwindow.cpp:2193-2203` and reports “Exported …” when true.

Engineer: `QFile::open(WriteOnly)` truncates the destination immediately. The function returns `QTextStream::status()` while the stream still owns buffered output. Its destructor flushes after the return value has already been evaluated. Therefore a final write failure is missed. The existing destination has also already been discarded, even when the new output fails. `QSaveFile` plus an explicit checked stream flush and checked `commit()` would address both problems.

Plain English: A failed export can say it succeeded while leaving an incomplete report. If you were replacing a previous report, the previous good copy is gone too. This loss concerns the export file; this probe does not claim MXF media was overwritten by Export.

Proof: Write an existing `ORIGINAL REPORT`, impose a process-local eight-byte `RLIMIT_FSIZE` with `SIGXFSZ` ignored, then call the actual exporter for one row. Result: `CSV_WRITE returned=1 expected_bytes=306 actual_bytes=8 contents=efbbbf436c697020`. The eight remaining bytes are BOM plus `Clip `, not the original report or the complete new CSV. This exercises a real write error without filling a disk.

Skill connection: E.1/E.6 error strategy and RAII failure handling. Destruction releases ownership, but an operation whose failure matters must be explicitly checked before reporting success.

### SF2 — [Unranked scope checkpoint; awaiting user answer] Adding a standalone media folder with no databases finds no media

Location: `src/mediascanner.cpp:613-641` (single folder requires `hasAnyDatabase()`) and 669-687 (fallback searches only nested named Avid roots).

Engineer: The manual scanner has a per-folder enumeration path only when an Avid database exists. A directory named `Archive` containing valid MXF files directly, with no databases and no canonical root wrapper, falls through every case. The final search probes `Archive/Avid MediaFiles/MXF` and `Archive/OMFI MediaFiles`; it never enumerates `Archive/*.mxf`. Thus a manually selected folder cannot be scanned even though the MXF parser supports the file and supplies its identity/metadata.

Plain English: Dragging in an ordinary archive folder full of MXF files yields an empty table unless the archive still has an Avid database or a particular surrounding folder layout. The media is present and readable; the scan does not examine it.

Proof: The probe copies the real tone fixture to `Archive/tone.mxf` and passes that exact directory in `Options::manualPaths`. Result: `STANDALONE rows=0`. The same bytes in `Avid MediaFiles/MXF/1/tone.mxf` produce one fully described tone row. Scope caveat: if direct loose-media directories are explicitly outside the intended product, this is a scope/feature gap instead of an implementation bug; do not infer that an old source comment is a current user ruling.

Decision pending: confirm whether manually selected loose-media folders are in scope. If they are, recognize direct media files when a manually added folder does not resolve to a canonical root and reuse `processFolderTask`. Preserve explicit limits on recursive discovery separately. The observed zero-row result is verified; its status as a product defect remains unranked until that scope answer.

### SF3 — [P2, selection correctness] Real project `No project` and genuinely unassigned media are the same filter key

Location: `src/mediafilterproxy.cpp:267-270`; supporting display conversion `src/mediafile.h:260-265`; aggregate consumers `src/mainwindow.cpp:1596-1599` and 2219-2221.

Engineer: The project filter compares `projectDisplay()` instead of the raw project identity. `projectDisplay()` maps both an empty project and the valid literal project name `No project` to the same QString. Sidebar and Project Summary also aggregate by that display string, and `hasProject` becomes whichever category was encountered last. A two-row model with one of each therefore has no separate project filter representation.

Plain English: A project actually called “No project” gets mixed with files whose project is unknown. Choosing that project also selects unrelated unassigned media for any subsequent table operation.

Proof: The probe constructs one empty project and one literal `No project`, whose `hasNoProject()` results are respectively true and false. `setProjectFilter({"No project"})` yields `PROJECT_SENTINEL selected=2 unknown=1 named_unknown=0`. The same conflation is visible directly in both MainWindow aggregation keys.

Suggested change: Use a raw optional project key (empty QString can already represent absence), and apply the friendly label only in display. Store identity in `Qt::UserRole`. One typed project aggregation helper should serve sidebar and summary.

### SF4 — [P2, recovery/bin filtering] Parked media is listed but its real extension and identity are never read

Location: `src/mediascanner.cpp:1135-1142` and 1195-1213; admission rule `src/conventions.h:250-278`.

Engineer: `isAvidMediaName()` strips MediaMuster’s `.__copyreplace_`/`.__movereplace_` suffix for admission, but `buildMediaFile()` derives `extension` from the raw suffix. A valid parked MXF therefore has `extension=".__movereplace_ab12"`, is not header-readable, and cannot recover clip/MOB/project metadata. The PMR also naturally does not contain this temporary filename. OMF parks similarly fail the era test. The shared suffix helper is not applied at the type-dispatch boundary.

Plain English: Media stranded after a replacement operation is visible by filename, but the app treats its contents as unknown even though it can read the original file. Bin and project filters then cannot identify that stranded media correctly.

Proof: Two byte-identical copies of the real fixture: `tone.mxf` reports `header=1`, tone clip name, and its known master MOB; `parked.mxf.__movereplace_ab12` reports `header=0`, empty clip name and empty MOB. Exact results in `probes.log`.

Suggested change: Preserve raw filesystem identity/name but derive a separate logical media extension via `withoutTempSuffix()` for metadata dispatch. Share this derivation with OMF routing and other consumers. Do not blindly join a parked file to the current PMR entry for its former name; its header must establish its identity.

### SF5 — [P2, measured performance] Removing many nonadjacent rows blocks the UI quadratically

Location: `src/mediatablemodel.cpp:134-156`, especially 146-148; synchronous UI caller `src/mainwindow.cpp:933`.

Engineer: Contiguous deletions are coalesced, but each separated range is erased from a contiguous QVector. Removing alternating rows repeatedly shifts surviving MediaFile objects. Back-to-front order keeps indexes correct but still performs quadratic relocation work when many rows survive between removed rows. The model removal executes synchronously on the GUI thread after Move/Delete.

Plain English: After a large move or delete finishes, the app can spend several seconds freezing while removing the finished rows from the table. More files grow this delay disproportionately.

Proof, current-source C++17 Release without a view or proxy, vector detached from external sharing before timing: delete every other path. 10,000 initial rows: 357 ms; 20,000: 1,389 ms; 40,000: 5,501 ms. Doubling rows approximately quadruples time. The probe uses only model mutation; disk operations and parsing are outside the timer. These are measurements on this Mac, not promised timings on other hosts. A proxy/view may add work, but no unmeasured claim is made about how much.

Suggested change: Use a threshold for bulk compaction into surviving rows and restore selection by path; alternatively use a layout/persistent-index remapping strategy. Preserve fine-grained remove signals for small changes. Benchmark the chosen strategy with realistic separated selections.

Skill connection: Per.6 measured performance; Per.19 contiguous storage is useful but repeated middle erase is not linear compaction.

### SF6 — [P2] Duration sorting orders mixed frame rates by nominal base, reversing real duration and displayed timecode

Location: `src/mediafilterproxy.cpp:359-375`; rendered drop-frame calculation at `src/mediafile.h:410-435`.

Engineer: The comparator divides frames by nominal base and explicitly disregards drop-frame numbering. `durationDisplay()` adds dropped frame numbers when rendering. Across mixed drop-frame/non-drop-frame rows those two orders can differ. The comparator’s comment that order and display cannot disagree is false.

Plain English: Clicking Duration can put a slightly longer 29.97 fps clip before a shorter 30 fps clip. Its displayed timecode is also later than the clip that comes after it.

Proof: The strengthened probe uses two video rows with supported real rates: `fps="29.97"`, nominal base30, drop-frame true, 107,893 frames; and `fps="30"`, nominal base30, drop-frame false, 108,000 frames. At exact 30000/1001 rate, the first lasts 107893*1001/30000 = 3600.0297667 seconds; the second lasts exactly3600 seconds. Display values are respectively `01;00;00;01` and `01:00:00:00`. Ascending sort returns the longer first row before the shorter second row: `DURATION_SORT DF=01;00;00;01 then NDF=01:00:00:00`. This uses generated model rows carrying legitimate supported rate values, not an assertion that this pair was observed in a production corpus. The earlier v1 probe omitted fps; v2 explicitly supplies it and the video kinds.

Suggested change: Preserve the exact parser edit-rate rational in MediaFile and sort physical durations by that rate. If the intended sort is the displayed timecode instead, extract the displayed numeric components into a shared helper used by renderer and comparator. The current implementation is wrong under both interpretations for this mixed-rate pair.

### SF7 — [P2, explicit goal gap] Parsed sample rate has no table column

Location: `src/mediatablemodel.h:22-43`; table field mapping `src/mediatablemodel.cpp:214-258`; headers 333-336. CSV omission at `src/mediacsv.cpp:46-52`.

Engineer: `MediaFile::sampleRate` exists and scanner populates it (`mediascanner.cpp:204-205`). Audio fixture tests verify 48,000 Hz (`tests/tst_scanner.cpp:1525`). The complete column enum, data switch and header array omit Sample Rate. No main-window reader surfaces that member either. CSV cannot be used as a workaround: its field chain omits it as well.

Plain English: The scanner knows an audio file’s sample rate, but the user cannot see it in the table requested in the brief.

Proof: Complete enum/data/header mappings are exhaustive; the static_assert at 337-339 pins those table columns. Existing scanner fixture proves the underlying data is present. `rg -n 'sampleRate' src/mainwindow.cpp` has no matches. Channels and bit depth are likewise stored but not surfaced, but the current brief specifically requires sample rate, so only that is a promised-feature defect.

## Verified maintenance/extraction opportunities

1. Remove or deliberately designate the uncalled legacy flat effect filter API. `setEffectFilter`, `setPrecomputeCategoryFilter`, `setEffectCategoryFilter` have declarations/definitions and tests but no production setter callers (`rg` across src). They retain three extra sets plus state-clearing branches alongside the actual tree filter, and MainWindow retains unreachable-from-current-UI legacy chip paths at 2753-2771. `setBinFilterMobs` is also a tests-only compatibility wrapper. Do not merely extract more code around these two state models; choose whether the compatibility surface is required.
2. Extract project identity/aggregation shared by sidebar and summary (`mainwindow.cpp:1587-1613`, 2216-2231). This is a useful place to fix SF3 and to centralize rebuilding after row removal.
3. Extract logical media-name/type derivation using existing `Conventions::withoutTempSuffix` and preserve physical names separately. This fixes actual current drift in SF4.
4. Extract timecode numeric components/rendering source shared by duration sorting and display; fixes SF6 without independently recoding arithmetic.
5. Consider one column descriptor table pairing a typed Column, label and display/accessor, with explicit CSV-only fields. Table enum, table header list, table data switch, sort switch, CSV header and CSV emit chain are separately maintained. Existing helpers already correctly consolidate clip/codec/date/kind/type formatting; extend selectively rather than building a generic framework.
6. `BackgroundJob(QObject*)` ignores its sole argument (`backgroundjob.h:26-28`), and all three production owners still pass `this`. A default constructor would express its actual independent RAII ownership and remove misleading context API. Its raw owning QThread is currently joined/deleted and copying/moving is deleted, so this is not a leak finding.
7. Factual comment nit: `mediacsv.h:10` says 25 columns with effects; implementation and tests correctly produce 26 (`tst_mediacsv.cpp:114-116`). Also `mediascanner.cpp:454` says three cancel doors, whereas current doScan has two cancellation returns (362-377). Neither changes runtime behavior.

## Scope checkpoints / observations not promoted to defects

- Adding `<root>/Avid MediaFiles/MXF/1` scans all sibling media folders: exact probe yields three rows from folder1 (two) and sibling2 (one). Source 573-581 deliberately redirects to the parent MXF root; test 1926-1967 codifies it. Whether that matches desired folder-selection scope needs a current user answer, not an assumption from comments.
- `MediaTableModel::rowCount(validParent)` returns all rows and `columnCount(validParent)` returns all columns because both ignore parent (13-19). Probe observes rows2/cols15. No user-visible failure was established, so do not inflate this into a demonstrated crash or data-loss bug.
- Cooperative cancellation has no checks during recursive Quarantined Files enumeration (870-879); ordinary directory enumeration is a single blocking call (899), and parser/OS calls can also block. No timing/failure injection of a live network share was performed, so do not claim a measured network cancellation bound.
- Database-current optimization trusts filename + PMR modification time + complete records and intentionally skips headers. Existing tests deliberately allow junk bytes stamped to an indexed filename/time (967-998) to receive complete old metadata. Whether destructive operations must verify bin/project identities from file headers despite this optimization is a product safety checkpoint; no claim that those database facts must always be re-read is made here.
- No volume topology/TCC/Windows runtime validation was available in this assigned review. VolumeManager code was read fully; unused capacity refresh and topology edge cases are not presented as reproduced findings.

## Coverage

Every line read in these 18 implementation/header files, 4,464 counted lines (some files lack final newline):

- src/mediascanner.cpp, src/mediascanner.h
- src/mediafile.h
- src/mediatablemodel.cpp, src/mediatablemodel.h
- src/mediafilterproxy.cpp, src/mediafilterproxy.h
- src/volumemanager.cpp, src/volumemanager.h
- src/binfilter.h
- src/precomputefilter.h
- src/mediacsv.cpp, src/mediacsv.h
- src/conventions.h
- src/formatutil.h
- src/enumutil.h
- src/backgroundjob.h
- src/progressthrottle.h

Every line read in corresponding tests, 4,079 counted lines:

- tests/tst_scanner.cpp (2,115)
- tests/tst_mediatablemodel.cpp (453)
- tests/tst_mediafilterproxy.cpp (726)
- tests/tst_mediacsv.cpp (281)
- tests/tst_conventions.cpp (276)
- tests/tst_format.cpp (51)
- tests/tst_avbmetadata.cpp (177)

Cross-module MainWindow call sites and relevant tests/CMakeLists source lists read as indicated above; no claim of full coverage of those cross-module files. No separate .cpp exists for the other assigned header-only names.

## Additional requested verification

All 62 headers under src, including third_party/xxhash.h, compiled individually as the sole include in a fresh C++17 translation unit using the fresh application's actual macOS mainwindow compilation flags, with `-fsyntax-only`. Result: 62 passed, 0 failed. Reproducer: `check_headers.py`; summary: `header_selfcontain.log`; individual outputs: `header_checks/`. This verifies self-containment for the active macOS branches; it does not compile Windows-only branches.

Repeated Release deletion timings in probes_v2.log: 10,000 rows349ms, 20,000 rows1367ms, 40,000 rows5485ms, agreeing with the first run.

Parser-agent coordination: probes_v2.log also demonstrates model-level incorrect OMF alias adoption at mediatablemodel.cpp68. A bin containing only master UID `2a0000001122334455667788` supplies `FIRST MASTER ONLY` / `First master bin` to a row belonging to distinct master UID `2a0000004433221166558877`. Both are OmfUid canonical forms; the erroneous byte-swapped alias conflates them. Parser agent owns the combined parser/bin-filter/model finding; it is not counted again here.

Independent MainWindow inspection confirms the project's sidebar is rebuilt only by onScanFinished1583-1613. Move/Delete completion933-940 removes model rows and updates filter/status counts but leaves project names and count/byte tooltips unchanged. No model mutation signal rebuilds it. Partial removal leaves old totals, complete removal leaves a selectable empty project until rescan. Root owns the UI-level reproduction/report.
