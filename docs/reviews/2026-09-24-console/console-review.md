# MediaMuster Console wording review

24 September 2026 · original review snapshot; subsequent implementation is recorded at the end. Source links refer to the code reviewed at that time.

**340 catalogue entries: 261 Good, 79 Misleading. Every Good entry has five alternatives: 1,305 alternatives for those entries.** The quick list below contains every reviewed template. The detailed entries explain when each appears, link to its source, and give alternatives or a correction.

**Your relatives example is misleading.** The code counts every matching visible file, including the files already selected. “Selected 1 relative across 1 master clip” therefore means one selected file belonging to one master clip; it has not selected another relative. The simplest accurate replacement is **“Selected 1 file from 1 master clip.”** It uses the existing counts. Reporting **“No additional relatives to select”** would require comparing the new selection with the original selection. See M026–M027 and the [selection code](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1903).

## How to read the verdicts

- **Good:** the wording agrees with its trigger. It may still contain jargon or be longer than necessary. Five alternatives are choices; choose one, not all five. Some short existing messages cannot have five strictly shorter equivalents, so their alternatives are plain variants.
- **Misleading:** the wording can overstate what the code established, describe the wrong thing, or hide a meaningful scope or limit. These entries have a reason and a recommended correction. This does **not** mean 79 separate file-operation bugs were found.
- Counts are catalogue entries, not 340 unique complete lines: wrappers, suffixes, detailed reasons and grouped wording variants are included. A reason may appear inside several outer messages. Identical text with different triggers can have separate entries.
- `%1`, `%2` and so on are Qt placeholders. Named `{placeholders}` represent substituted values or make an alternative easier to read. File paths, counts, clip names, filter labels and native error descriptions vary. `\n` denotes an actual line break. Optional text is identified in its entry.
- Singular/plural forms should follow the count: **1 file**, **2 files**, **1 master clip**, **2 master clips**. Alternatives use example plural forms unless a template explicitly shows the alternatives.

## Scope and evidence

This is a source-based inventory of text that the in-app Console can receive in supported **macOS and Windows** builds, including enabled feature flags, Undo, recovery and rare failure paths. Defensive templates that ordinary UI preconditions suppress are identified as such. It is not a claim that every failure was reproduced on both operating systems.

The Console receives app messages, scanner batches, file-operation results and recovery notes, Rebalance messages, forwarded bin-loading failures, and Reveal-in-Finder/Explorer failures. These were followed through their lower-level reasons. System and Qt error descriptions are open-ended; the catalogue records where they are inserted and preserves them in the alternatives rather than inventing a finite list of OS messages.

Each displayed event uses **`HH:mm:ss [LEVEL] [module] message`**. Levels are `INFO`, `WARN`, `ERR` or `DBG`, with padding for alignment; fatal values also map to `ERR`. This wrapper is **Good**: it provides the time, severity and source. Five optional presentations are `HH:mm:ss [LEVEL] module: message`, `HH:mm:ss LEVEL module — message`, `HH:mm:ss · LEVEL · module · message`, `[HH:mm:ss] LEVEL module: message`, and `HH:mm:ss message (LEVEL, module)`. There is little benefit in changing the existing wrapper. It is not counted as a message-body entry. [Formatting source](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:88).

The diagnostic log also receives Qt logging-category messages that never appear in this Console. Those file-only messages, dialog-only text, status/progress labels, test fault injections and compile-time branches for unsupported platforms are outside this Console inventory. Important near-misses are listed at the ends of the relevant sections. [Console routing](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:2126), [diagnostic log routing](/Users/martymclean/Developer/MediaMuster/src/diagnostics.cpp:141).

## Most useful wording corrections

| Current wording | What is wrong | Straightforward correction |
| --- | --- | --- |
| “Selected … relatives across … master clips” | Counts all matching selected files, including the original selection. | “Selected {files} files from {clips} master clips.” |
| “Recovered … files via MDB / UMID lookup” | A metadata record was matched; no file was restored. | “Matched {files} files to MDB records by MasterMobId.” |
| “Precomputes disabled” | The media remains; extra details and filters are disabled. | “Precompute details hidden; precompute filters cleared.” |
| “No … at the root” after finding a media folder | An empty file result triggers this, even if the media folder exists. | “No supported media files found at {location}.” |
| “Copied; source retained” | That state can also mean a Trash move was declined, with no copy. | “Source retained.” Keep the detailed reason. |
| “The file changed …” following failed identity checks | A file can instead be unavailable or unreadable. | “Couldn't verify the file …” Keep the particular action and path. |
| “Journal and files retained at {journal path}” | Media files are not stored at the journal filename; early failures may have no journal yet. | “Operation stopped: {reason}.” Add an existing journal path separately. |
| “Avid databases reset in {count} folders” | Counts folders touched by Rebalance, not actual database resets. | “Rebalance updated {count} media folders.” |

Some corrections need a small condition or count change as well as wording. Each detailed entry says where that matters. In particular, this review does not treat a new sentence as a fix for unconfirmed CSV write completion or inaccurate cancelled-scan counts.

## Catalogue totals

| Area | IDs | Entries | Good | Misleading |
| --- | --- | ---: | ---: | ---: |
| App, selections, filters, exports and status | M001–M051 | 51 | 40 | 11 |
| Scanning and media metadata | S001–S038 | 38 | 25 | 13 |
| Bin-loading errors and their context | B001–B047 | 47 | 40 | 7 |
| Manage Media, Rebalance, Undo and recovery | O001–O111 | 111 | 83 | 28 |
| File access, copying, journals and system Trash | E001–E093 | 93 | 73 | 20 |
| **Total** | | **340** | **261** | **79** |

## Every current message at a glance

Click an ID for its full assessment and wording choices. Leading spaces and line breaks are easier to see in the detailed entry.

| ID | Current message or component | Verdict |
| --- | --- | --- |
| [M001](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:406) | `%1 %2 initialised on %3` | **Good** |
| [M002](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:424) | `Full Disk Access not granted. Go to System Preferences > Privacy & Security.` | **Misleading** |
| [M003](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:438) | `MediaMuster quit unexpectedly. — %1 report(s) saved. Go to Help > Reveal Logs to send them to the developer.` | **Misleading** |
| [M004](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:450) | `Style: %1` | **Good** |
| [M005](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:468) | `Removed %1 files from table` | **Good** |
| [M006](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:486) | `OMF/OMFI enabled for this session. Rescan to include legacy media.` | **Good** |
| [M007](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:504) | `OMF/OMFI disabled; legacy media removed from the table.` | **Good** |
| [M008](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:522) | `Precompute Details enabled for this session` | **Good** |
| [M009](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:540) | `Precomputes disabled; precompute filters cleared` | **Misleading** |
| [M010](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:552) | `Precompute filter cleared` | **Good** |
| [M011](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:570) | `Precompute filter: %1; volume: %2` | **Good** |
| [M012](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:588) | `Cannot load bin "%1": %2` | **Good** |
| [M013](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:606) | `Bin filter cleared` | **Good** |
| [M014](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:624) | `Bin filter active — %1 operations` | **Good** |
| [M015](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:642) | `Opening rebalance dialog (%1 volume(s), default '%2')` | **Misleading** |
| [M016](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:654) | `Couldn't determine volume path for re-scan; please scan manually` | **Good** |
| [M017](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:672) | `Re-scanning '%1' after rebalance` | **Good** |
| [M018](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:690) | `Not an Avid media location. Add an Avid MediaFiles or OMFI MediaFiles folder, or its containing folder. %1` | **Good** |
| [M019](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:708) | `Added: %1` | **Good** |
| [M020](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:726) | `Found %1 volumes (%2 with Avid MediaFiles)` | **Misleading** |
| [M021](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:738) | `Scan All: %1 locations` | **Good** |
| [M022](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:756) | `Exporting %1 %2 to %3` | **Good** |
| [M023](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:774) | `Exported %1 %2 to %3` | **Misleading** |
| [M024](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:786) | `Failed to write %1` | **Good** |
| [M025](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:804) | `No master MOBs in selection — nothing to follow` | **Good** |
| [M026](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:822) | `No relatives found (the relatives may be filtered out)` | **Misleading** |
| [M027](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:836) | `Selected %1 relative%2 across %3 master clip%4` | **Misleading** |
| [M028](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:850) | `Inverted selection: %1 of %2 visible row%3 selected` | **Good** |
| [M029](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:868) | `Cancel requested` | **Good** |
| [M030](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:886) | `%1: Completed[ — %2]` | **Good** |
| [M031](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:904) | `%1: Copied; source retained[ — %2]` | **Misleading** |
| [M032](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:916) | `%1: Original restored[ — %2]` | **Good** |
| [M033](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:934) | `%1: Already at destination[ — %2]` | **Good** |
| [M034](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:952) | `%1: Skipped[ — %2]` | **Good** |
| [M035](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:970) | `%1: Cancelled[ — %2]` | **Good** |
| [M036](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:988) | `%1: Failed[ — %2]` | **Good** |
| [M037](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1006) | `%1: Needs attention[ — %2]` | **Good** |
| [M038](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1024) | `Undoing the last operation.` | **Good** |
| [M039](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1042) | `Restoring interrupted originals.` | **Good** |
| [M040](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1060) | `Stopped the unfinished job. Completed results were kept.` | **Good** |
| [M041](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1078) | `Resuming the previous job.` | **Misleading** |
| [M042](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1090) | `Cannot reveal: file and parent folder are both unreachable` | **Misleading** |
| [M043](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1102) | `Couldn't open the parent folder: %1` | **Good** |
| [M044](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1120) | `open(1) and osascript both failed; opening parent` | **Good** |
| [M045](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1138) | `GetFullPathNameW failed; opening parent folder` | **Good** |
| [M046](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1156) | `SHOpenFolderAndSelectItems failed: 0x%1` | **Good** |
| [M047](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1174) | `SHParseDisplayName failed: 0x%1` | **Good** |
| [M048](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1192) | `Shell API + explorer.exe /select both failed; opening parent` | **Good** |
| [M049](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1210) | `No file path to reveal` | **Good** |
| [M050](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1228) | `File not found, opening parent folder: %1` | **Good** |
| [M051](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1246) | `Journal cleanup: {detail}` | **Good** |
| [S001](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1270) | `Scanning %1 location(s)...` | **Good** |
| [S002](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1284) | `Skipping unsupported UME media folder: %1` | **Good** |
| [S003](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1298) | `Permission denied: %1 /   Permission denied: %1` | **Misleading** |
| [S004](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1308) | `Grant Full Disk Access in System Preferences > Privacy & Security` | **Misleading** |
| [S005](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1318) | `Scanning: %1 (%2)` | **Good** |
| [S006](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1332) | `%1: %2 media files found` | **Good** |
| [S007](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1346) | `No media files found.` | **Good** |
| [S008](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1360) | `Scan complete: %1 files found` | **Good** |
| [S009](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1374) | `%1 file%2 with no local database reference` | **Misleading** |
| [S010](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1384) | `%1 file%2 in folders with no readable database` | **Misleading** |
| [S011](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1394) | `%1 file%2 with an invalid (all-zero) UMID` | **Good** |
| [S012](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1408) | `%1 file%2 with no project name anywhere` | **Misleading** |
| [S013](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1418) | `%1 non-portable filename%2` | **Misleading** |
| [S014](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1428) | `Scan cancelled by user` | **Good** |
| [S015](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1442) | `%1 folder(s) over %2 files (Avid recommends staying under %3):` | **Misleading** |
| [S016](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1454) | `Found Avid MediaFiles/MXF` | **Good** |
| [S017](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1468) | `Found OMFI MediaFiles` | **Good** |
| [S018](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1482) | `No %1 at the root of %2 (media in a subfolder is found via File > Add Folder or Volume)` | **Misleading** |
| [S019](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1492) | `Not an Avid media location: %1. Add an Avid MediaFiles or OMFI MediaFiles folder, or its containing folder.` | **Good** |
| [S020](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1506) | `%1 subfolders queued for concurrent scanning` | **Good** |
| [S021](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1520) | `Quarantined Files folder on %1 is empty` | **Good** |
| [S022](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1534) | `⚠️ Avid Quarantined Files folder on %1 contains %2 MXF file(s)!` | **Good** |
| [S023](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1548) | `Quarantined Files folder on %1 contains %2 non-MXF file(s)` | **Good** |
| [S024](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1562) | `/%1: %2 media file(s), %3 described by the databases, %4 need a header read` | **Good** |
| [S025](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1576) | `(%1 changed since Avid indexed them)` | **Misleading** |
| [S026](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1586) | `%1: %2 file entries in /%3` | **Good** |
| [S027](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1600) | `%1 in /%2 is unreadable; unmatched files here surface as 'No database', not 'No reference'` | **Good** |
| [S028](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1614) | `%1 in /%2 is unreadable; ignored, the msmFMID.pmr index stands` | **Good** |
| [S029](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1628) | `No msmFMID.pmr in /%1` | **Good** |
| [S030](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1642) | `%1: %2 clips, %3 files in /%4` | **Good** |
| [S031](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1656) | `%1 in /%2 is unreadable; unmatched files here surface as 'No database', not 'No reference'` | **Good** |
| [S032](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1670) | `%1 in /%2 is unreadable; ignored, the msmMMOB.mdb records stand` | **Good** |
| [S033](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1684) | `No msmMMOB.mdb in /%1` | **Good** |
| [S034](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1698) | `Reading MXF headers for %1 file(s) needing metadata verification` | **Good** |
| [S035](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1712) | `Reading MXF/OMF headers for %1 file(s) needing metadata verification (%2 MXF, %3 OMF)` | **Misleading** |
| [S036](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1722) | `MXF parse: %1 files, avg %2 KB/file, max %3 KB, total %4 MB read` | **Misleading** |
| [S037](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1732) | `OMF parse: %1 files, avg %2 KB/file, max %3 KB, total %4 KB read` | **Misleading** |
| [S038](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1742) | `Recovered %1 file(s) via MDB / UMID lookup` | **Misleading** |
| [B001](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1760) | `Choose an Avid bin file with an .avb extension.` | **Good** |
| [B002](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1774) | `This bin contains data that MediaMuster does not yet support` | **Good** |
| [B003](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1788) | `The bin could not be read completely.` | **Good** |
| [B004](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1802) | `This bin file no longer exists.` | **Good** |
| [B005](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1816) | `This path is not a regular file. \| AVB path is not a regular file.` | **Good** |
| [B006](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1830) | `{QFile::errorString()}` | **Good** |
| [B007](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1844) | `This file is not an Avid bin.` | **Misleading** |
| [B008](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1854) | `Bin reading cancelled.` | **Good** |
| [B009](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1868) | `%1 (byte %2)` | **Good** |
| [B010](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1882) | `Object %1 (%2): %3` | **Good** |
| [B011](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1896) | `Truncated AVB property` | **Good** |
| [B012](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1910) | `Cannot read AVB property` | **Good** |
| [B013](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1924) | `Invalid AVB string or byte-array length` | **Good** |
| [B014](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1938) | `AVB property exceeds its object` | **Good** |
| [B015](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1952) | `Truncated AVB object` | **Misleading** |
| [B016](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1962) | `Invalid AVB property tag; expected 0x%1` | **Good** |
| [B017](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1976) | `Unsupported AVB object version %1 (expected %2)` | **Good** |
| [B018](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:1990) | `Unsupported AVB extension 0x%1` | **Good** |
| [B019](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2004) | `Unexpected data after AVB object end` | **Good** |
| [B020](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2018) | `Invalid UTF-8 AVB string` | **Good** |
| [B021](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2032) | `Invalid AVB entry count` | **Misleading** |
| [B022](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2042) | `Invalid MOB label length` | **Good** |
| [B023](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2056) | `Invalid MOB material length` | **Good** |
| [B024](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2070) | `AVB file changed while reading; load it again.` | **Good** |
| [B025](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2084) | `AVB identity and metadata inventory exceeds the 192 MiB memory budget.` | **Good** |
| [B026](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2098) | `AVB file is empty, truncated, or exceeds the 256 MiB limit.` | **Good** |
| [B027](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2112) | `Not an Avid bin: invalid byte-order marker.` | **Good** |
| [B028](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2126) | `Not an Avid bin: invalid document header` | **Good** |
| [B029](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2140) | `Invalid AVB object count or root reference` | **Misleading** |
| [B030](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2150) | `AVB header byte order is inconsistent` | **Good** |
| [B031](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2164) | `Invalid AVB document format identifiers` | **Good** |
| [B032](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2178) | `Invalid AVB chunk length` | **Good** |
| [B033](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2192) | `Invalid AVB class identifier` | **Good** |
| [B034](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2206) | `Unexpected data after declared AVB objects` | **Good** |
| [B035](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2220) | `AVB document root is not a bin` | **Good** |
| [B036](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2234) | `Invalid AVB object reference %1` | **Good** |
| [B037](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2248) | `AVB reference %1 must identify %2` | **Good** |
| [B038](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2262) | `Unsupported AVB track flags` | **Good** |
| [B039](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2276) | `Unsupported AVB bin version` | **Good** |
| [B040](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2290) | `Invalid AVB bin rectangle version` | **Misleading** |
| [B041](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2300) | `Invalid AVB bin color version` | **Misleading** |
| [B042](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2310) | `Unsupported AVB attribute type %1` | **Good** |
| [B043](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2324) | `Invalid AVB marker color version` | **Misleading** |
| [B044](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2334) | `Unsupported AudioSuite plug-in count` | **Good** |
| [B045](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2348) | `Invalid AudioSuite preset length` | **Good** |
| [B046](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2362) | `AVB selector refers to an absent track` | **Good** |
| [B047](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2376) | `Unsupported AVB class %1; whole-bin identity coverage is incomplete.` | **Good** |
| [O001](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2406) | `Another operation is still running.` | **Good** |
| [O002](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2420) | `Enable undo in the Debug menu first.` | **Good** |
| [O003](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2434) | `{name}: {message}` | **Good** |
| [O004](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2448) | `Avid databases reset in {count} folder(s); Avid rebuilds them on next launch.` | **Misleading** |
| [O005](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2458) | `Copy finished; original retained. This storage does not support confirming folder changes against a crash or power loss.` | **Good** |
| [O006](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2472) | `The file's Avid identity is missing or differs from the scan. Rescan before proceeding.` | **Good** |
| [O007](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2486) | `The completed destination is missing or changed: {destination}` | **Misleading** |
| [O008](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2496) | `Original restoration is pending: {temporary path} -> {original path}` | **Good** |
| [O009](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2510) | `The original changed while awaiting a Trash choice; no fallback was attempted. {detail}` | **Misleading** |
| [O010](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2520) | `The system Trash result was interrupted before a recovery receipt was saved. Inspect Trash; MediaMuster will not repeat this deletion.` | **Misleading** |
| [O011](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2530) | `An original or retirement location changed. Both locations were retained.` | **Misleading** |
| [O012](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2540) | `The interrupted relocation needs inspection; both locations were retained.` | **Misleading** |
| [O013](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2550) | `The original file is missing or changed; its recovery record was retained: {source}` | **Misleading** |
| [O014](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2560) | `The original changed or is missing without a recorded removal intent.` | **Misleading** |
| [O015](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2570) | `The completed copy could not confirm its writes during recovery. {detail}` | **Good** |
| [O016](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2584) | `Cannot establish the interrupted result; journal and files were retained.` | **Good** |
| [O017](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2598) | `Temporary cleanup pending at {path}: {reason}` | **Good** |
| [O018](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2612) | `The interrupted file operation must be reconciled before its folder is removed.` | **Good** |
| [O019](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2626) | `Cannot confirm the folder removal. {detail}` | **Good** |
| [O020](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2640) | `The folder identity changed; it was retained.` | **Misleading** |
| [O021](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2650) | `The partial file cannot be safely identified as disposable. {detail}` | **Good** |
| [O022](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2664) | `The surviving original or completed copy changed; the partial was retained.` | **Misleading** |
| [O023](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2674) | `Cannot confirm partial cleanup. {detail}` | **Good** |
| [O024](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2688) | `Journal failure; source retained.` | **Good** |
| [O025](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2702) | `Journal failure; temporary folder retained at {temporary folder}` | **Good** |
| [O026](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2716) | `Journal failure; temporary file retained at {temporary file}` | **Good** |
| [O027](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2730) | `Copy finished; the storage did not confirm the full durability request.` | **Good** |
| [O028](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2744) | `Cancelled before publication.` | **Good** |
| [O029](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2758) | `A file changed before publication; source retained.` | **Misleading** |
| [O030](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2768) | `The temporary file changed before publication.` | **Misleading** |
| [O031](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2778) | `The destination became occupied; source retained.` | **Good** |
| [O032](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2792) | `All Keep Both names are occupied.` | **Good** |
| [O033](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2806) | `The published file's folder update could not be confirmed. Source retained.` | **Good** |
| [O034](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2820) | `The copy was published, but the journal failed. Source retained at {source}` | **Good** |
| [O035](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2834) | `The operation finished on disk, but the journal could not confirm completion. Destination: {destination}` | **Good** |
| [O036](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2848) | `Too many destination conflicts.` | **Good** |
| [O037](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2862) | `Skipped as requested.` | **Good** |
| [O038](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2876) | `Restored from system Trash.` | **Good** |
| [O039](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2890) | `The file changed since it was selected or journalled; rescan before proceeding.` | **Misleading** |
| [O040](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2900) | `System Trash result needs recovery.` | **Good** |
| [O041](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2914) | `Moved to system Trash.` | **Good** |
| [O042](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2928) | `Original changed while checking bin support. {detail}` | **Misleading** |
| [O043](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2938) | `Cannot save the bin refusal; original retained.` | **Good** |
| [O044](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2952) | `Cancelled before moving to MediaMuster Trash.` | **Good** |
| [O045](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2966) | `Already at the destination; no file changes were needed.` | **Good** |
| [O046](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2980) | `Rebalance stopped: a destination became occupied after the group check. Rescan and replan.` | **Good** |
| [O047](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:2994) | `Destination occupied; source retained.` | **Good** |
| [O048](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3008) | `Cannot prepare relocation; the source was retained.\n{detail}` | **Good** |
| [O049](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3022) | `Journal failure; relocation stopped.` | **Good** |
| [O050](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3036) | `The source changed before relocation; rescan before proceeding.` | **Misleading** |
| [O051](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3046) | `Cancelled before moving to Trash.` | **Good** |
| [O052](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3060) | `Rebalance stopped: a destination became occupied. Completed moves and remaining files are recorded.` | **Good** |
| [O053](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3074) | `Destination became occupied; source retained.` | **Good** |
| [O054](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3088) | `The relocated file changed during the operation. Inspect {destination}; its journal is retained.` | **Misleading** |
| [O055](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3098) | `File relocated to {destination}, but folder durability needs recovery confirmation.` | **Good** |
| [O056](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3112) | `Relocated to {destination}; journal completion failed.` | **Good** |
| [O057](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3126) | `Moved to MediaMuster Trash.` | **Good** |
| [O058](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3140) | `Safe same-filesystem relocation is unavailable. The source was retained.` | **Good** |
| [O059](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3154) | `Original restoration pending: {temporary path} -> {original path}. {reason}` | **Good** |
| [O060](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3168) | `A path is no longer safe; files were retained.` | **Good** |
| [O061](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3182) | `The original location is occupied; nothing was overwritten.` | **Good** |
| [O062](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3196) | `The retained original is missing or changed. {detail}` | **Misleading** |
| [O063](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3206) | `The original could not be returned safely. {detail}` | **Good** |
| [O064](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3220) | `The folder updates still need confirmation. {detail}` | **Good** |
| [O065](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3234) | `Original restored to {source}. Completed copies were kept.` | **Good** |
| [O066](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3248) | `Cannot read the original restoration record.` | **Good** |
| [O067](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3262) | `An Undo already owns this job's recovery. Resume that Undo first.` | **Good** |
| [O068](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3276) | `The completed copy is not ready for original removal.` | **Good** |
| [O069](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3290) | `Cancelled; original retained.` | **Good** |
| [O070](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3304) | `Original removal durability needs recovery. {detail}` | **Good** |
| [O071](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3318) | `Both original and retirement paths are occupied.` | **Good** |
| [O072](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3332) | `Original changed before removal: {path}` | **Misleading** |
| [O073](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3342) | `Cannot prepare original removal. {detail}` | **Good** |
| [O074](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3356) | `Original removal needs recovery. {detail}` | **Good** |
| [O075](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3370) | `Could not confirm original removal. {detail}` | **Good** |
| [O076](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3384) | `Enable undo in the Debug menu before starting an Undo.` | **Good** |
| [O077](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3398) | `This job cannot start another Undo.` | **Good** |
| [O078](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3412) | `An Undo object has no saved identity.` | **Good** |
| [O079](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3426) | `An interrupted system Trash action needs inspection before Undo.` | **Good** |
| [O080](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3440) | `A relocated file changed or its original location is occupied.` | **Misleading** |
| [O081](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3450) | `An original changed or disappeared without a removal record; Undo stopped.` | **Misleading** |
| [O082](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3460) | `This job has no completed work that can be undone.` | **Good** |
| [O083](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3474) | `Avid database retirement stopped: {detail}` | **Good** |
| [O084](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3488) | `Retrying {name}` | **Good** |
| [O085](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3502) | `Cancelled before retrying the copy.` | **Good** |
| [O086](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3516) | `Cannot read the requested operation journal.` | **Good** |
| [O087](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3530) | `This job was abandoned or has already started Undo.` | **Good** |
| [O088](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3544) | `This job has already started Undo.` | **Good** |
| [O089](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3558) | `Cannot confirm which job owns this Undo.` | **Good** |
| [O090](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3572) | `Cannot save ownership of the interrupted Undo.` | **Good** |
| [O091](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3586) | `The previous job was interrupted. Resume or stop it first.` | **Good** |
| [O092](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3600) | `Only the most recent eligible job can be undone.` | **Good** |
| [O093](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3614) | `Choose an absolute destination folder before starting.` | **Good** |
| [O094](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3628) | `The source and relocation paths must be absolute.` | **Good** |
| [O095](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3642) | `Unsupported name, folder or conflict policy. Replace is not supported.` | **Good** |
| [O096](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3656) | `Undo was saved, but original ownership needs recovery.` | **Good** |
| [O097](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3670) | `Rebalance group skipped: a destination folder no longer has room below 5,000 files. Rescan and replan.` | **Misleading** |
| [O098](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3680) | `Rebalance group skipped: a destination is occupied. Rescan and replan.` | **Good** |
| [O099](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3694) | `Rebalance unavailable; source files retained.\n{detail}` | **Misleading** |
| [O100](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3704) | `Avid database relocation stopped: {detail}` | **Good** |
| [O101](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3718) | `Original retained because the job's required copies have not all completed safely.` | **Good** |
| [O102](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3732) | `Cannot confirm original locations before finishing Undo.` | **Good** |
| [O103](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3746) | `A restored original changed; its remaining copy was retained.` | **Misleading** |
| [O104](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3756) | `An original changed while preparing the Trash choice; no fallback was attempted.` | **Misleading** |
| [O105](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3766) | `Original retained; the MediaMuster Trash move was not approved or the operation stopped.` | **Good** |
| [O106](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3780) | `A restored original changed while awaiting the Trash choice; its remaining copy was retained.` | **Misleading** |
| [O107](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3790) | `Operation stopped: {error}. Journal and files retained at {journal path}.` | **Misleading** |
| [O108](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3800) | `{operation}: {completed} completed, {unchanged} unchanged, {retained} source retained, {skipped} skipped, {failed} failed, {attention} need attention{cancelled suffix}. Journal: {journal path}` | **Misleading** |
| [O109](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3810) | `Isolated temporary file retained: {path}` | **Misleading** |
| [O110](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3820) | `Invalid journal retained for inspection: {journal path}` | **Good** |
| [O111](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3834) | `{error} Journal: {journal path}` | **Good** |
| [E001](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3877) | `Windows file error %1` | **Good** |
| [E002](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3891) | `{POSIX error description}` | **Good** |
| [E003](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3905) | `A path contains a symbolic link or unsupported reparse point: %1` | **Misleading** |
| [E004](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3913) | `Cannot create folder %1` | **Good** |
| [E005](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3927) | `Folder created, but its directory persistence is unconfirmed: %1\n%2` | **Good** |
| [E006](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3941) | `A private operation folder requires a fresh, unredirected UUID path.` | **Good** |
| [E007](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3955) | `The operation folder could not be made private: %1` | **Good** |
| [E008](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3969) | `The newly created operation folder could not be identified.` | **Good** |
| [E009](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3983) | `Folder cleanup requires its recorded private directory identity.` | **Good** |
| [E010](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:3997) | `The private folder was replaced; it was retained.` | **Misleading** |
| [E011](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4005) | `The private folder identity or permissions changed; it was retained.` | **Misleading** |
| [E012](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4013) | `Folder cleanup is unconfirmed; the path remains occupied.` | **Good** |
| [E013](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4027) | `Unsupported or redirected path: %1` | **Good** |
| [E014](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4041) | `Only regular files are supported.` | **Misleading** |
| [E015](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4049) | `Cannot attach file handle.` | **Good** |
| [E016](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4063) | `Cannot attach file to the copy stream.` | **Misleading** |
| [E017](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4071) | `Could not preserve all file metadata: %1` | **Good** |
| [E018](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4085) | `Encrypted Windows files require an encryption-aware copy. The source was retained.` | **Good** |
| [E019](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4099) | `The file or its location changed before relocation.` | **Misleading** |
| [E020](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4107) | `The file cannot be protected for relocation; it was retained.` | **Good** |
| [E021](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4121) | `The relocated file could not be confirmed at %1. Files have been retained; recovery needs attention.` | **Good** |
| [E022](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4135) | `File retained at %1: protected removal is unavailable.` | **Good** |
| [E023](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4149) | `Partial cleanup requires its recorded isolated file and folder.` | **Good** |
| [E024](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4163) | `The partial file or its staging folder could not be protected.` | **Good** |
| [E025](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4177) | `The staging folder or partial file changed; it was retained.` | **Misleading** |
| [E026](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4185) | `Partial removed, but its staging folder could not be flushed: {native detail}` | **Good** |
| [E027](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4199) | `Partial cleanup is unconfirmed; the path remains occupied.` | **Good** |
| [E028](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4213) | `Original removal requires its recorded isolated retirement file.` | **Good** |
| [E029](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4227) | `The retired original could not be protected for removal.` | **Good** |
| [E030](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4241) | `Original removal is pending; the retirement path remains occupied.` | **Misleading** |
| [E031](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4249) | `The retirement directory or original identity is not protected.` | **Good** |
| [E032](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4263) | `Original removed, but its retirement directory could not be flushed: {native detail}` | **Good** |
| [E033](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4279) | `Cannot start native copying with these source or staging identities.` | **Good** |
| [E034](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4293) | `Cannot position the native copy handles.` | **Good** |
| [E035](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4307) | `Cannot allocate native copy state.` | **Good** |
| [E036](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4321) | `Native copying failed: %1 (POSIX %2).` | **Good** |
| [E037](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4335) | `A native copying handle referred to a changed file.` | **Misleading** |
| [E038](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4343) | `Native copying could not be completed and protected (Windows %1). %2 %3` | **Misleading** |
| [E039](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4351) | `The source changed during copying; it has been retained.` | **Misleading** |
| [E040](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4359) | `The destination could not confirm its writes.` | **Good** |
| [E041](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4373) | `The destination length differs from the source.` | **Misleading** |
| [E042](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4381) | `The source changed before copying finished; it has been retained.` | **Misleading** |
| [E043](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4391) | `Another file operation or recovery owns the journal. Try again after it finishes.` | **Misleading** |
| [E044](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4399) | `The operation journal could not be saved. Further changes have stopped; files and recovery records were retained.` | **Good** |
| [E045](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4413) | `{Qt file error description}` | **Good** |
| [E046](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4427) | `Cannot persist the journal directory.\n{sync error}` | **Good** |
| [E047](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4441) | `Journal contains an invalid record; it was preserved for inspection.` | **Good** |
| [E048](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4455) | `Cannot determine the journal retention date.` | **Good** |
| [E049](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4469) | `Cannot remove expired journal: {path}. {Qt file error}` | **Good** |
| [E050](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4483) | `Cannot read recovery record.` | **Good** |
| [E051](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4497) | `Cannot establish the recorded source storage for restoration. Originals were retained.` | **Good** |
| [E052](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4511) | `The recorded volume has more than one possible mount; files were retained.` | **Good** |
| [E053](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4525) | `Cannot establish the recorded volume at %1. Reconnect the original storage; recovery records are retained.` | **Good** |
| [E054](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4539) | `CreateFileW(directory) failed for %1 (Windows error %2).` | **Good** |
| [E055](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4553) | `FlushFileBuffers(directory) failed for %1 (Windows error %2).` | **Good** |
| [E056](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4567) | `open(directory) failed for %1 (POSIX error %2: %3).` | **Good** |
| [E057](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4581) | `fsync(directory) failed for %1 (POSIX error %2: %3).` | **Good** |
| [E058](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4599) | `Cancelled before system Trash.` | **Good** |
| [E059](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4613) | `The original changed before system Trash.` | **Misleading** |
| [E060](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4621) | `Cancelled before Trash restoration.` | **Good** |
| [E061](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4635) | `The Trash receipt or restore destination cannot be confirmed.` | **Good** |
| [E062](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4649) | `The system returned no Trash result.` | **Good** |
| [E063](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4663) | `%1 (%2, code %3)` | **Good** |
| [E064](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4677) | `The source changed before system Trash.` | **Misleading** |
| [E065](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4685) | `The system Trash result needs identity or location reconciliation.` | **Good** |
| [E066](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4699) | `File is in system Trash, but directory persistence needs confirmation. {sync error}` | **Good** |
| [E067](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4713) | `This is not a macOS Trash receipt.` | **Good** |
| [E068](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4727) | `The recorded Trash item changed or is missing. {file error}` | **Misleading** |
| [E069](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4735) | `Restored file needs directory-persistence confirmation. {sync error}` | **Good** |
| [E070](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4753) | `The Shell operation has no confirmed recoverable result (HRESULT %1).` | **Good** |
| [E071](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4767) | `The Shell result needs identity or location reconciliation.` | **Good** |
| [E072](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4781) | `The Shell operation completed, but directory persistence needs confirmation. {sync error}` | **Good** |
| [E073](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4795) | `Windows Shell operations are unavailable.` | **Good** |
| [E074](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4809) | `System Trash operation failed (HRESULT %1%2).` | **Good** |
| [E075](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4823) | `The system bin could not initialize its Shell thread (Windows HRESULT 0x{code}).` | **Good** |
| [E076](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4837) | `The system bin could not identify the source (Windows HRESULT 0x{code}).` | **Good** |
| [E077](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4851) | `The system bin could not identify the source folder (Windows HRESULT 0x{code}).` | **Good** |
| [E078](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4865) | `This folder does not provide native recycling (Windows HRESULT 0x{code}).` | **Misleading** |
| [E079](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4873) | `The system bin could not install its failure handler (Windows HRESULT 0x{code}).` | **Good** |
| [E080](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4887) | `The system bin is unavailable (Windows HRESULT 0x{code}).` | **Good** |
| [E081](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4901) | `Cancelled before system bin recycling (Windows HRESULT 0x{code}).` | **Good** |
| [E082](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4915) | `The source changed before system bin recycling.` | **Misleading** |
| [E083](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4923) | `The system bin could not recycle this file (Windows HRESULT 0x{code}).` | **Good** |
| [E084](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4937) | `Native failure detail (Windows HRESULT 0x{code}).` | **Good** |
| [E085](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4951) | `The system bin result needs identity or location reconciliation (Windows HRESULT 0x{code}).` | **Good** |
| [E086](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4965) | `File is in the system bin, but directory persistence needs confirmation. {sync error}` | **Good** |
| [E087](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4979) | `This is not a Windows Trash receipt.` | **Good** |
| [E088](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:4993) | `The Shell Trash identifier is invalid.` | **Good** |
| [E089](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:5007) | `A Shell STA thread could not be initialized.` | **Good** |
| [E090](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:5021) | `Cancelled while locating the Trash item.` | **Good** |
| [E091](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:5035) | `More than one Trash item matches the saved identity.` | **Good** |
| [E092](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:5049) | `The recorded Recycle Bin item changed or is missing.` | **Misleading** |
| [E093](/Users/martymclean/Developer/MediaMuster/docs/reviews/2026-09-24-console/console-review.md:5057) | `Trash restoration requires its original local filesystem.` | **Good** |

## App, selections, filters, exports and status

Read-only review of the current working tree, 24 September 2026. These are message templates, not every possible filename/count. Placeholders in alternatives use `{name}` notation. Current `%1` etc. are exact positional placeholders unless noted. Optional `— %2` is shown as `[ — %2]`; `{detail}` means retain that optional explanation, including its separator. Five alternatives are supplied for every **Good** template. A message can be accurate but still too technical. Some one-word statuses are already minimal, so alternatives are plain-language variants rather than all being shorter.

Sources flow through `MainWindow::addLog`; operation result bodies use the same wrapper. Raw recovery notes, journal-dismiss errors and manager logs are catalogued in the operations/file-errors sections rather than duplicated here. Reveal messages use `[app]` for Help > Reveal Logs and `[reveal]` for a selected media file. RebalanceDialog adds no console literals of its own; its aborted reason is dialog-only.

### M001 — Good

**Current:** `%1 %2 initialised on %3`

**Source:** [src/mainwindow.cpp:234](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:234) · app / INFO.

**When / values:** Startup; app name, app version and operating-system label.

**Assessment:** Accurate startup information; 'started' is plainer than 'initialised'.

**Five simpler alternatives:**

1. {app} {version} started on {OS}.
2. Started {app} {version} on {OS}.
3. {app} {version} is running on {OS}.
4. App started: {app} {version}, {OS}.
5. Running {app} {version} on {OS}.

### M002 — Misleading

**Current:** `Full Disk Access not granted. Go to System Preferences > Privacy & Security.`

**Source:** [src/mainwindow.cpp:242](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:242) · app / WARN / macOS.

**When / values:** The app could not open the user's protected TCC database.

**Assessment:** A failed read is a permission heuristic, not proof of the exact grant state. The settings wording also combines names from different macOS settings layouts.

**Recommended correction:** Couldn't confirm Full Disk Access. Check System Settings > Privacy & Security > Full Disk Access.

The app's own shortcut is **Help > Full Disk Access**. The current macOS settings names are confirmed by [Apple's Privacy & Security guide](https://support.apple.com/en-mt/guide/mac-help/-mchl211c911f/mac).

### M003 — Misleading

**Current:** `MediaMuster quit unexpectedly. — %1 report(s) saved. Go to Help > Reveal Logs to send them to the developer.`

**Source:** [src/mainwindow.cpp:275](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:275) · app / WARN / macOS.

**When / values:** One or more previously uncollected crash reports, up to 30 days old, were copied into the log folder.

**Assessment:** It can sound like the immediately preceding session crashed. The collection may instead have found older reports. 'report(s)' is also awkward.

**Recommended correction:** Found {count} crash reports. Saved with your logs; use Help > Reveal Logs to share them.

### M004 — Good

**Current:** `Style: %1`

**Source:** [src/mainwindow.cpp:741](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:741) · app / INFO.

**When / values:** Debug Fusion-style toggle; %1 is fusion, macos or windows.

**Assessment:** Correct, but raw style identifiers are less friendly than display names.

**Five simpler alternatives:**

1. Appearance: {style}.
2. Using {style} appearance.
3. Switched to {style} appearance.
4. App appearance: {style}.
5. {style} appearance selected.

### M005 — Good

**Current:** `Removed %1 files from table`

**Source:** [src/mainwindow.cpp:853](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:853) · ops / INFO.

**When / values:** Difference in model row count after confirmed removals and UI refresh.

**Assessment:** Explicitly says the table; it does not claim another disk deletion. Use file/files or row/rows correctly.

**Five simpler alternatives:**

1. Removed {count} rows from the table.
2. Table updated: {count} files removed.
3. {count} files removed from the list.
4. Removed {count} file entries.
5. The table now excludes {count} removed files.

### M006 — Good

**Current:** `OMF/OMFI enabled for this session. Rescan to include legacy media.`

**Source:** [src/mainwindow.cpp:1017](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1017) · scanner / INFO.

**When / values:** OMF feature switched on; existing rows are not automatically rescanned.

**Assessment:** Accurate and gives the required next action.

**Five simpler alternatives:**

1. Legacy media enabled. Rescan to include it.
2. OMFI MediaFiles support enabled for this session. Rescan to load its media.
3. Rescan to include legacy media in this session.
4. Legacy media scanning enabled. Run another scan to load it.
5. OMF support is on for this session. Rescan to include legacy media.

### M007 — Good

**Current:** `OMF/OMFI disabled; legacy media removed from the table.`

**Source:** [src/mainwindow.cpp:1017](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1017) · scanner / INFO.

**When / values:** OMF feature switched off; rows in the OMF media family are removed.

**Assessment:** Accurate for rows from the legacy media family, including supported audio in OMFI MediaFiles. Alternatives should not imply that every such file has the OMF file format.

**Five simpler alternatives:**

1. Legacy media disabled; its rows removed from the table.
2. OMFI MediaFiles support is off; its files are removed from the table.
3. Removed legacy media from the table.
4. Legacy media disabled for this session; table updated.
5. OMF support is off. Removed its legacy media rows.

### M008 — Good

**Current:** `Precompute Details enabled for this session`

**Source:** [src/mainwindow.cpp:1078](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1078) · effects / INFO.

**When / values:** Precompute details, classification/filter controls and export fields enabled.

**Assessment:** Correctly limits the statement to details rather than newly discovered media.

**Five simpler alternatives:**

1. Precompute details enabled.
2. Precompute details are on for this session.
3. Showing precompute details.
4. Precompute details and filters enabled.
5. Precompute columns and filters are available.

### M009 — Misleading

**Current:** `Precomputes disabled; precompute filters cleared`

**Source:** [src/mainwindow.cpp:1078](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1078) · effects / INFO.

**When / values:** Precompute feature off; details/filters disabled, but rendered media remains in the scan/table.

**Assessment:** The precompute media itself has not been disabled or removed; only the additional details and filtering are off.

**Recommended correction:** Precompute details hidden; precompute filters cleared.

### M010 — Good

**Current:** `Precompute filter cleared`

**Source:** [src/mainwindow.cpp:1105](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1105) · effects / INFO.

**When / values:** No precompute tree restriction and no precompute volume restriction.

**Assessment:** Accurate short message.

**Five simpler alternatives:**

1. Precompute filter removed.
2. Precompute filtering cleared.
3. No precompute filter is applied.
4. Cleared the precompute filter.
5. All precomputes pass this filter.

### M011 — Good

**Current:** `Precompute filter: %1; volume: %2`

**Source:** [src/mainwindow.cpp:1105](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1105) · effects / INFO.

**When / values:** %1 = all precomputes, no checked branches, or checked path labels joined by '; '. Each path uses ' / '. %2 = all scanned volumes or the selected location path.

**Assessment:** Accurate description of the applied filter. 'No checked branches' means the active tree selection is empty, not 'all'. Other filters can still hide rows.

**Five simpler alternatives:**

1. Precomputes: {choices}; location: {location}.
2. Filter precomputes by {choices} in {location}.
3. Precompute selection: {choices}; location: {location}.
4. Precompute filter set to {choices}; location set to {location}.
5. Showing matching precomputes for {choices} in {location}.

### M012 — Good

**Current:** `Cannot load bin "%1": %2`

**Source:** [src/mainwindow.cpp:1124](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1124) · binfilter / WARN.

**When / values:** Path plus BinFilterDialog/AvbParser reasons; multiple reasons are joined with '; '. Complete list of authored reasons is in the Bin-loading section (B001–B047).

**Assessment:** Accurate: incomplete/unsupported bin results are rejected rather than used for filtering.

**Five simpler alternatives:**

1. Couldn't load bin {path}: {reason}.
2. Bin load failed: {path}. {reason}
3. Cannot use this bin: {path}. {reason}
4. Couldn't read bin {path}: {reason}.
5. Bin unavailable: {path}. {reason}

### M013 — Good

**Current:** `Bin filter cleared`

**Source:** [src/mainwindow.cpp:1153](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1153) · binfilter / INFO.

**When / values:** No active bin-filter chain.

**Assessment:** Accurate short status.

**Five simpler alternatives:**

1. Bin filter removed.
2. Bin filtering cleared.
3. No bin filter is applied.
4. Cleared the bin filter.
5. All files pass the bin filter.

### M014 — Good

**Current:** `Bin filter active — %1 operations`

**Source:** [src/mainwindow.cpp:1157](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1157) · binfilter / INFO.

**When / values:** Number of steps in the bin-filter chain, not file copy/move/delete operations.

**Assessment:** Accurate in the bin-filter context, but 'steps' or 'rules' avoids confusion with file operations.

**Five simpler alternatives:**

1. Bin filter active: {count} steps.
2. Applied {count} bin-filter steps.
3. Bin filter uses {count} rules.
4. Filtering with {count} bin rules.
5. Bin filtering enabled with {count} steps.

### M015 — Misleading

**Current:** `Opening rebalance dialog (%1 volume(s), default '%2')`

**Source:** [src/mainwindow.cpp:1242](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1242) · rebalance / INFO.

**When / values:** Count of eligible MXF-root choices and initially selected label; choices may be scan locations on the same physical volume.

**Assessment:** The count is MXF locations, not necessarily distinct physical volumes. 'Default' is an internal UI description.

**Recommended correction:** Opening Rebalance for {count} locations; selected: {label}.

### M016 — Good

**Current:** `Couldn't determine volume path for re-scan; please scan manually`

**Source:** [src/mainwindow.cpp:1271](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1271) · rebalance / WARN.

**When / values:** Rebalance ended with effects to refresh, but its label could not be resolved to a scan location.

**Assessment:** Accurate next action; use 'rescan' consistently.

**Five simpler alternatives:**

1. Couldn't find the location to rescan. Please scan it manually.
2. Automatic rescan unavailable. Scan the location manually.
3. Rebalance finished, but its scan location is unknown. Rescan manually.
4. Couldn't start the rescan. Select the location and scan it again.
5. Please rescan manually; the location could not be identified.

### M017 — Good

**Current:** `Re-scanning '%1' after rebalance`

**Source:** [src/mainwindow.cpp:1276](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1276) · rebalance / INFO.

**When / values:** Starts the scan after a Rebalance that may have moved files, including partial/cancelled runs.

**Assessment:** Accurate; does not claim the Rebalance completed every planned move.

**Five simpler alternatives:**

1. Rescanning {path} after Rebalance.
2. Refreshing {path} after Rebalance.
3. Scanning {path} again to update the table.
4. Updating the scan for {path}.
5. Rescan started for {path} after Rebalance.

### M018 — Good

**Current:** `Not an Avid media location. Add an Avid MediaFiles or OMFI MediaFiles folder, or its containing folder. %1`

**Source:** [src/mainwindow.cpp:1348](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1348) · volumes / WARN.

**When / values:** canScanPath rejected an added folder; %1 is the appended path, not a literal positional argument in this statement.

**Assessment:** Accurately rejects an unrecognised location, though 'couldn't recognise' describes the check more clearly. OMFI MediaFiles locations can be added with the OMF feature off; actually scanning their legacy media requires enabling that feature.

**Five simpler alternatives:**

1. Couldn't recognise {path}. Add an Avid MediaFiles or OMFI MediaFiles folder, or its parent.
2. Can't scan {path}. Choose an Avid media folder or its containing folder.
3. Unsupported location: {path}. Add an Avid MediaFiles or OMFI MediaFiles folder.
4. Add an Avid media folder or its parent. This location was not accepted: {path}.
5. Couldn't add {path}. Choose the Avid MediaFiles or OMFI MediaFiles folder, or its parent.

### M019 — Good

**Current:** `Added: %1`

**Source:** [src/mainwindow.cpp:1387](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1387) · volumes / INFO.

**When / values:** A manual scan location was added to the Volumes list.

**Assessment:** Accurate; naming the list avoids sounding like files were copied.

**Five simpler alternatives:**

1. Added location: {path}.
2. Added {path} to Volumes.
3. Scan location added: {path}.
4. Added folder: {path}.
5. {path} added to the scan locations.

### M020 — Misleading

**Current:** `Found %1 volumes (%2 with Avid MediaFiles)`

**Source:** [src/mainwindow.cpp:1445](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1445) · volumes / INFO.

**When / values:** Counts VolumeManager entries; Avid-folder flag checks either Avid MediaFiles or OMFI MediaFiles, and entries can include known system-drive scan bases.

**Assessment:** OMF-only locations are counted as 'with Avid MediaFiles'. Entries are also not always distinct physical volumes.

**Recommended correction:** Found {count} locations; {avidCount} contain Avid media folders.

### M021 — Good

**Current:** `Scan All: %1 locations`

**Source:** [src/mainwindow.cpp:1481](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1481) · scanner / INFO.

**When / values:** Scan All requested for the merged detected/manual scan locations.

**Assessment:** Accurate request count, not a claim that every location will yield files.

**Five simpler alternatives:**

1. Scanning all {count} locations.
2. Scan All started for {count} locations.
3. Starting a scan of {count} locations.
4. Scanning {count} listed locations.
5. Scan requested for all {count} locations.

### M022 — Good

**Current:** `Exporting %1 %2 to %3`

**Source:** [src/mainwindow.cpp:1860](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1860) · export / INFO.

**When / values:** %1 row count; %2 = records or selected records; %3 CSV path. Unselected export means visible filtered rows.

**Assessment:** Accurate export request. 'Rows' is clearer than 'records'. Preserve the selected/visible distinction in every alternative.

**Five simpler alternatives:**

1. Exporting {count} {selected/visible} rows to {path}.
2. Saving {count} {selected/visible} rows as CSV: {path}.
3. Writing CSV for {count} {selected/visible} rows: {path}.
4. CSV export started: {count} {selected/visible} rows to {path}.
5. Creating {path} from {count} {selected/visible} rows.

### M023 — Misleading

**Current:** `Exported %1 %2 to %3`

**Source:** [src/mainwindow.cpp:1875](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1875) · export / INFO.

**When / values:** Shown when MediaCsv::write returns true.

**Assessment:** The current writer checks QTextStream status before the final destructor flush. A late write failure can still produce this success message. This is the previously deferred export issue; wording alone does not establish success.

**Recommended correction:** After confirmed save: Saved {count} {selected/visible} rows to {path}.

### M024 — Good

**Current:** `Failed to write %1`

**Source:** [src/mainwindow.cpp:1880](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1880) · export / ERR.

**When / values:** CSV writer returned false.

**Assessment:** Accurate error, though it lacks an underlying reason.

**Five simpler alternatives:**

1. Couldn't save CSV: {path}.
2. CSV export failed: {path}.
3. Couldn't write {path}.
4. Failed to save {path}.
5. CSV file could not be written: {path}.

### M025 — Good

**Current:** `No master MOBs in selection — nothing to follow`

**Source:** [src/mainwindow.cpp:1925](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1925) · relatives / WARN.

**When / values:** Selected files supplied no usable MasterMobId; normal UI enables the command only when a selected master ID exists.

**Assessment:** Accurate defensive condition, not evidence that real Avid files normally lack IDs. Name the property clearly.

**Five simpler alternatives:**

1. No MasterMobId found in the selected files.
2. Selected files have no master clip IDs to match.
3. Can't find relatives without a MasterMobId.
4. No usable master clip IDs in this selection.
5. Relatives couldn't be matched: no MasterMobId is available.

### M026 — Misleading

**Current:** `No relatives found (the relatives may be filtered out)`

**Source:** [src/mainwindow.cpp:1942](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1942) · relatives / WARN.

**When / values:** Guard after matching visible rows to the selected MasterMobIds.

**Assessment:** The matching includes seed rows themselves. With the normal unchanged visible selection, those rows make this branch nonempty; it does not detect 'no additional relatives'. Filtering is only speculation here.

**Recommended correction:** For this existing defensive branch: No visible files match the selected master clip IDs.

If the intended purpose is to report that no *additional* files were selected, that needs a separate comparison with the original selection. Its wording could then be: No additional relatives to select in the current view.

### M027 — Misleading

**Current:** `Selected %1 relative%2 across %3 master clip%4`

**Source:** [src/mainwindow.cpp:1959](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1959) · relatives / INFO.

**When / values:** %1 is ALL matching visible files, including already-selected seed files. %3 is the number of distinct selected MasterMobIds. %2/%4 supply singular/plural endings.

**Assessment:** One matching file is not another relative. The count is neither newly found relatives nor newly added selections. This also omits that hidden/filtered files are excluded.

**Recommended correction:** Selected {total} files from {clips} master clips.

This uses the existing counts and only changes the wording. For the user's example: **Selected 1 file from 1 master clip.** A richer version, **Selected {total} visible files from {clips} master clips ({added} added)**, would require comparing the result with the original selection. Only then could zero added files be reported as **No additional relatives to select in the current view.**

### M028 — Good

**Current:** `Inverted selection: %1 of %2 visible row%3 selected`

**Source:** [src/mainwindow.cpp:1998](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:1998) · selection / INFO.

**When / values:** New selected count and current proxy-row count; %3 pluralises rows.

**Assessment:** Accurately limits the result to visible rows.

**Five simpler alternatives:**

1. Selection inverted: {selected} of {visible} visible rows selected.
2. Now selected: {selected} of {visible} visible files.
3. Inverted selection; {selected}/{visible} visible rows selected.
4. Selected the other visible rows: {selected} of {visible}.
5. Selection switched to {selected} of the {visible} visible rows.

### M029 — Good

**Current:** `Cancel requested`

**Source:** [src/mainwindow.cpp:2207](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:2207) · app or ops / WARN.

**When / values:** Scan cancel request; identical file-operation text at fileoperationcontroller.cpp:222.

**Assessment:** Correctly reports a request rather than claiming work has already stopped.

**Five simpler alternatives:**

1. Cancellation requested.
2. Stop requested.
3. Asked the scan or operation to stop.
4. Asked the current work to stop.
5. Cancellation requested; waiting for the current work to stop.

### M030 — Good

**Current:** `%1: Completed[ — %2]`

**Source:** [src/fileoperationcontroller.cpp:66](/Users/martymclean/Developer/MediaMuster/src/fileoperationcontroller.cpp:66) · ops / INFO.

**When / values:** Item name and optional result.message; suffix catalogue is the operation and file-error sections (O001–O111 and E001–E093).

**Assessment:** Reports a completed item, not completion of the entire job.

**Five simpler alternatives:**

1. {name}: Done{detail}.
2. {name}: Finished{detail}.
3. {name}: Operation complete{detail}.
4. Completed: {name}{detail}.
5. Finished processing {name}{detail}.

### M031 — Misleading

**Current:** `%1: Copied; source retained[ — %2]`

**Source:** [src/fileoperationcontroller.cpp:69](/Users/martymclean/Developer/MediaMuster/src/fileoperationcontroller.cpp:69) · ops / INFO.

**When / values:** SourceRetained state; includes a refused/declined Trash attempt with no copy made.

**Assessment:** The shared state does not guarantee that any copy exists. The source can simply have been left alone.

**Recommended correction:** {name}: Source retained{detail}.

### M032 — Good

**Current:** `%1: Original restored[ — %2]`

**Source:** [src/fileoperationcontroller.cpp:72](/Users/martymclean/Developer/MediaMuster/src/fileoperationcontroller.cpp:72) · ops / INFO.

**When / values:** OriginalRestored result; original returned to its original location.

**Assessment:** Accurate result state.

**Five simpler alternatives:**

1. {name}: Original returned{detail}.
2. {name}: Original file restored{detail}.
3. Restored original: {name}{detail}.
4. {name}: Original back in place{detail}.
5. Returned {name} to its original location{detail}.

### M033 — Good

**Current:** `%1: Already at destination[ — %2]`

**Source:** [src/fileoperationcontroller.cpp:75](/Users/martymclean/Developer/MediaMuster/src/fileoperationcontroller.cpp:75) · ops / INFO.

**When / values:** NoEffect result; runner emits this when source/destination already refer to the same file.

**Assessment:** Current NoEffect producer supports this specific description.

**Five simpler alternatives:**

1. {name}: Already in place{detail}.
2. {name}: Already in the destination{detail}.
3. {name}: No file changes needed{detail}.
4. {name}: Destination already contains this file{detail}.
5. {name}: Nothing to change; already at destination{detail}.

### M034 — Good

**Current:** `%1: Skipped[ — %2]`

**Source:** [src/fileoperationcontroller.cpp:78](/Users/martymclean/Developer/MediaMuster/src/fileoperationcontroller.cpp:78) · ops / INFO.

**When / values:** Item deliberately skipped or skipped because of an applicable conflict/group decision.

**Assessment:** Accurate broad state; the suffix explains why.

**Five simpler alternatives:**

1. Skipped: {name}{detail}.
2. {name}: Left out{detail}.
3. {name}: Processing skipped{detail}.
4. Did not process {name}{detail}.
5. {name}: Omitted from this operation{detail}.

### M035 — Good

**Current:** `%1: Cancelled[ — %2]`

**Source:** [src/fileoperationcontroller.cpp:81](/Users/martymclean/Developer/MediaMuster/src/fileoperationcontroller.cpp:81) · ops / INFO.

**When / values:** Item reached the cancellation outcome.

**Assessment:** A completed cancellation outcome, unlike the earlier cancellation request.

**Five simpler alternatives:**

1. {name}: Stopped{detail}.
2. Cancelled: {name}{detail}.
3. {name}: Operation cancelled{detail}.
4. Processing cancelled for {name}{detail}.
5. {name}: Work cancelled{detail}.

### M036 — Good

**Current:** `%1: Failed[ — %2]`

**Source:** [src/fileoperationcontroller.cpp:84](/Users/martymclean/Developer/MediaMuster/src/fileoperationcontroller.cpp:84) · ops / WARN.

**When / values:** Failed item outcome; details must be retained because partial effects can exist.

**Assessment:** Accurate broad result. Do not simplify this to 'nothing changed'.

**Five simpler alternatives:**

1. {name}: Could not finish{detail}.
2. {name}: Operation failed{detail}.
3. Failed: {name}{detail}.
4. {name}: Unsuccessful{detail}.
5. Couldn't complete the operation for {name}{detail}.

### M037 — Good

**Current:** `%1: Needs attention[ — %2]`

**Source:** [src/fileoperationcontroller.cpp:87](/Users/martymclean/Developer/MediaMuster/src/fileoperationcontroller.cpp:87) · ops / WARN.

**When / values:** Outcome needs inspection/recovery; not equivalent to a clean failure or success.

**Assessment:** Correctly conveys unresolved state, but the explanation is essential.

**Five simpler alternatives:**

1. {name}: Please check{detail}.
2. {name}: Review needed{detail}.
3. Check {name}{detail}.
4. {name}: Needs checking{detail}.
5. {name}: Result needs review{detail}.

### M038 — Good

**Current:** `Undoing the last operation.`

**Source:** [src/fileoperationcontroller.cpp:326](/Users/martymclean/Developer/MediaMuster/src/fileoperationcontroller.cpp:326) · ops / INFO.

**When / values:** Undo request passed dispatch; targets the latest eligible undo journal.

**Assessment:** Accurate start status for the selected Undo candidate.

**Five simpler alternatives:**

1. Undo started.
2. Undoing the previous operation.
3. Reversing the last operation.
4. Started Undo for the last operation.
5. Undoing the latest eligible file operation.

### M039 — Good

**Current:** `Restoring interrupted originals.`

**Source:** [src/fileoperationcontroller.cpp:497](/Users/martymclean/Developer/MediaMuster/src/fileoperationcontroller.cpp:497) · ops / INFO.

**When / values:** Restore-originals request passed dispatch.

**Assessment:** Meaning is sound; 'interrupted' should describe the operation rather than the originals.

**Five simpler alternatives:**

1. Restoring original files.
2. Returning original files to their previous locations.
3. Restoring originals from the interrupted operation.
4. Original-file restoration started.
5. Putting the original files back.

### M040 — Good

**Current:** `Stopped the unfinished job. Completed results were kept.`

**Source:** [src/fileoperationcontroller.cpp:513](/Users/martymclean/Developer/MediaMuster/src/fileoperationcontroller.cpp:513) · ops / INFO.

**When / values:** Dismiss/Stop succeeded; completed effects remain, remaining work is abandoned.

**Assessment:** Accurately distinguishes stopping remaining work from Undo.

**Five simpler alternatives:**

1. Unfinished job stopped; completed changes kept.
2. Stopped the remaining work. Completed changes remain.
3. Job stopped. Finished work was kept.
4. Remaining work cancelled; completed results kept.
5. Stopped the unfinished operation without undoing completed changes.

### M041 — Misleading

**Current:** `Resuming the previous job.`

**Source:** [src/fileoperationcontroller.cpp:533](/Users/martymclean/Developer/MediaMuster/src/fileoperationcontroller.cpp:533) · ops / INFO.

**When / values:** Logged before dispatchRequest, which can still refuse the request; the user can select a particular unfinished job.

**Assessment:** It claims a resume has started too early, and 'previous' need not mean the selected historical job.

**Recommended correction:** Resume requested for the selected job. Or emit Resuming the selected job only after dispatch succeeds.

### M042 — Misleading

**Current:** `Cannot reveal: file and parent folder are both unreachable`

**Source:** [src/revealinfinder.cpp:30](/Users/martymclean/Developer/MediaMuster/src/revealinfinder.cpp:30) · app or reveal / ERR.

**When / values:** Fallback sees an empty/missing parent path. File existence is not rechecked here.

**Assessment:** This branch only establishes that the parent folder cannot be found; it does not independently establish both objects are unreachable.

**Recommended correction:** Couldn't open the containing folder; its location is unavailable.

### M043 — Good

**Current:** `Couldn't open the parent folder: %1`

**Source:** [src/revealinfinder.cpp:34](/Users/martymclean/Developer/MediaMuster/src/revealinfinder.cpp:34) · app or reveal / WARN.

**When / values:** QDesktopServices rejected the parent-folder open request.

**Assessment:** Accurate failed open request.

**Five simpler alternatives:**

1. Couldn't open folder: {path}.
2. Folder could not be opened: {path}.
3. Couldn't show the containing folder: {path}.
4. Unable to open {path}.
5. Opening the folder failed: {path}.

### M044 — Good

**Current:** `open(1) and osascript both failed; opening parent`

**Source:** [src/revealinfinder.cpp:59](/Users/martymclean/Developer/MediaMuster/src/revealinfinder.cpp:59) · app or reveal / WARN / macOS.

**When / values:** Both reveal processes failed to start; the containing-folder fallback is next.

**Assessment:** Accurate for these start failures, but shell-command names are developer jargon. This message does not catch a process that starts and subsequently fails.

**Five simpler alternatives:**

1. Couldn't start Finder reveal; trying the containing folder.
2. Couldn't reveal the file; trying its folder.
3. Finder reveal could not start. Trying the folder instead.
4. File reveal failed to start; opening its folder next.
5. Trying the containing folder after both reveal attempts failed to start.

### M045 — Good

**Current:** `GetFullPathNameW failed; opening parent folder`

**Source:** [src/revealinfinder.cpp:84](/Users/martymclean/Developer/MediaMuster/src/revealinfinder.cpp:84) · app or reveal / WARN / Windows.

**When / values:** Path resolution returned failure or a required size beyond the fixed buffer.

**Assessment:** Describes an unusable resolution result; the API name need not be shown to an editor.

**Five simpler alternatives:**

1. Couldn't resolve the file path; trying its folder.
2. Couldn't get the full path; opening the containing folder next.
3. File path unavailable; trying the parent folder.
4. Couldn't prepare this path for Explorer; trying its folder.
5. Trying the folder because the full file path could not be resolved.

### M046 — Good

**Current:** `SHOpenFolderAndSelectItems failed: 0x%1`

**Source:** [src/revealinfinder.cpp:108](/Users/martymclean/Developer/MediaMuster/src/revealinfinder.cpp:108) · app or reveal / WARN / Windows.

**When / values:** Windows Shell file-selection call returned a failing HRESULT; %1 is eight hexadecimal digits.

**Assessment:** Accurate, but the function name is unnecessarily technical.

**Five simpler alternatives:**

1. Explorer couldn't select the file. Error 0x{code}.
2. Couldn't highlight the file in Explorer: 0x{code}.
3. File selection in Explorer failed: 0x{code}.
4. Windows couldn't reveal the file: 0x{code}.
5. Couldn't open and select this file. Windows error 0x{code}.

### M047 — Good

**Current:** `SHParseDisplayName failed: 0x%1`

**Source:** [src/revealinfinder.cpp:114](/Users/martymclean/Developer/MediaMuster/src/revealinfinder.cpp:114) · app or reveal / WARN / Windows.

**When / values:** Windows Shell did not return a usable item identifier; %1 is the HRESULT.

**Assessment:** Accurate at the operation level; use a description rather than the API name.

**Five simpler alternatives:**

1. Windows couldn't resolve this item: 0x{code}.
2. Couldn't locate the item in Explorer: 0x{code}.
3. Explorer couldn't recognise the path. Error 0x{code}.
4. Couldn't prepare the file for Explorer: 0x{code}.
5. Windows item lookup failed: 0x{code}.

### M048 — Good

**Current:** `Shell API + explorer.exe /select both failed; opening parent`

**Source:** [src/revealinfinder.cpp:131](/Users/martymclean/Developer/MediaMuster/src/revealinfinder.cpp:131) · app or reveal / WARN / Windows.

**When / values:** Native Shell reveal failed, and Explorer fallback could not start.

**Assessment:** Accurate, but overly technical; keep 'trying' because the folder may also fail to open.

**Five simpler alternatives:**

1. Couldn't reveal the file in Explorer; trying its folder.
2. File reveal failed; trying the containing folder.
3. Couldn't select the file. Opening its folder next.
4. Explorer reveal failed; trying the parent folder.
5. Trying the file's folder after both reveal attempts failed.

### M049 — Good

**Current:** `No file path to reveal`

**Source:** [src/revealinfinder.cpp:143](/Users/martymclean/Developer/MediaMuster/src/revealinfinder.cpp:143) · app or reveal / ERR.

**When / values:** Reveal callback received an empty path.

**Assessment:** Accurate defensive error.

**Five simpler alternatives:**

1. No file location is available.
2. Can't reveal the file without its path.
3. File path is empty.
4. No file location to open.
5. Couldn't reveal the file: its path is missing.

### M050 — Good

**Current:** `File not found, opening parent folder: %1`

**Source:** [src/revealinfinder.cpp:153](/Users/martymclean/Developer/MediaMuster/src/revealinfinder.cpp:153) · app or reveal / WARN.

**When / values:** QFileInfo could not find the target; attempts the parent-folder fallback.

**Assessment:** Accurately describes the lookup and next attempt. Do not imply successful folder opening or prove permanent deletion.

**Five simpler alternatives:**

1. Couldn't find the file; trying folder {path}.
2. File not found. Trying its folder: {path}.
3. The file isn't available; opening its folder next: {path}.
4. Can't locate the file. Trying the containing folder: {path}.
5. Trying {path} because the file could not be found.

### M051 — Good

**Current:** `Journal cleanup: {detail}`

**Source:** [src/fileoperationcontroller.cpp:254](/Users/martymclean/Developer/MediaMuster/src/fileoperationcontroller.cpp:254) · app / INFO or WARN according to the recovery summary.

**When / values:** Journal pruning returned false before the startup recovery sweep. `{detail}` is the journal error, catalogued in the file-operation detail section; it can describe lock acquisition, retention dates, saving records or deleting expired records.

**Assessment:** Accurate context for the error, but saying cleanup failed is clearer.

**Five simpler alternatives:**

1. Couldn't finish journal cleanup: {detail}
2. Journal cleanup failed: {detail}
3. Couldn't clean up operation history: {detail}
4. Operation history cleanup stopped: {detail}
5. Couldn't complete operation history cleanup: {detail}

## Scanning and media metadata

This inventories the in-app Console messages emitted by `MediaScanner`, including the folder-worker buffers. [The signal is connected to the Console here](/Users/martymclean/Developer/MediaMuster/src/mainwindow.cpp:817). Parser `qCDebug`, `qCInfo`, `qCWarning` and `qCCritical` messages go to Diagnostics; they are not additional Console messages. Progress-dialog labels and the MainWindow `Scan All` announcement are covered separately.

There are **38 entries: 25 Good and 13 Misleading**, counting the optional changed-file suffix separately. Repeated wording at different call sites and finite filename/feature-flag variants are grouped and explicitly listed. Each Good entry has five alternatives. Alternatives use named placeholders rather than Qt's numbered placeholders; `{file/files}` means normal singular/plural handling. Leading spaces in quoted current text are intentional. No production wording has been changed.

### S001 — Good — Starting a scan

**Current:** `Scanning %1 location(s)...`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:332). Info, `scanner`. `%1` is the number of requested volume and manually added paths, before unsupported paths or duplicates are skipped.

**Good.** A start announcement, not a claim that every requested location was successfully read. Replace `(s)` with normal plural handling.

1. `Scanning {n} {location/locations}…`
2. `Starting scan: {n} {location/locations}`
3. `Scan started for {n} {location/locations}`
4. `Checking {n} {location/locations} for media…`
5. `Looking for media in {n} {location/locations}…`

### S002 — Good — Unsupported UME location

**Current:** `Skipping unsupported UME media folder: %1`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:351). Info, `scanner`. `%1` is the requested path; its direct or canonical path is inside an unsupported UME media tree.

**Good.** Gives both the action and its reason.

1. `Skipped UME folder: {path}`
2. `UME media is unsupported: {path}`
3. `Cannot scan UME media: {path}`
4. `Skipping UME media: {path}`
5. `UME folder not scanned: {path}`

### S003 — Misleading — Unreadable location or folder

**Current variants:** `Permission denied: %1` and `Permission denied: %1`

[Location source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:367), [OMF root](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:607), [MXF root](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:650), [MXF subfolder](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:669). Critical for locations/roots, Warning for MXF subfolders; `scanner`. `%1` is a full path except the last case, which supplies just the folder name.

**Misleading.** The code checks only whether the path can be read. It does not inspect the cause: a removed/disconnected location also fails. The subfolder version does not identify its full location.

**Recommended:** `Cannot read folder: {full path}`. Include an actual system error only if the operation obtains one.

### S004 — Misleading — Full Disk Access advice

**Current:** `Grant Full Disk Access in System Preferences > Privacy & Security`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:369). Warning, `scanner`. Follows every failed top-level readability check, on both platforms.

**Misleading.** It assumes Full Disk Access is the cause, appears on Windows too, and combines the old macOS “System Preferences” name with the newer “Privacy & Security” wording.

**Recommended:** `Check that the drive is connected and MediaMuster can access this folder.` Only show specific macOS access instructions when relevant.

### S005 — Good — Location being scanned

**Current:** `Scanning: %1 (%2)`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:374). Info, `scanner`. `%1` is the location's final path component (or the path itself if that is empty); `%2` is the requested full path.

**Good.** Correctly identifies the current location. The display name is redundant when the path is already shown.

1. `Scanning {path}`
2. `Checking {path}`
3. `Looking for media in {path}`
4. `Scan started: {path}`
5. `Reading media information from {path}`

### S006 — Good — Files found at one location

**Current:** `%1: %2 media files found`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:383). Info, `scanner`. `%1` is the location display name; `%2` is the number of recognised media files returned for it, greater than zero. This may be a partial count if cancellation happened during that location.

**Good.** Says files were found, not that the location scan completed.

1. `{location}: found {n} media {file/files}`
2. `Found {n} media {file/files} in {location}`
3. `{location}: {n} media {file/files}`
4. `Media found in {location}: {n} {file/files}`
5. `{n} media {file/files} found in {location}`

### S007 — Good — No scan results

**Current:** `No media files found.`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:458). Warning, `scanner`. An uncancelled scan returned no recognised media files. It does not prove there are no files outside supported locations or formats.

**Good.** “Found” accurately describes the scan result. Warning severity is a separate choice; an empty folder is not necessarily a problem.

1. `No media found.`
2. `No supported media found.`
3. `Scan finished: no media found.`
4. `No media found in the scanned locations.`
5. `Found 0 media files.`

### S008 — Good — Scan completed with results

**Current:** `Scan complete: %1 files found`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:462). Info, `scanner`. `%1` is the complete result count for an uncancelled scan; individual unreadable locations may have been skipped and reported separately.

**Good.** Accurately reports that the scan ended and how many files it found.

1. `Scan complete: {n} media {file/files}`
2. `Found {n} media {file/files}. Scan complete.`
3. `Scan finished: {n} {file/files} found`
4. `Scanned locations: {n} media {file/files} found`
5. `Finished scanning. Found {n} media {file/files}.`

### S009 — Misleading — No database reference summary

**Current:** `%1 file%2 with no local database reference`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:467). Warning, `scanner`. `%1` counts `DbStatus::NoReference`; `%2` is empty or `s`.

**Misleading.** This status specifically means the folder's readable **PMR index** did not name the file. The MDB can still contain information about the file or its master clip; “no local database reference” is broader than the test performed. [Status definition](/Users/martymclean/Developer/MediaMuster/src/mediafile.h:140).

**Recommended:** `{n} {file/files} not listed in the local PMR index`.

### S010 — Misleading — Missing/unreadable database summary

**Current:** `%1 file%2 in folders with no readable database`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:472). Warning, `scanner`. Counts files in `NoDatabase` or `DbUnreadable` states; `%2` is empty or `s`.

**Misleading.** A PMR can be missing while the MDB is readable, or one database can be unreadable while another works. The code does not establish that the folder has *no readable database*. [Folder-status conditions](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:776).

**Recommended:** `Database references could not be checked for {n} {file/files}`.

### S011 — Good — All-zero identifier summary

**Current:** `%1 file%2 with an invalid (all-zero) UMID`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:477). Warning, `scanner`. `%1` counts files where an available file or master identity is all zeros; `%2` is empty or `s`. This does not flag IDs simply because they are missing.

**Good.** The parenthesis correctly states the exact invalidity checked. The affected identity may be the file's MobId or its MasterMobId.

1. `{n} {file/files} with an all-zero media ID`
2. `All-zero media ID found for {n} {file/files}`
3. `{n} {file/files} have an all-zero MobId or MasterMobId`
4. `Zero-filled media IDs found in {n} {file/files}`
5. `Invalid all-zero media ID: {n} affected {file/files}`

### S012 — Misleading — Missing project summary

**Current:** `%1 file%2 with no project name anywhere`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:480). Warning, `scanner`. `%1` counts empty `MediaFile::project` values after the available reads; `%2` is empty or `s`.

**Misleading.** “Anywhere” claims that the name does not exist. The scan only establishes that it did not retrieve one, including when a source could not be read.

**Recommended:** `No project name found for {n} {file/files}`.

### S013 — Misleading — Filename portability summary

**Current:** `%1 non-portable filename%2`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:483). Warning, `scanner`. `%1` counts filenames containing a character outside the scanner's ASCII allowlist; `%2` is empty or `s`.

**Misleading.** This is a conservative character test, not proof that a filename fails on another platform. For example, every non-ASCII letter triggers it. [Actual allowlist](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:1375).

**Recommended:** `{n} {filename/filenames} may have compatibility issues`.

### S014 — Good — Cancelled scan

**Current:** `Scan cancelled by user`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:497). Warning, `scanner`. The scanner observed cancellation; it retains and returns the partial results already collected.

**Good.** Correct for the normal Cancel action. “By user” is unnecessary; cancellation can also be requested during shutdown.

1. `Scan cancelled`
2. `Scanning cancelled`
3. `Scan stopped`
4. `Scan cancelled; partial results kept`
5. `Stopped scanning; showing results found so far`

### S015 — Misleading — Folder count warning and attached rows

**Current:** `%1 folder(s) over %2 files (Avid recommends staying under %3):`

**Repeated continuation:** `\n  %1 — %2 files`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:505). Warning, `scanner`. In the heading, `%1` is the number of MXF folders over the warning threshold; `%2` is **4500**, `%3` is **5000**. Each continuation substitutes the volume/folder display name and its recognised MXF count. Emitted only at normal completion, with all qualifying folders in one message.

**Misleading.** The counts are correct, but the warning mixes MediaMuster's 4500-file threshold with a stricter-than-needed statement of Avid's guidance. Avid says Media Composer creates a new folder when the previous one reaches **5000**, and that folders over 5000 can generally work but take longer to index. That is not a hard maximum of 4999. [Avid's explanation](https://kb.avid.com/pkb/articles/en_US/Knowledge/Avid-MediaFiles-MXF-folder-size-limit).

**Recommended:** `{n} {folder/folders} contain over 4500 files. Aim for 5000 or fewer per folder:` followed by `{folder}: {n} files` for each listed folder. This is guidance for manageable folder sizes, not a claim that larger folders cannot work.

### S016 — Good — MXF root found

**Current:** `Found Avid MediaFiles/MXF`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:545). Info, `scanner`. An MXF root was found directly beneath the current volume/base location.

**Good.** Reports the folder's existence, not that it contains media.

1. `Found MXF media folder`
2. `Avid MediaFiles/MXF found`
3. `MXF media folder found`
4. `Found folder: Avid MediaFiles/MXF`
5. `Located Avid MediaFiles/MXF`

### S017 — Good — OMF root found

**Current:** `Found OMFI MediaFiles`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:554). Info, `scanner`. **OMF feature enabled only**; an OMF root was found directly beneath the current volume/base location.

**Good.** Reports the folder's existence, not that it contains media.

1. `Found OMF media folder`
2. `OMFI MediaFiles found`
3. `OMF media folder found`
4. `Found folder: OMFI MediaFiles`
5. `Located OMFI MediaFiles`

### S018 — Misleading — No media at the volume root

**Current:** `No %1 at the root of %2 (media in a subfolder is found via File > Add Folder or Volume)`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:563). Warning, `scanner`. `%1` is `Avid MediaFiles` with OMF off, or `Avid MediaFiles or OMFI MediaFiles` with OMF on. `%2` is the location name.

**Misleading.** This is triggered by an **empty file result**, not by missing folders. It can immediately follow “Found Avid MediaFiles/MXF” if that tree contains no recognised files, cannot be read, or scanning was cancelled.

**Recommended:** `No media found in the supported media folders on {location}. Use File > Add Folder or Volume to scan another location.` If retaining the current missing-folder claim, change its condition to check whether those roots actually exist.

### S019 — Good — Unsupported manually added location

**Current:** `Not an Avid media location: %1. Add an Avid MediaFiles or OMFI MediaFiles folder, or its containing folder.`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:581). Warning, `scanner`. `%1` is the manually requested folder; no supported media root could be resolved from it.

**Good.** The folder guidance matches the location-recognition rules, which accept recognised OMFI MediaFiles locations regardless of the feature flag. Actually scanning their legacy media requires the OMF feature; with it off, the scanner skips that family. “Could not recognise” would describe the recognition failure more directly than “Not an Avid media location.”

1. `No recognised media folder at {path}. Add Avid MediaFiles, OMFI MediaFiles or their containing folder.`
2. `Cannot recognise {path} as an Avid media location. Add its Avid MediaFiles or OMFI MediaFiles folder, or the containing folder.`
3. `Avid media folder not found at {path}. Choose Avid MediaFiles, OMFI MediaFiles or their containing folder.`
4. `Cannot find an Avid media folder at {path}. Add the media folder or its containing folder.`
5. `Unrecognised media location: {path}. Choose Avid MediaFiles, OMFI MediaFiles or the folder containing them.`

### S020 — Good — MXF folder queue

**Current:** `%1 subfolders queued for concurrent scanning`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:683). Info, `scanner`. `%1` is the number of recognised, readable MXF child folders placed in the task list, including Quarantined Files. Can be zero.

**Good.** Correct, but “queued” and “concurrent” expose implementation details that the user does not need.

1. `Scanning {n} media {folder/folders}`
2. `{n} media {folder/folders} to scan`
3. `Checking {n} media {folder/folders}`
4. `Starting scan of {n} media {folder/folders}`
5. `Ready to scan {n} media {folder/folders}`

### S021 — Good — Empty quarantine folder

**Current:** `Quarantined Files folder on %1 is empty`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:802). Info, `scanner`. `%1` is the location name. The folder's non-hidden, non-symlink regular-file listing is empty; this does not recursively scan anything.

**Good.** Appropriate for the expected flat Avid quarantine folder. “No files found” is more exact about the scan's scope than “is empty.”

1. `No quarantined files found on {location}`
2. `Quarantined Files is empty on {location}`
3. `{location}: no quarantined files found`
4. `No files found in Quarantined Files on {location}`
5. `Quarantined Files on {location}: no files found`

### S022 — Good — Quarantined MXF files

**Current:** `⚠️ Avid Quarantined Files folder on %1 contains %2 MXF file(s)!`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:805). Warning, `scanner`. `%1` is the location name; `%2` is the number of visible regular files with an MXF suffix in Quarantined Files, greater than zero.

**Good.** Correctly reports Avid's quarantine location. The warning icon, exclamation mark and `(s)` are unnecessary.

1. `{n} quarantined MXF {file/files} on {location}`
2. `{location}: {n} MXF {file/files} in Quarantined Files`
3. `Found {n} quarantined MXF {file/files} on {location}`
4. `Quarantined Files on {location}: {n} MXF {file/files}`
5. `{location} has {n} quarantined MXF {file/files}`

### S023 — Good — Quarantine contains only other file types

**Current:** `Quarantined Files folder on %1 contains %2 non-MXF file(s)`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:811). Info, `scanner`. `%1` is the location; `%2` is the visible regular-file count. No MXF files were found, but other files were present. This can include database files; it does not claim that those are quarantined media.

**Good.** Accurately distinguishes other files from MXF media.

1. `Quarantined Files on {location}: {n} non-MXF {file/files}`
2. `{location}: {n} non-MXF {file/files} in Quarantined Files`
3. `Found {n} non-MXF {file/files} in Quarantined Files on {location}`
4. `No MXF files in Quarantined Files on {location}; {n} other {file/files}`
5. `Quarantined Files on {location} has {n} other {file/files}, no MXF files`

### S024 — Good — Per-folder metadata coverage

**Current:** `/%1: %2 media file(s), %3 described by the databases, %4 need a header read`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:843). Info, `scanner`. `%1` is the folder name; `%2` is all recognised files collected there; `%3` can use current database metadata without reading the media; `%4` requires a media metadata read. Zero-byte files are counted in `%2` but neither `%3` nor `%4`, so those two counts need not add to the total. Can have the S025 suffix.

**Good.** The counts describe the two metadata sources, although the current text is wordy. These are planned reads, not confirmations that reads succeeded.

1. `{folder}: {total} media files; {database} use database metadata, {read} need file reads`
2. `{folder}: {total} files; database metadata for {database}, read metadata from {read}`
3. `{folder}: {total} media files; {database} covered by databases, {read} to check`
4. `{folder}: {total} files; {database} need no media read, {read} need one`
5. `{folder}: {total} files; {database} ready from databases, {read} need metadata reads`

### S025 — Misleading — Optional changed-since-indexing suffix

**Current:** `(%1 changed since Avid indexed them)`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:849). Appended to S024 when `tally.stale` is greater than zero. `%1` counts files described by MDB and PMR whose database freshness could not be established.

**Misleading.** The test includes a **missing PMR timestamp**, as well as a mismatch with the filesystem timestamp. Neither a missing timestamp nor a timestamp change proves that the media bytes changed. [Counter condition](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:1062).

**Recommended:** `({n} need their database metadata checked against the media)`.

### S026 — Good — PMR entries read

**Current:** `%1: %2 file entries in /%3`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:907). Info, `pmr`. `%1` is `PMR` for msmFMID.pmr, or `amaFMID.pmr` for the alternate file; `%2` is the number of filename keys in the parsed index; `%3` is the folder name. It counts indexed names, not files verified to exist on disk.

**Good.** “Entries” correctly identifies database information rather than found media files.

1. `{folder}: {n} filenames in {database}`
2. `{database} in {folder}: {n} file entries`
3. `Read {n} filenames from {database} in {folder}`
4. `{folder}: {database} lists {n} files`
5. `{database}: {n} indexed filenames in {folder}`

### S027 — Good — PMR unreadable, status affected

**Current:** `%1 in /%2 is unreadable; unmatched files here surface as 'No database', not 'No reference'`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:919). Warning, `pmr`. `%1` is `msmFMID.pmr` or `amaFMID.pmr`; `%2` is the folder name. Triggered if the primary PMR fails to parse, or the alternate fails without a successfully read primary. “Unmatched” means not listed by the successfully parsed PMR entries, if any.

**Good.** Explains the effect of an unreadable index. “Surface as” is jargon, and the table's exact visible label is “No Database.”

1. `Cannot read {database} in {folder}; unlisted files show “No Database”`
2. `{database} is unreadable in {folder}; unlisted files are marked “No Database”`
3. `{folder}: cannot read {database}. Unlisted files show “No Database”`
4. `Could not read {database} in {folder}; “No Database” applies to unlisted files`
5. `{database} read failed in {folder}; unlisted files cannot be checked`

### S028 — Good — Alternate PMR unreadable, primary retained

**Current:** `%1 in /%2 is unreadable; ignored, the msmFMID.pmr index stands`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:927). Info, `pmr`. In the current filename list, `%1` can only be `amaFMID.pmr`; `%2` is the folder name. The alternate could not be parsed, but msmFMID.pmr was read successfully.

**Good.** Correctly says that the successful primary index remains available. “Stands” is unnecessary wording.

1. `Cannot read amaFMID.pmr in {folder}; using msmFMID.pmr`
2. `{folder}: using msmFMID.pmr; amaFMID.pmr is unreadable`
3. `Skipped unreadable amaFMID.pmr in {folder}; msmFMID.pmr loaded`
4. `amaFMID.pmr failed in {folder}; msmFMID.pmr remains available`
5. `{folder}: msmFMID.pmr loaded, amaFMID.pmr skipped`

### S029 — Good — No PMR found

**Current:** `No msmFMID.pmr in /%1`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:933). Info, `pmr`. `%1` is the folder name. This only appears when **neither** msmFMID.pmr nor amaFMID.pmr exists.

**Good.** True, but naming the PMR type conveys the actual broader check more clearly.

1. `No PMR index in {folder}`
2. `{folder}: no PMR index found`
3. `PMR index not found in {folder}`
4. `{folder} has no PMR file index`
5. `No local PMR file index found: {folder}`

### S030 — Good — MDB records read

**Current:** `%1: %2 clips, %3 files in /%4`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:953). Info, `mdb`. `%1` is `MDB` for msmMMOB.mdb, or `amaMMOB.mdb` for the alternate; `%2` is the number of master-mob records, `%3` is the number of file-mob records; `%4` is the folder name. These are database record counts, not a count of files currently on disk.

**Good.** With the MDB label, the intended meaning is database contents. Adding “records” removes any remaining ambiguity.

1. `{database} in {folder}: {clips} clip records, {files} file records`
2. `{folder}: {database} lists {clips} clips and {files} files`
3. `Read {clips} clip and {files} file records from {database} in {folder}`
4. `{database}: {clips} clips, {files} file records ({folder})`
5. `{folder}: loaded {clips} clips and {files} file records from {database}`

### S031 — Good — MDB unreadable, status affected

**Current:** `%1 in /%2 is unreadable; unmatched files here surface as 'No database', not 'No reference'`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:977). Warning, `mdb`. `%1` is `msmMMOB.mdb` or `amaMMOB.mdb`; `%2` is the folder. Triggered if the primary MDB fails to parse, or the alternate fails without a successfully read primary. The status change applies to files not listed by the available PMR index.

**Good.** Accurately describes the implemented status rule, although this repeats S027's jargon.

1. `Cannot read {database} in {folder}; unlisted files show “No Database”`
2. `{database} is unreadable in {folder}; unlisted files are marked “No Database”`
3. `{folder}: cannot read {database}. Unlisted files show “No Database”`
4. `Could not read {database} in {folder}; “No Database” applies to unlisted files`
5. `{database} read failed in {folder}; unlisted files cannot be fully checked`

### S032 — Good — Alternate MDB unreadable, primary retained

**Current:** `%1 in /%2 is unreadable; ignored, the msmMMOB.mdb records stand`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:985). Info, `mdb`. In the current filename list, `%1` can only be `amaMMOB.mdb`; `%2` is the folder name. msmMMOB.mdb was successfully read first.

**Good.** Accurate fallback report; it can be much shorter.

1. `Cannot read amaMMOB.mdb in {folder}; using msmMMOB.mdb`
2. `{folder}: using msmMMOB.mdb; amaMMOB.mdb is unreadable`
3. `Skipped unreadable amaMMOB.mdb in {folder}; msmMMOB.mdb loaded`
4. `amaMMOB.mdb failed in {folder}; msmMMOB.mdb remains available`
5. `{folder}: msmMMOB.mdb loaded, amaMMOB.mdb skipped`

### S033 — Good — No MDB found

**Current:** `No msmMMOB.mdb in /%1`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:991). Info, `mdb`. `%1` is the folder name. This appears when neither msmMMOB.mdb nor amaMMOB.mdb exists.

**Good.** True, but naming the MDB type makes the full check clearer.

1. `No MDB database in {folder}`
2. `{folder}: no MDB database found`
3. `MDB database not found in {folder}`
4. `{folder} has no MDB database`
5. `No local MDB database found: {folder}`

### S034 — Good — MXF metadata reads starting

**Current:** `Reading MXF headers for %1 file(s) needing metadata verification`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:1282). Info, `scanner`. `%1` is the number of files scheduled for MXF metadata reads. Used whenever there are no OMF-family reads, even if the OMF feature is enabled.

**Good.** These files need actual media reads because database metadata is incomplete or cannot be relied upon. “Metadata verification” is heavier than necessary.

1. `Reading metadata from {n} MXF {file/files}`
2. `Checking metadata in {n} MXF {file/files}`
3. `Reading {n} MXF {header/headers}`
4. `{n} MXF {file/files} need metadata reads`
5. `Checking {n} MXF {file/files} for metadata`

### S035 — Misleading — Mixed MXF/legacy metadata reads starting

**Current:** `Reading MXF/OMF headers for %1 file(s) needing metadata verification (%2 MXF, %3 OMF)`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:1285). Info, `scanner`. **OMF feature enabled only**, with at least one OMF-family read. `%1` is total planned reads, `%2` the MXF count (possibly zero), `%3` the legacy count, which includes supported OMF, AIF/AIFC/AIFF and WAV media in OMFI MediaFiles.

**Misleading.** It labels every legacy file “OMF” as though that were its file format. The selection is by managed media family, and can include audio files. “Headers” also hides that the legacy parser reads embedded OMF/Bento metadata.

**Recommended:** `Reading media metadata: {mxf} MXF {file/files}, {legacy} {file/files} from OMFI MediaFiles`.

### S036 — Misleading — MXF read statistics

**Current:** `MXF parse: %1 files, avg %2 KB/file, max %3 KB, total %4 MB read`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:1349). Info, `mxf`. `%1` is scheduled MXF files; `%2` is recorded bytes divided by that scheduled count and then 1024; `%3` is the maximum per-file recorded bytes divided by 1024; `%4` is total recorded bytes divided by 1024². All are integer truncations.

**Misleading.** After cancellation, some scheduled files may never be attempted, yet the message still uses the full scheduled count and divides the average by it. “Parse” can also read as successful parsing even when a read failed.

**Recommended:** `MXF metadata reads: {attempted} of {planned} files attempted; {total} MiB read` with actual attempted counts, or omit this developer-oriented statistic from the Console and keep it in Diagnostics.

### S037 — Misleading — Legacy read statistics

**Current:** `OMF parse: %1 files, avg %2 KB/file, max %3 KB, total %4 KB read`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:1362). Info, `omf`. **OMF feature enabled only** with at least one scheduled legacy file. `%1` is scheduled files, `%2` average bytes per scheduled file / 1024, `%3` maximum bytes / 1024, `%4` total bytes / 1024, all truncated integers.

**Misleading.** Same scheduled-versus-attempted problem as S036, plus “OMF” includes supported legacy audio files rather than identifying their actual file format.

**Recommended:** `OMFI MediaFiles metadata reads: {attempted} of {planned} files attempted; {total} KiB read` with actual attempted counts, or move the statistic to Diagnostics.

### S038 — Misleading — MDB match using identity read from media

**Current:** `Recovered %1 file(s) via MDB / UMID lookup`

[Source](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:1370). Info, `mdb`. `%1` counts media rows whose header-derived master identity matched an MDB master record during the metadata pass. The same code handles MXF and enabled legacy media.

**Misleading.** No files were restored or recovered. A database record was matched and applied, and the counter increments even if it adds no previously missing metadata. [Actual increment condition](/Users/martymclean/Developer/MediaMuster/src/mediascanner.cpp:1227).

**Recommended:** `Matched {n} {file/files} to MDB clip records using their media IDs`.

## Bin-loading errors and their context

There are **47 entries: 40 Good and 7 Misleading**. These are the reasons inserted into MainWindow's `Cannot load bin "%1": %2` message, rather than separate Console events. The outer message is inventoried in the MainWindow section.

[BinFilterDialog joins the error and warnings with `;`](/Users/martymclean/Developer/MediaMuster/src/binfilterdialog.cpp:549). Unsupported-data failures may contain the B002 prefix, up to 32 unique warnings, and nested B009/B010 context. An error after earlier warnings can include both. The first two header-inspection paths report a drag rejection before a drop occurs. No parser `qCWarning` line is included: the separately forwarded parser reason is what reaches the Console.

The list preserves all app-owned reason text, including duplicate variants and reporting fallbacks. B006 is an open-ended Qt/OS error family, not a promise that every OS-generated translation can be enumerated. B008 normally does not reach the Console because cancelled, removed rows have no remaining receiver. All Good entries have five wording alternatives; low-level identifiers and numbers are retained where they aid diagnosis. No production text was changed.

### B001 — Good — Wrong file extension

**Current:** `Choose an Avid bin file with an .avb extension.`

[Source](/Users/martymclean/Developer/MediaMuster/src/binfilterdialog.cpp:468). Shown for non-.avb paths, both during drag entry (line 394) and when adding a file. Extension checking is case-insensitive.

**Good.** Clear, actionable input guidance.

1. `Choose an .avb bin file.`
2. `Select an Avid .avb file.`
3. `Use an Avid bin file (.avb).`
4. `Only .avb bin files can be added.`
5. `Add a file with the .avb extension.`

### B002 — Good — Bin contains unsupported data

**Current:** `This bin contains data that MediaMuster does not yet support`

[Source](/Users/martymclean/Developer/MediaMuster/src/binfilterdialog.cpp:553). Prefix added when parsing returned valid=true but complete=false. Unsupported-feature warnings follow it, separated by '; '.

**Good.** Correctly attributes the limitation to MediaMuster, rather than claiming the bin is corrupt.

1. `MediaMuster cannot read all the data in this bin.`
2. `This bin uses features MediaMuster does not support.`
3. `Some data in this bin is unsupported.`
4. `MediaMuster does not yet support this bin's contents.`
5. `This bin contains unsupported data.`

### B003 — Good — Generic incomplete-bin fallback

**Current:** `The bin could not be read completely.`

[Source](/Users/martymclean/Developer/MediaMuster/src/binfilterdialog.cpp:558). Fallback only when reportLoadFailure receives no error or warning and valid=false. The current production parser supplies a reason for its failure paths, so this is not expected in normal use; it remains a possible template in the reporting function.

**Good.** Accurately admits incomplete reading without guessing a cause.

1. `Could not read the whole bin.`
2. `The bin could not be fully read.`
3. `Bin reading was incomplete.`
4. `Could not finish reading this bin.`
5. `This bin could not be loaded completely.`

### B004 — Good — Bin no longer exists

**Current:** `This bin file no longer exists.`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:1072). Header inspection during a drag finds that the path does not exist.

**Good.** Clear missing-file explanation. Removing 'no longer' avoids assuming that the path previously existed.

1. `Bin file not found.`
2. `This bin file was not found.`
3. `Cannot find this bin file.`
4. `The bin file is missing.`
5. `No file found at this bin path.`

### B005 — Good — Not a regular file

**Current:** `This path is not a regular file. | AVB path is not a regular file.`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:1074). The first exact variant is from header inspection (line 1074); the second is from full parsing (line 1102). QFileInfo::isFile() is false. The full parser also uses it when the path has disappeared.

**Good.** Correct, though 'regular file' is filesystem jargon and does not tell the user whether the path is missing or a directory.

1. `This path is not a readable bin file.`
2. `No bin file found at this path.`
3. `This is not a file MediaMuster can load.`
4. `Choose a bin file at an existing file path.`
5. `This path does not point to a bin file.`

### B006 — Good — Qt/system file error

**Current:** `{QFile::errorString()}`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:1077). Header probe open failures (line 1077), probe read failures (line 1083), and full-parser open failures (line 1108). The exact string comes from Qt/the OS and may be localised; it is not a finite set of app-owned wording. It is inserted in the outer Cannot load bin message unchanged.

**Good.** Retaining the actual system reason is more accurate than guessing. The alternatives below preserve that reason verbatim.

1. `File error: {system reason}`
2. `Could not read the bin: {system reason}`
3. `Bin file access failed: {system reason}`
4. `Cannot open or read this file: {system reason}`
5. `The file could not be read ({system reason}).`

### B007 — Misleading — Unrecognised header during a drag

**Current:** `This file is not an Avid bin.`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:1082). Header probe read fewer than 21 signature bytes without a reported read error, or the signature did not match either supported byte order (line 1086).

**Misleading.** The check establishes only that the supported header was not recognised. A truncated or damaged AVB can produce the same result; the wording rules that out.

**Recommended:** `This file's Avid bin header was not recognised.`

### B008 — Good — Cancelled bin parsing

**Current:** `Bin reading cancelled.`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:1097). Used before parsing, during object reads (line 120), during composition processing (line 380), sorting (line 398), and final validation (line 408). Reader cancellation adds the B009 byte suffix. Normally not displayed in today's UI: removing a loading row cancels its job and causes completeBinLoad to discard that result; destroying the dialog removes its callback.

**Good.** Correct cancellation wording. Included for completeness as a parser reason, with normal Console suppression explicitly noted.

1. `Bin load cancelled.`
2. `Reading the bin was cancelled.`
3. `Cancelled reading this bin.`
4. `Stopped reading the bin.`
5. `Bin reading stopped.`

### B009 — Good — Byte-position suffix

**Current:** `%1 (byte %2)`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:124). Reader::fail and Reader::unsupported (line 128) wrap the reason in %1 with the current absolute file cursor position in %2. It is the detection position, which can be just after the offending value; it is not necessarily the exact byte that is wrong.

**Good.** Useful diagnostic context. Keep this detail if retaining low-level parser reasons in the Console.

1. `{reason} at byte {offset}`
2. `{reason} — byte {offset}`
3. `{reason} [byte {offset}]`
4. `{reason}; detected at byte {offset}`
5. `Byte {offset}: {reason}`

### B010 — Good — Object context around an unsupported feature

**Current:** `Object %1 (%2): %3`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:372). Wraps an Unsupported exception as a warning. %1 is the object ordinal, %2 its four-character class code, %3 the unsupported reason already including B009. All collected warnings join the load failure, up to 32 unique warnings.

**Good.** Accurate context, although primarily useful to developers.

1. `Object {id}, {class}: {reason}`
2. `{class} object {id}: {reason}`
3. `{reason} — object {id} ({class})`
4. `Object {id}: {reason} [{class}]`
5. `{class} #{id}: {reason}`

### B011 — Good — Property shorter than requested

**Current:** `Truncated AVB property`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:134). A property read asks for a negative number of bytes or more bytes than remain in the bounded object/document reader. It receives B009.

**Good.** Describes missing property data without claiming the entire file is unusable for every application.

1. `Bin property is incomplete.`
2. `Incomplete data in a bin property.`
3. `Bin property ends too soon.`
4. `Not enough data for this bin property.`
5. `A bin property is cut short.`

### B012 — Good — Property read failed

**Current:** `Cannot read AVB property`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:136). Seeking or reading the requested property bytes failed. It receives B009.

**Good.** Accurately states the failed read without guessing a filesystem cause.

1. `Cannot read bin data.`
2. `Could not read this bin property.`
3. `Bin property read failed.`
4. `Unable to read a property in the bin.`
5. `Failed to read bin property data.`

### B013 — Good — String or byte-array length invalid

**Current:** `Invalid AVB string or byte-array length`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:142). Byte-array size is negative, exceeds remaining object bytes, or exceeds the reader's 65535-byte maximum. It receives B009.

**Good.** Names the failed bounded field-length check. It is technical but useful for a malformed property.

1. `Invalid bin text or data length.`
2. `Bin text or data has an invalid length.`
3. `Cannot read this bin text or data length.`
4. `Invalid length for a bin data field.`
5. `Bin string or byte-array size is invalid.`

### B014 — Good — Property overflows the containing object

**Current:** `AVB property exceeds its object`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:151). A skip length is negative or exceeds the reader's remaining object bytes. It receives B009.

**Good.** Accurately describes the structural boundary problem.

1. `Bin property extends beyond its object.`
2. `Bin property is larger than its object.`
3. `Property data runs past the bin object.`
4. `Bin property does not fit in its object.`
5. `Invalid property size in a bin object.`

### B015 — Misleading — Cannot read a skipped object's endpoint

**Current:** `Truncated AVB object`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:157). A seek to the last skipped byte or a one-byte read there failed. It receives B009.

**Misleading.** Truncation is one possible cause, but the code also reaches this on a general seek/read failure. The actual condition does not prove the file was shortened.

**Recommended:** `Cannot read the end of this bin object.`

### B016 — Good — Unexpected property tag

**Current:** `Invalid AVB property tag; expected 0x%1`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:201). The next tag byte differs from the expected byte. %1 is that expected tag, formatted as at least two hexadecimal digits; B009 follows. The same helper also verifies fixed format markers.

**Good.** Reports the exact failed format expectation; 'unexpected' is plainer than 'invalid'.

1. `Unexpected bin tag; expected 0x{tag}.`
2. `Expected bin tag 0x{tag}.`
3. `Bin tag does not match 0x{tag}.`
4. `Wrong bin tag; expected 0x{tag}.`
5. `Cannot read bin tag; expected 0x{tag}.`

### B017 — Good — Unsupported object version

**Current:** `Unsupported AVB object version %1 (expected %2)`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:213). An object's version differs from the version that its reader handles. %1 is the actual numeric version; %2 the expected version. Wrapped by B009 and B010.

**Good.** Correctly says unsupported rather than corrupt; preserves the observed and supported values.

1. `Bin object version {actual} is unsupported; expected {expected}.`
2. `Cannot read object version {actual}; only {expected} is supported.`
3. `Unsupported object version: {actual}; supported version: {expected}.`
4. `This object uses version {actual}; MediaMuster reads version {expected}.`
5. `Expected object version {expected}, found unsupported version {actual}.`

### B018 — Good — Unsupported extension tag

**Current:** `Unsupported AVB extension 0x%1`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:227). A parsed object's extension tag is not one supported by that object reader. %1 is the extension byte in hexadecimal. Wrapped by B009 and B010.

**Good.** Accurate description of a parser limitation.

1. `Unsupported bin extension: 0x{tag}.`
2. `Cannot read bin extension 0x{tag}.`
3. `Bin extension 0x{tag} is not supported.`
4. `MediaMuster does not support extension 0x{tag}.`
5. `Unknown bin extension: 0x{tag}.`

### B019 — Good — Data after an object's end

**Current:** `Unexpected data after AVB object end`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:233). The expected end tag was read, but bytes remain in this bounded object. Receives B009.

**Good.** Accurately identifies the unexpected trailing data.

1. `Extra data after the bin object.`
2. `Unexpected data at the end of a bin object.`
3. `Bin object has extra trailing data.`
4. `Data remains after the bin object's end.`
5. `Unexpected bytes follow the bin object.`

### B020 — Good — Invalid UTF-8 text

**Current:** `Invalid UTF-8 AVB string`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:260). QStringDecoder reports invalid UTF-8 for a field defined as UTF-8 text. Receives B009.

**Good.** Clear and specific for a technical error; it does not blame unrelated bin text.

1. `Invalid UTF-8 text in the bin.`
2. `Bin text contains invalid UTF-8.`
3. `Cannot decode bin text as UTF-8.`
4. `A bin text field is not valid UTF-8.`
5. `Invalid text encoding in a UTF-8 bin field.`

### B021 — Misleading — Invalid or over-limit entry count

**Current:** `Invalid AVB entry count`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:276). The declared count is negative, exceeds one million entries, or cannot fit in the bytes remaining. Receives B009.

**Misleading.** The one-million-entry ceiling is MediaMuster's own bound; exceeding it does not itself prove the AVB count is invalid.

**Recommended:** `Bin entry count is invalid or exceeds MediaMuster's limit.`

### B022 — Good — Wrong MobId label length

**Current:** `Invalid MOB label length`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:322). A typed MobId label length is not the required 12 bytes. Receives B009.

**Good.** Accurately identifies a fixed-size field mismatch.

1. `Invalid MobId label length.`
2. `MobId label has the wrong length.`
3. `Expected a 12-byte MobId label.`
4. `Bin media ID label has an invalid size.`
5. `MobId label is not 12 bytes long.`

### B023 — Good — Wrong MobId material length

**Current:** `Invalid MOB material length`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:337). The typed MobId's final material section is not the required eight bytes. Receives B009.

**Good.** Accurately identifies a fixed-size field mismatch, though 'material' is identifier jargon.

1. `Invalid MobId material length.`
2. `MobId material field has the wrong length.`
3. `Expected an 8-byte MobId material field.`
4. `Bin media ID material field has an invalid size.`
5. `MobId material field is not 8 bytes long.`

### B024 — Good — Bin changed during reading

**Current:** `AVB file changed while reading; load it again.`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:410). At the end of parsing, file size or modification time differs from the starting value.

**Good.** Explains why the result is rejected and gives the next action. It need not mean every media identifier changed.

1. `The bin changed while loading. Load it again.`
2. `Bin changed during reading; try again.`
3. `Reload the bin: it changed while being read.`
4. `This bin changed during loading. Try again.`
5. `Could not finish loading the changed bin. Reload it.`

### B025 — Good — Retained metadata memory limit

**Current:** `AVB identity and metadata inventory exceeds the 192 MiB memory budget.`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:419). The parser's conservative retained-data estimate would exceed its own 192 MiB budget; not a measurement of all process memory and not necessarily physical-memory exhaustion.

**Good.** Names the configured limit, but 'inventory' and 'memory budget' are unnecessarily heavy.

1. `Bin metadata exceeds MediaMuster's 192 MiB limit.`
2. `Too much bin metadata to load within the 192 MiB limit.`
3. `This bin's metadata exceeds the 192 MiB reading limit.`
4. `MediaMuster's 192 MiB bin metadata limit was reached.`
5. `Cannot load the bin metadata within the 192 MiB limit.`

### B026 — Good — File-size limit or insufficient data

**Current:** `AVB file is empty, truncated, or exceeds the 256 MiB limit.`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:434). The file is fewer than two bytes long or larger than 256 MiB.

**Good.** Lists the possibilities the combined check represents. Separate conditions could give a more precise reason, but the existing disjunction is true.

1. `Bin file is too short or exceeds 256 MiB.`
2. `Cannot read this bin: too little data or more than 256 MiB.`
3. `Bin file size is below 2 bytes or above 256 MiB.`
4. `The bin is incomplete or over the 256 MiB limit.`
5. `Bin file is empty, incomplete, or larger than 256 MiB.`

### B027 — Good — Invalid byte-order marker

**Current:** `Not an Avid bin: invalid byte-order marker.`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:441). The first two bytes are neither of the supported AVB byte-order markers.

**Good.** The supplied bytes do not have the required AVB marker. 'Unrecognised' would avoid sounding certain about the file's history.

1. `Unrecognised Avid bin byte-order marker.`
2. `Invalid byte-order marker in the bin.`
3. `Cannot recognise the bin's byte order.`
4. `The file has no valid AVB byte-order marker.`
5. `Cannot read this bin's byte-order marker.`

### B028 — Good — Invalid document header

**Current:** `Not an Avid bin: invalid document header`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:444). The Domain/OBJD/AObjDoc document signature does not match. Receives B009.

**Good.** Identifies the failed AVB signature validation. Shorter alternatives avoid implying whether this was once a valid bin.

1. `Invalid Avid bin header.`
2. `The bin header was not recognised.`
3. `Cannot recognise the Avid bin header.`
4. `Invalid document header in this file.`
5. `The file has no recognised AVB document header.`

### B029 — Misleading — Object count or root reference invalid or too large

**Current:** `Invalid AVB object count or root reference`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:450). Object count is zero, exceeds one million, cannot fit in remaining bytes, or the root reference is zero/out of range. Receives B009.

**Misleading.** The count can fail solely because of MediaMuster's one-million-object limit. Calling that invalid mislabels an unsupported size as file corruption.

**Recommended:** `Bin object count or root reference is invalid or exceeds MediaMuster's limit.`

### B030 — Good — Inconsistent byte order

**Current:** `AVB header byte order is inconsistent`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:452). The header's second byte-order marker disagrees with the initial marker. Receives B009.

**Good.** Exactly describes the conflicting format information.

1. `Bin byte-order markers disagree.`
2. `Conflicting byte-order markers in the bin header.`
3. `The bin header has inconsistent byte order.`
4. `Bin header byte order does not match.`
5. `The bin's byte-order markers do not match.`

### B031 — Good — Invalid document format IDs

**Current:** `Invalid AVB document format identifiers`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:455). The required ATob and ATve identifiers are not present. Receives B009.

**Good.** Accurately describes failed fixed identifiers, although a user does not need the internal term.

1. `Invalid bin format markers.`
2. `Bin format markers were not recognised.`
3. `Unexpected format markers in the bin.`
4. `The bin has invalid document markers.`
5. `Cannot recognise the bin's format markers.`

### B032 — Good — Invalid object chunk length

**Current:** `Invalid AVB chunk length`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:466). A declared object body size is zero or larger than remaining file bytes. Receives B009.

**Good.** Accurately names the failed size check; 'object size' is slightly more familiar than 'chunk length'.

1. `Invalid bin object size.`
2. `A bin object has an invalid length.`
3. `Bin object size is zero or exceeds the file.`
4. `Invalid length for bin object data.`
5. `A bin object does not fit in the file.`

### B033 — Good — Invalid class code

**Current:** `Invalid AVB class identifier`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:470). One or more bytes of the four-character class code fall outside printable ASCII. Receives B009.

**Good.** Accurately identifies a malformed class code, rather than merely an unknown supported-code value.

1. `Invalid bin object type code.`
2. `A bin object has an invalid type code.`
3. `Malformed bin object identifier.`
4. `Bin class code contains invalid characters.`
5. `Cannot read this bin object type code.`

### B034 — Good — Extra data after declared objects

**Current:** `Unexpected data after declared AVB objects`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:476). Bytes remain after the document's declared number of objects was read. Receives B009.

**Good.** Accurately states the structural inconsistency the reader found.

1. `Extra data after the bin's objects.`
2. `Unexpected trailing data in the bin.`
3. `Data remains after all declared bin objects.`
4. `Unexpected bytes follow the declared bin objects.`
5. `The bin has extra data after its object list.`

### B035 — Good — Document root has wrong type

**Current:** `AVB document root is not a bin`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:478). The root reference resolves to an object whose class is neither ABIN nor BINF. Receives B009.

**Good.** Correctly identifies the missing expected bin root; a technical reason, not a general claim about all data in the file.

1. `The document has no bin root.`
2. `The main document object is not a bin.`
3. `Expected a bin as the document root.`
4. `Document root has the wrong type.`
5. `The file's root object is not a bin.`

### B036 — Good — Invalid object reference

**Current:** `Invalid AVB object reference %1`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:484). %1 is an object reference outside the object table, or zero where a reference is required. Receives B009.

**Good.** Accurately identifies an invalid internal link and preserves the reference number.

1. `Invalid bin object reference: {reference}.`
2. `Bin object reference {reference} is invalid.`
3. `Cannot resolve bin object reference {reference}.`
4. `Invalid link to bin object {reference}.`
5. `Bin reference {reference} does not identify a valid object.`

### B037 — Good — Reference points to wrong object type

**Current:** `AVB reference %1 must identify %2`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:486). %1 is the object reference and %2 the required four-character class code. The nonzero reference exists but points to another class. Receives B009.

**Good.** Correct, though the current wording sounds like a requirement rather than a report of the mismatch.

1. `Bin reference {reference} points to the wrong type; expected {class}.`
2. `Expected {class} at bin reference {reference}.`
3. `Bin reference {reference} does not identify {class}.`
4. `Wrong object type at reference {reference}; expected {class}.`
5. `Reference {reference} should point to {class}.`

### B038 — Good — Unsupported track flags

**Current:** `Unsupported AVB track flags`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:546). A track's flags include bits outside the supported set (mask 0xfc00). Wrapped by B009 and B010.

**Good.** Correctly reports unsupported track information.

1. `Unsupported bin track settings.`
2. `MediaMuster cannot read these track flags.`
3. `Bin track flags are not supported.`
4. `Unsupported track flags in this bin.`
5. `This track uses unsupported flags.`

### B039 — Good — Unsupported bin object version

**Current:** `Unsupported AVB bin version`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:642). The ABIN/BINF object's version is neither 0x0e nor 0x0f. Wrapped by B009 and B010.

**Good.** Correctly identifies a reader limitation without claiming the whole bin is corrupt.

1. `Unsupported bin version.`
2. `This bin version is not supported.`
3. `MediaMuster cannot read this bin version.`
4. `Bin version not yet supported.`
5. `Cannot load this version of the bin.`

### B040 — Misleading — Unexpected rectangle-data version

**Current:** `Invalid AVB bin rectangle version`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:666). The bin rectangle field's version is not 1. Receives B009.

**Misleading.** The code proves only that the version differs from the one the parser expects. 'Invalid' implies the file itself is wrong.

**Recommended:** `Unsupported bin rectangle-data version.`

### B041 — Misleading — Unexpected bin colour-data version

**Current:** `Invalid AVB bin color version`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:671). A bin colour structure's version is not 1. Receives B009.

**Misleading.** A different version is not, by itself, proof of invalid data. Also use UK-English 'colour' in app-owned wording.

**Recommended:** `Unsupported bin colour-data version.`

### B042 — Good — Unsupported attribute value type

**Current:** `Unsupported AVB attribute type %1`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:716). %1 is an attribute type other than the supported types in the switch. Wrapped by B009 and B010.

**Good.** Accurate statement of a parser limitation, with the identifying value retained.

1. `Unsupported bin attribute type: {type}.`
2. `Cannot read bin attribute type {type}.`
3. `Bin attribute type {type} is not supported.`
4. `MediaMuster does not support attribute type {type}.`
5. `This bin uses unsupported attribute type {type}.`

### B043 — Misleading — Unexpected marker colour-data version

**Current:** `Invalid AVB marker color version`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:791). The TMBC marker colour field's version is not 1. Receives B009.

**Misleading.** This proves an unexpected version, not necessarily invalid data. UK-English wording should use 'colour'.

**Recommended:** `Unsupported marker colour-data version.`

### B044 — Good — Unsupported AudioSuite plug-in count

**Current:** `Unsupported AudioSuite plug-in count`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:844). The AudioSuite object declares a plug-in count other than the one plug-in supported by this parser. Wrapped by B009 and B010.

**Good.** Correctly identifies a supported-count restriction, not an invalid file.

1. `Only one AudioSuite plug-in per object is supported.`
2. `This AudioSuite plug-in count is not supported.`
3. `Cannot read this number of AudioSuite plug-ins.`
4. `MediaMuster expects one AudioSuite plug-in here.`
5. `Unsupported number of AudioSuite plug-ins.`

### B045 — Good — Inconsistent AudioSuite preset lengths

**Current:** `Invalid AudioSuite preset length`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:897). The outer preset length and following byte-array length disagree. Receives B009.

**Good.** Accurately identifies conflicting sizes, though 'do not match' explains more clearly.

1. `AudioSuite preset lengths do not match.`
2. `Conflicting AudioSuite preset lengths.`
3. `AudioSuite preset has inconsistent lengths.`
4. `The AudioSuite preset size does not match its data length.`
5. `Invalid size for AudioSuite preset data.`

### B046 — Good — Selector refers to absent track

**Current:** `AVB selector refers to an absent track`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:959). The selector's track index is greater than or equal to the declared track count. Receives B009.

**Good.** Accurately describes the out-of-range track link.

1. `Bin selector points to a missing track.`
2. `Selected track is missing from the bin object.`
3. `Bin selector references a track that is not present.`
4. `Selector track index is outside the track list.`
5. `The bin selector's track does not exist.`

### B047 — Good — Unknown class prevents complete identity reading

**Current:** `Unsupported AVB class %1; whole-bin identity coverage is incomplete.`

[Source](/Users/martymclean/Developer/MediaMuster/src/avbparser.cpp:1049). %1 is a four-character class not parsed by the reader and not in its list of known classes with no direct MobId properties. Adds a warning and sets complete=false; BinFilterDialog rejects the incomplete bin and forwards its warnings.

**Good.** The limitation and incomplete result are accurate. 'Whole-bin identity coverage' is developer jargon for not being able to read all relevant media IDs.

1. `Unsupported bin object {class}; some media IDs may be missing.`
2. `Cannot read object type {class}; media IDs may be incomplete.`
3. `Bin object {class} is unsupported, so not all media IDs can be checked.`
4. `Unsupported object {class} prevents a complete bin read.`
5. `MediaMuster cannot read all media IDs because object {class} is unsupported.`

## Manage Media, Rebalance, Undo and recovery

Read-only source review, 24 September 2026. No production text changed.

**Scope:** `OpManager`, `OpRunner`, `Rebalancer`, and `OperationRecovery`; checked `OperationPlan` and `RebalancePlanner` for forwarded text. All catalogue entries are message templates, not every possible filename, OS error or numeric value. `{placeholders}` denote substituted values. `\n` denotes a real newline. Exact literals are preserved apart from parameter substitution and folding adjacent C++ strings. Repeated identical literals share one entry; distinct variants have separate entries.

Identity-check messages were checked against `OpFile::inspect`, `inspectDirectory` and `stillAt` ([source](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:527)): those helpers also fail on inaccessible paths or failed native metadata reads. Wording that asserts a definite change after a failed check is therefore flagged; this is a precision issue, not proof that the operation itself damages files.

**Verdict:** Good means the text agrees with its trigger; it may still be wordy or technical. Misleading means it asserts something not established by the code, uses a wrong label, or obscures an important scope/limit. Good entries have five simpler candidate phrasings. Misleading entries have one recommended correction and the reason. A few already-short templates do not have five strictly shorter equivalent phrasings; those alternatives are simpler or more explicit, rather than claiming a smaller character count.

**Console routing:** Per-file details appear as `{name}: {state} — {detail}` through `FileOperationController`; state wording is audited separately. Rebalance emits its non-completed details as `{name}: {detail}`. General `OpSink::log` text passes through unchanged. Recovery notes reach `FileOperationController::onRecoveryDone` and then the Console. Exceptions from ordinary operations use the final “Operation stopped” wrapper; exceptions from protective original restoration are logged directly as warnings. Names are clip names where known, otherwise filenames. These names, paths and OS-provided details can themselves contain arbitrary text.

**Composition:** This catalogue lists wrappers and their component messages separately so every actual combination can be read without pretending the unbounded filename/error combinations form a finite list. Cleanup reasons are nested in `Temporary cleanup pending at {path}: {reason}`; restoration reasons are nested in `Original restoration pending: … {reason}`. Lower-level errors from `OpJournal`, `OpFile`, `OpCopier`, `NativeFile` and `OpTrash` are forwarded unchanged, sometimes following a newline. Their full text is covered in the lower-level operation catalogue. Copier warnings can precede the durability suffixes; the deferred-original wrapper can append those combined warnings. Recovery can then append `Journal: {path}` to these messages. Empty `OpResult.message` produces only the controller's state label.

**Inventory: 111 templates/components — 83 Good, 28 Misleading.**

### O001 — Good

**Current:** `Another operation is still running.`

**When:** A second operation is dispatched while the manager is busy. Source: [opmanager.cpp:161](/Users/martymclean/Developer/MediaMuster/src/opmanager.cpp:161).

**Five simpler alternatives:**

1. An operation is already running.
2. Wait for the current operation to finish.
3. Finish the current operation first.
4. Another operation is in progress.
5. The current operation must finish first.

### O002 — Good

**Current:** `Enable undo in the Debug menu first.`

**When:** Undo is disabled; a fresh Undo is requested. Source: [opmanager.cpp:168](/Users/martymclean/Developer/MediaMuster/src/opmanager.cpp:168).

**Five simpler alternatives:**

1. Enable Undo in Debug first.
2. Turn on Undo in the Debug menu.
3. Undo is off. Enable it in Debug.
4. To undo, enable Undo in Debug.
5. Enable Debug → Undo first.

### O003 — Good

**Current:** `{name}: {message}`

**When:** A Rebalance item reports a state other than Completed. The full detail comes from the runner or a lower-level operation. Source: [rebalancer.cpp:28](/Users/martymclean/Developer/MediaMuster/src/rebalancer.cpp:28).

**Five simpler alternatives:**

1. {name} — {message}
2. {message} File: {name}.
3. File {name}: {message}
4. Rebalance: {name} — {message}
5. {name}\n{message}

### O004 — Misleading

**Current:** `Avid databases reset in {count} folder(s); Avid rebuilds them on next launch.`

**When:** At least one media move finished; the adapter counts unique source/destination folders touched by completed moves. Source: [rebalancer.cpp:35](/Users/martymclean/Developer/MediaMuster/src/rebalancer.cpp:35).

**Why misleading:** The count is folders touched by media moves, not databases actually reset. A folder may have had no databases. The code also cannot promise when Avid will rebuild them.

**Recommended:** Rebalance updated {count} media folders. Avid may rebuild their databases when it next scans them.

### O005 — Good

**Current:** `Copy finished; original retained. This storage does not support confirming folder changes against a crash or power loss.`

**When:** Folder synchronisation reports reduced guarantees after the copy succeeds. Appended to the result detail. Source: [oprunner.cpp:44](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:44).

**Five simpler alternatives:**

1. Copied; original kept because this storage cannot confirm folder changes are saved.
2. Copy complete. Original kept: folder changes could be lost after a crash.
3. Copied, with the original kept because folder changes cannot be confirmed.
4. Copy complete; original kept because the drive cannot confirm the folder update.
5. Copied; original kept because a crash could undo the folder change.

### O006 — Good

**Current:** `The file's Avid identity is missing or differs from the scan. Rescan before proceeding.`

**When:** The opened MXF header fails the identity check against the scan. Source: [oprunner.cpp:108](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:108).

**Five simpler alternatives:**

1. The file's Avid ID is missing or changed. Rescan and try again.
2. The Avid ID no longer matches the scan. Rescan and try again.
3. Cannot confirm this file matches the scan. Rescan and try again.
4. The scanned Avid ID could not be confirmed. Rescan and try again.
5. Avid ID check failed. Rescan and try again.

### O007 — Misleading

**Current:** `The completed destination is missing or changed: {destination}`

**When:** Recovery or Undo cannot match the saved completed-file identity at the destination. Source: [oprunner.cpp:143](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:143).

**Why misleading:** The destination may be inaccessible or unidentifiable, rather than missing or changed; OpFile::inspect returns an invalid stamp when opening fails.

**Recommended:** Cannot verify the completed destination: {destination}

### O008 — Good

**Current:** `Original restoration is pending: {temporary path} -> {original path}`

**When:** Recovery cannot confirm a previously attempted return of the original. Source: [oprunner.cpp:168](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:168).

**Five simpler alternatives:**

1. Original still needs restoring: {temporary path} → {original path}
2. Restore pending: {temporary path} → {original path}
3. Original return not confirmed: {temporary path} → {original path}
4. Original not yet confirmed at {original path}; temporary path: {temporary path}.
5. Cannot confirm restoration from {temporary path} to {original path}.

### O009 — Misleading

**Current:** `The original changed while awaiting a Trash choice; no fallback was attempted. {detail}`

**When:** Recovery cannot open or verify the original while the Trash fallback decision is pending. Source: [oprunner.cpp:188](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:188).

**Why misleading:** An open error or missing file also reaches this message, so a change is not established. “Fallback” hides which action was avoided.

**Recommended:** Cannot verify the original while awaiting a Trash choice. No move to MediaMuster Trash was attempted. {detail}

### O010 — Misleading

**Current:** `The system Trash result was interrupted before a recovery receipt was saved. Inspect Trash; MediaMuster will not repeat this deletion.`

**When:** Recovery cannot verify the result of an interrupted system Trash operation. Source: [oprunner.cpp:205](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:205).

**Why misleading:** This path also handles an existing receipt whose saved file cannot be verified or whose original path is occupied. It does not establish that the receipt was never saved.

**Recommended:** Cannot confirm the interrupted system Trash result. Check system Trash; MediaMuster will not repeat this move.

### O011 — Misleading

**Current:** `An original or retirement location changed. Both locations were retained.`

**When:** Recovery finds changed identities or files at both original and temporary removal locations. Source: [oprunner.cpp:218](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:218).

**Why misleading:** It can trigger with only one occupied location; “both locations retained” suggests two retained files. “Retirement” is an internal term.

**Recommended:** The original or its temporary removal location no longer matches the record. No files were removed.

### O012 — Misleading

**Current:** `The interrupted relocation needs inspection; both locations were retained.`

**When:** Recovery cannot establish an interrupted same-volume move as either completed or not begun. Source: [oprunner.cpp:255](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:255).

**Why misleading:** Neither location necessarily contains the file, so “both locations retained” suggests more than is known.

**Recommended:** Cannot confirm the interrupted move. Check the source and destination; no further changes were made.

### O013 — Misleading

**Current:** `The original file is missing or changed; its recovery record was retained: {source}`

**When:** Recovery cannot match the original before restarting a transfer. Source: [oprunner.cpp:271](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:271).

**Why misleading:** An inaccessible original also produces an invalid identity stamp; missing or changed is not established.

**Recommended:** Cannot verify the original at {source}. Its recovery record was kept.

### O014 — Misleading

**Current:** `The original changed or is missing without a recorded removal intent.`

**When:** A completed-copy recovery finds the original missing/different without a saved removal step. Source: [oprunner.cpp:283](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:283).

**Why misleading:** The original may be inaccessible rather than changed or missing. No recorded removal explains the failed check.

**Recommended:** Cannot verify the original, and no removal was recorded.

### O015 — Good

**Current:** `The completed copy could not confirm its writes during recovery. {detail}`

**When:** Recovery cannot synchronise the completed file or its containing folder. Source: [oprunner.cpp:293](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:293).

**Five simpler alternatives:**

1. Cannot confirm the copy is safely saved during recovery. {detail}
2. Recovery could not confirm the completed copy was saved. {detail}
3. Copy saved status could not be confirmed during recovery. {detail}
4. Cannot verify the completed copy is saved on the drive. {detail}
5. Recovery could not confirm the copy reached storage. {detail}

### O016 — Good

**Current:** `Cannot establish the interrupted result; journal and files were retained.`

**When:** No recognised recovery state can establish the operation result. Source: [oprunner.cpp:302](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:302).

**Five simpler alternatives:**

1. Cannot confirm the interrupted operation. Files and recovery record kept.
2. Interrupted result unknown. Files and operation record kept.
3. Recovery could not confirm the result. Existing files and record kept.
4. The interrupted operation needs checking. Files and record kept.
5. Cannot verify what completed. Files and recovery details kept.

### O017 — Good

**Current:** `Temporary cleanup pending at {path}: {reason}`

**When:** Cleanup retains a temporary file or directory. May repeat on separate lines for several paths; reasons follow below. Source: [oprunner.cpp:323](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:323).

**Five simpler alternatives:**

1. Temporary item kept at {path}: {reason}
2. Could not clean up {path}: {reason}
3. Cleanup incomplete at {path}: {reason}
4. Still awaiting cleanup: {path}. {reason}
5. Temporary item remains: {path}. {reason}

### O018 — Good

**Current:** `The interrupted file operation must be reconciled before its folder is removed.`

**When:** Cleanup wrapper reason: an interrupted publish/removal step still needs recovery. Source: [oprunner.cpp:330](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:330).

**Five simpler alternatives:**

1. Recover the interrupted operation before removing its folder.
2. The folder must stay until recovery checks the operation.
3. Finish recovery before cleaning up this folder.
4. The operation needs recovery before this folder can be removed.
5. This folder is needed to recover the interrupted operation.

### O019 — Good

**Current:** `Cannot confirm the folder removal. {detail}`

**When:** Cleanup wrapper reason: a missing directory cannot be safely resolved or its parent synced. Source: [oprunner.cpp:346](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:346).

**Five simpler alternatives:**

1. Folder removal could not be confirmed. {detail}
2. Cannot verify the folder was removed. {detail}
3. The folder removal still needs checking. {detail}
4. Cannot confirm the folder change was saved. {detail}
5. Removal of the folder is unconfirmed. {detail}

### O020 — Misleading

**Current:** `The folder identity changed; it was retained.`

**When:** Cleanup wrapper reason: the path no longer matches the recorded temporary directory identity. Source: [oprunner.cpp:355](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:355).

**Why misleading:** inspectDirectory returns an invalid identity when the folder cannot be opened or inspected. That does not establish a change.

**Recommended:** Cannot verify the recorded temporary folder; it was kept.

### O021 — Good

**Current:** `The partial file cannot be safely identified as disposable. {detail}`

**When:** Cleanup wrapper reason: the file or surviving copy cannot be verified, or its state forbids removal. Source: [oprunner.cpp:372](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:372).

**Five simpler alternatives:**

1. Cannot safely remove the partial file. {detail}
2. The partial file was kept because removal could not be verified as safe. {detail}
3. Cannot confirm the partial file is safe to delete. {detail}
4. Partial file kept; it may still be needed. {detail}
5. Cannot confirm this partial file can be discarded. {detail}

### O022 — Misleading

**Current:** `The surviving original or completed copy changed; the partial was retained.`

**When:** Cleanup wrapper reason: the surviving file cannot be opened or verified after the partial was checked. Source: [oprunner.cpp:394](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:394).

**Why misleading:** Open failures or a missing file also reach this branch; it does not establish a change.

**Recommended:** Cannot verify the surviving original or completed copy; the partial file was kept.

### O023 — Good

**Current:** `Cannot confirm partial cleanup. {detail}`

**When:** Cleanup wrapper reason: the containing directory cannot be fully synced after partial-file removal. Source: [oprunner.cpp:411](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:411).

**Five simpler alternatives:**

1. Cannot confirm partial-file cleanup was saved. {detail}
2. Partial-file cleanup is unconfirmed. {detail}
3. Cannot verify the partial-file removal reached storage. {detail}
4. Cleanup of the partial file still needs confirmation. {detail}
5. Cannot confirm the partial file was fully removed. {detail}

### O024 — Good

**Current:** `Journal failure; source retained.`

**When:** Saving a required operation step fails before changing the source. Also used at lines 758 and 872. Source: [oprunner.cpp:463](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:463).

**Five simpler alternatives:**

1. Could not save the operation record; original kept.
2. Operation record failed; source kept.
3. Original kept because the operation could not be recorded.
4. Cannot record the next step; source kept.
5. Source kept after an operation-record error.

### O025 — Good

**Current:** `Journal failure; temporary folder retained at {temporary folder}`

**When:** The temporary directory exists, but saving its identity/step fails. Source: [oprunner.cpp:470](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:470).

**Five simpler alternatives:**

1. Cannot save the operation record; temporary folder kept at {temporary folder}.
2. Temporary folder kept at {temporary folder} after a record error.
3. Operation record failed. Temporary folder: {temporary folder}.
4. Could not record this step; temporary folder kept: {temporary folder}.
5. Temporary folder retained because the record failed: {temporary folder}.

### O026 — Good

**Current:** `Journal failure; temporary file retained at {temporary file}`

**When:** Saving copy/publish progress fails while the temporary copy still exists. Also lines 519, 549 and 560. Source: [oprunner.cpp:491](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:491).

**Five simpler alternatives:**

1. Cannot save the operation record; temporary file kept at {temporary file}.
2. Temporary file kept at {temporary file} after a record error.
3. Operation record failed. Temporary file: {temporary file}.
4. Could not record this step; temporary file kept: {temporary file}.
5. Temporary file retained because the record failed: {temporary file}.

### O027 — Good

**Current:** `Copy finished; the storage did not confirm the full durability request.`

**When:** Appended to copier detail after copying completes with reduced file-sync guarantees. Leading space is literal. Source: [oprunner.cpp:542](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:542).

**Five simpler alternatives:**

1. Copy finished; storage could not fully confirm it was saved.
2. Copied, but the drive could not fully confirm the write.
3. Copy complete; a full save confirmation was unavailable.
4. Copied; storage did not confirm protection against a crash.
5. Copy complete, but a crash could still affect the saved result.

### O028 — Good

**Current:** `Cancelled before publication.`

**When:** Cancellation after temporary copy finishes, before moving it to its final filename. Source: [oprunner.cpp:551](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:551).

**Five simpler alternatives:**

1. Cancelled before placing the copy at its destination.
2. Cancelled before the copy reached its final location.
3. Cancelled before saving the copy under its final name.
4. Cancelled before completing the destination file.
5. Cancelled before making the copy available at its destination.

### O029 — Misleading

**Current:** `A file changed before publication; source retained.`

**When:** The source or temporary copy fails identity verification before final placement. Source: [oprunner.cpp:553](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:553).

**Why misleading:** Either source or temporary-copy verification can fail because a path is missing or inaccessible, not only because a file changed. The claim that the source remains is also not established when its own check fails.

**Recommended:** Cannot verify the source or temporary copy before completing the copy. No further source removal was attempted.

### O030 — Misleading

**Current:** `The temporary file changed before publication.`

**When:** The temporary copy no longer matches its saved identity before final placement. Source: [oprunner.cpp:564](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:564).

**Why misleading:** The temporary copy may be missing or inaccessible; failure to verify it does not establish a change.

**Recommended:** Cannot verify the temporary copy before placing it at its final destination.

### O031 — Good

**Current:** `The destination became occupied; source retained.`

**When:** A competing file occupies the final destination during copy publication; Keep Both was not selected. Source: [oprunner.cpp:570](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:570).

**Five simpler alternatives:**

1. Destination now exists; original kept.
2. A file appeared at the destination; original kept.
3. Destination conflict; original kept.
4. Another file now occupies the destination; original kept.
5. Cannot use the occupied destination; original kept.

### O032 — Good

**Current:** `All Keep Both names are occupied.`

**When:** Every candidate suffix from (2) through (999) is already occupied. Also line 801. Source: [oprunner.cpp:573](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:573).

**Five simpler alternatives:**

1. No free Keep Both filename was found.
2. Keep Both could not find an unused filename.
3. All available Keep Both filenames are taken.
4. No unused Keep Both name is available.
5. Keep Both ran out of filename choices.

### O033 — Good

**Current:** `The published file's folder update could not be confirmed. Source retained.`

**When:** The copy reached its final path, but a subsequent directory sync failed. Optional native detail follows on a new line. Source: [oprunner.cpp:591](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:591).

**Five simpler alternatives:**

1. Copy placed at the destination; folder update unconfirmed. Original kept.
2. Cannot confirm the destination folder was saved. Original kept.
3. Copy exists, but its folder update is unconfirmed. Original kept.
4. Original kept because the destination folder update failed confirmation.
5. Copy placed, but the drive could not confirm its folder change. Original kept.

### O034 — Good

**Current:** `The copy was published, but the journal failed. Source retained at {source}`

**When:** The completed copy is at the destination, but saving the Published step fails. Source: [oprunner.cpp:606](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:606).

**Five simpler alternatives:**

1. Copy placed, but its record failed. Original kept at {source}.
2. Copied; could not save the operation record. Original: {source}.
3. Copy reached its destination, but recording it failed. Original kept: {source}.
4. Copy is in place; record error. Original remains at {source}.
5. Original kept at {source}: the completed copy could not be recorded.

### O035 — Good

**Current:** `The operation finished on disk, but the journal could not confirm completion. Destination: {destination}`

**When:** Copy completes on disk, but saving the final Done entry fails. Source: [oprunner.cpp:613](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:613).

**Five simpler alternatives:**

1. Copy finished, but completion could not be recorded. Destination: {destination}.
2. Finished on disk; operation record failed. Destination: {destination}.
3. Copy saved at {destination}, but its completion record failed.
4. Completed at {destination}; could not save the completion record.
5. The file is at {destination}, but completion was not recorded.

### O036 — Good

**Current:** `Too many destination conflicts.`

**When:** The final-placement retry loop exhausts 999 iterations. Source: [oprunner.cpp:618](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:618).

**Five simpler alternatives:**

1. Too many filename conflicts at the destination.
2. Destination conflicts prevented completion.
3. Stopped after repeated destination conflicts.
4. Could not complete after repeated filename conflicts.
5. Too many competing files at the destination.

### O037 — Good

**Current:** `Skipped as requested.`

**When:** The item has an explicit Skip conflict policy. Source: [oprunner.cpp:632](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:632).

**Five simpler alternatives:**

1. Skipped by request.
2. Skipped: you chose Skip.
3. Skipped according to your choice.
4. Your Skip choice was applied.
5. Skipped using the selected option.

### O038 — Good

**Current:** `Restored from system Trash.`

**When:** The native Trash provider restores the file and completion is recorded. Source: [oprunner.cpp:655](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:655).

**Five simpler alternatives:**

1. Returned from system Trash.
2. Restored from Trash.
3. File restored from system Trash.
4. Moved back from system Trash.
5. System Trash restore complete.

### O039 — Misleading

**Current:** `The file changed since it was selected or journalled; rescan before proceeding.`

**When:** Source size, time, identity or saved file identity differs from the scan/record. Source: [oprunner.cpp:673](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:673).

**Why misleading:** The branch covers invalid recorded identities and failed path verification as well as genuine changes. Rescanning is sensible, but a change is not established in every case.

**Recommended:** The file could not be matched to the scan or operation record. Rescan and try again.

### O040 — Good

**Current:** `System Trash result needs recovery.`

**When:** The native Trash move succeeded, but saving completion failed. Source: [oprunner.cpp:703](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:703).

**Five simpler alternatives:**

1. Trash move completed, but its record needs recovery.
2. Could not record the completed system Trash move.
3. The system Trash move needs recovery checking.
4. System Trash move completed; recovery record incomplete.
5. Check recovery for the completed Trash move.

### O041 — Good

**Current:** `Moved to system Trash.`

**When:** The native Trash move and completion record both succeed. Source: [oprunner.cpp:704](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:704).

**Five simpler alternatives:**

1. Sent to system Trash.
2. Moved to Trash.
3. File sent to system Trash.
4. System Trash move complete.
5. File moved to system Trash.

### O042 — Misleading

**Current:** `Original changed while checking bin support. {detail}`

**When:** The native Trash call refuses; reopening or rechecking the original fails. Source: [oprunner.cpp:723](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:723).

**Why misleading:** “Bin” is ambiguous in Avid software, and inability to open the file does not establish that it changed.

**Recommended:** Cannot verify the original after checking system Trash support. {detail}

### O043 — Good

**Current:** `Cannot save the bin refusal; original retained.`

**When:** System Trash refused without moving anything, but recording that refusal fails. Source: [oprunner.cpp:736](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:736).

**Five simpler alternatives:**

1. Cannot record the system Trash refusal; original kept.
2. System Trash refused; could not record it. Original kept.
3. Original kept: the Trash refusal could not be saved.
4. Could not record why system Trash refused. Original kept.
5. Trash refusal record failed; original kept.

### O044 — Good

**Current:** `Cancelled before moving to MediaMuster Trash.`

**When:** Cancellation arrives after fallback approval but before any fallback move. Source: [oprunner.cpp:741](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:741).

**Five simpler alternatives:**

1. Cancelled before the MediaMuster Trash move.
2. MediaMuster Trash move cancelled before it began.
3. Cancelled; the MediaMuster Trash move did not start.
4. Cancelled before sending the file to MediaMuster Trash.
5. The move to MediaMuster Trash was cancelled before starting.

### O045 — Good

**Current:** `Already at the destination; no file changes were needed.`

**When:** The source and destination identify the same object. Source: [oprunner.cpp:771](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:771).

**Five simpler alternatives:**

1. Already at destination; unchanged.
2. Already in place; nothing changed.
3. File is already at the destination.
4. Destination is the same file; no changes made.
5. No move or copy needed; already at destination.

### O046 — Good

**Current:** `Rebalance stopped: a destination became occupied after the group check. Rescan and replan.`

**When:** A destination becomes occupied after the group-level precheck but before moving this item. Source: [oprunner.cpp:786](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:786).

**Five simpler alternatives:**

1. Rebalance stopped: a destination is now occupied. Rescan and try again.
2. Rebalance stopped because a destination changed. Rescan and try again.
3. A file now occupies the Rebalance destination. Rescan and try again.
4. Rebalance destination conflict. Rescan and try again.
5. Destination became occupied; Rebalance stopped. Rescan and try again.

### O047 — Good

**Current:** `Destination occupied; source retained.`

**When:** A destination exists, and the item is not allowed to keep both. Source: [oprunner.cpp:796](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:796).

**Five simpler alternatives:**

1. Destination already exists; original kept.
2. A file occupies the destination; original kept.
3. Destination conflict; original kept.
4. Cannot use the occupied destination; original kept.
5. Original kept because the destination is occupied.

### O048 — Good

**Current:** `Cannot prepare relocation; the source was retained.\n{detail}`

**When:** The source/destination folders cannot be synced before a same-volume move. Source: [oprunner.cpp:818](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:818).

**Five simpler alternatives:**

1. Cannot prepare the move; original kept.\n{detail}
2. Move preparation failed; original kept.\n{detail}
3. Original kept because the move could not be prepared.\n{detail}
4. Could not prepare the destination for the move; source kept.\n{detail}
5. Cannot safely start the move; original kept.\n{detail}

### O049 — Good

**Current:** `Journal failure; relocation stopped.`

**When:** Saving the required step before a move fails. Source: [oprunner.cpp:833](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:833).

**Five simpler alternatives:**

1. Operation record failed; move stopped.
2. Could not record the move; stopped.
3. Move stopped because its record could not be saved.
4. Cannot save the operation record; move stopped.
5. Recording failed before the move; stopped.

### O050 — Misleading

**Current:** `The source changed before relocation; rescan before proceeding.`

**When:** The source no longer matches its recorded file identity just before a move. Source: [oprunner.cpp:836](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:836).

**Why misleading:** The path may be missing or inaccessible. The identity check failing does not establish a change to the file.

**Recommended:** Cannot verify the source before moving it. Rescan and try again.

### O051 — Good

**Current:** `Cancelled before moving to Trash.`

**When:** Cancellation at the last check before a same-volume move to MediaMuster Trash. Source: [oprunner.cpp:846](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:846).

**Five simpler alternatives:**

1. Cancelled before the Trash move.
2. Trash move cancelled before it began.
3. Cancelled; the move to Trash did not start.
4. Cancelled before sending the file to Trash.
5. The Trash move was cancelled before starting.

### O052 — Good

**Current:** `Rebalance stopped: a destination became occupied. Completed moves and remaining files are recorded.`

**When:** The OS reports a destination collision while attempting a Rebalance move. The job records the failure. Source: [oprunner.cpp:855](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:855).

**Five simpler alternatives:**

1. Rebalance stopped at a destination conflict. Progress is recorded.
2. Destination now occupied; Rebalance stopped and progress recorded.
3. Rebalance conflict: completed and pending moves are recorded.
4. Rebalance stopped because a destination is occupied. The record shows what moved.
5. Destination conflict stopped Rebalance. Completed moves and remaining work are recorded.

### O053 — Good

**Current:** `Destination became occupied; source retained.`

**When:** The OS reports a move collision and no further Keep Both candidate can be used. Source: [oprunner.cpp:869](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:869).

**Five simpler alternatives:**

1. Destination now exists; original kept.
2. Another file appeared at the destination; original kept.
3. Destination conflict during the move; original kept.
4. Original kept because the destination became occupied.
5. The destination is now occupied; nothing moved.

### O054 — Misleading

**Current:** `The relocated file changed during the operation. Inspect {destination}; its journal is retained.`

**When:** After moving, the file no longer matches its original saved identity. Source: [oprunner.cpp:880](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:880).

**Why misleading:** A failed native metadata read also returns an invalid stamp, so a change is not established in every case.

**Recommended:** Cannot verify the file after moving it. Check {destination}; its operation record was kept.

### O055 — Good

**Current:** `File relocated to {destination}, but folder durability needs recovery confirmation.`

**When:** Move succeeds, but syncing source/destination directory updates fails or gives reduced guarantees. Optional native detail follows on a new line. Source: [oprunner.cpp:892](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:892).

**Five simpler alternatives:**

1. File moved to {destination}; recovery must confirm the folder changes were saved.
2. Moved to {destination}, but the folder updates need recovery checking.
3. Move reached {destination}; saved folder changes are unconfirmed.
4. Moved to {destination}; the drive could not confirm the folder changes.
5. File is at {destination}, but recovery must check the folder updates.

### O056 — Good

**Current:** `Relocated to {destination}; journal completion failed.`

**When:** Move and folder sync succeed; writing completion fails. Source: [oprunner.cpp:902](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:902).

**Five simpler alternatives:**

1. Moved to {destination}; could not record completion.
2. File moved to {destination}; completion record failed.
3. Move completed at {destination}, but its record failed.
4. At {destination}; the completed move could not be recorded.
5. Moved successfully to {destination}; recording completion failed.

### O057 — Good

**Current:** `Moved to MediaMuster Trash.`

**When:** The local fallback/maintenance Trash move completes and is recorded. Source: [oprunner.cpp:904](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:904).

**Five simpler alternatives:**

1. Sent to MediaMuster Trash.
2. File moved to MediaMuster Trash.
3. MediaMuster Trash move complete.
4. File sent to MediaMuster Trash.
5. Moved into MediaMuster Trash.

### O058 — Good

**Current:** `Safe same-filesystem relocation is unavailable. The source was retained.`

**When:** The requested move requires same-volume relocation but that mechanism or its sync guarantees are unavailable. Optional lower-level detail follows on a new line. Source: [oprunner.cpp:927](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:927).

**Five simpler alternatives:**

1. Cannot safely move this file on the same volume. Original kept.
2. Same-volume move unavailable; original kept.
3. Original kept because a safe same-volume move is unavailable.
4. Cannot confirm a safe move on this volume. Original kept.
5. The volume cannot support this move safely; original kept.

### O059 — Good

**Current:** `Original restoration pending: {temporary path} -> {original path}. {reason}`

**When:** Returning the temporarily moved original is blocked; reason follows below. A journal failure can be appended on a new line. Source: [oprunner.cpp:948](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:948).

**Five simpler alternatives:**

1. Original still needs restoring: {temporary path} → {original path}. {reason}
2. Restore pending from {temporary path} to {original path}. {reason}
3. Original return incomplete: {temporary path} → {original path}. {reason}
4. Cannot finish returning {temporary path} to {original path}. {reason}
5. Original restore blocked: {temporary path} → {original path}. {reason}

### O060 — Good

**Current:** `A path is no longer safe; files were retained.`

**When:** Original-restoration wrapper reason: source/temporary path safety check fails. Source: [oprunner.cpp:956](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:956).

**Five simpler alternatives:**

1. A path failed safety checks; files kept.
2. Cannot safely use a path; files kept.
3. Unsafe path detected; files kept.
4. Files kept because a path cannot be used safely.
5. A path could not be verified; files kept.

### O061 — Good

**Current:** `The original location is occupied; nothing was overwritten.`

**When:** Original-restoration wrapper reason: both paths exist or the original path contains a different object. Source: [oprunner.cpp:963](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:963).

**Five simpler alternatives:**

1. The original location is already occupied; nothing overwritten.
2. A file blocks the original location; nothing replaced.
3. Cannot restore over the existing file; nothing overwritten.
4. Original location unavailable; existing files kept.
5. Something occupies the original location; no files replaced.

### O062 — Misleading

**Current:** `The retained original is missing or changed. {detail}`

**When:** Original-restoration wrapper reason: cannot reopen/verify the temporarily retained original. Source: [oprunner.cpp:969](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:969).

**Why misleading:** The retained original may be inaccessible rather than missing or changed; an open failure enters this branch too.

**Recommended:** Cannot verify the retained original. {detail}

### O063 — Good

**Current:** `The original could not be returned safely. {detail}`

**When:** Original-restoration wrapper reason: identity recheck or native return move fails. Source: [oprunner.cpp:973](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:973).

**Five simpler alternatives:**

1. Cannot safely return the original. {detail}
2. Original return failed its safety checks. {detail}
3. Could not safely move the original back. {detail}
4. Cannot complete the return of the original safely. {detail}
5. Original restore could not be completed safely. {detail}

### O064 — Good

**Current:** `The folder updates still need confirmation. {detail}`

**When:** Original-restoration wrapper reason: original is back, but folder sync has not succeeded fully. Source: [oprunner.cpp:980](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:980).

**Five simpler alternatives:**

1. Folder changes are still unconfirmed. {detail}
2. Cannot yet confirm the folder changes were saved. {detail}
3. The folder changes still need checking. {detail}
4. Storage has not confirmed the folder updates. {detail}
5. Folder-update confirmation is pending. {detail}

### O065 — Good

**Current:** `Original restored to {source}. Completed copies were kept.`

**When:** Protective rollback returns the original and preserves existing copies. Source: [oprunner.cpp:984](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:984).

**Five simpler alternatives:**

1. Original back at {source}; completed copies kept.
2. Restored to {source}; existing completed copies kept.
3. Original returned to {source}. Copies kept.
4. Original restored at {source}, with completed copies kept.
5. Back at {source}; completed copies remain.

### O066 — Good

**Current:** `Cannot read the original restoration record.`

**When:** Restoration request points to a missing or corrupt operation record. Logged directly as a warning by restoreOriginals. Source: [oprunner.cpp:1002](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1002).

**Five simpler alternatives:**

1. Cannot read the record needed to restore originals.
2. Original-restoration record is unreadable.
3. Cannot open a valid restoration record.
4. The restoration record is missing or unreadable.
5. Cannot load the original-restoration record.

### O067 — Good

**Current:** `An Undo already owns this job's recovery. Resume that Undo first.`

**When:** The job already records an Undo, or another Undo record refers to it. Also line 1007. Source: [oprunner.cpp:1004](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1004).

**Five simpler alternatives:**

1. An Undo is already handling this operation. Resume it first.
2. Resume the existing Undo before restoring originals.
3. This operation has an Undo in progress. Resume that first.
4. Recovery is part of an existing Undo. Resume it first.
5. Resume the Undo already started for this operation.

### O068 — Good

**Current:** `The completed copy is not ready for original removal.`

**When:** The all-copies barrier, durability, metadata or destination verification check has not succeeded. Source: [oprunner.cpp:1059](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1059).

**Five simpler alternatives:**

1. The original cannot be removed yet; the copy is not ready.
2. Copy checks are incomplete; original removal is blocked.
3. Original kept until the completed copy passes its checks.
4. The copy is not yet safe to use without the original.
5. Original removal must wait for the copy checks.

### O069 — Good

**Current:** `Cancelled; original retained.`

**When:** Cancellation arrives before removing an original that is still in place. Source: [oprunner.cpp:1063](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1063).

**Five simpler alternatives:**

1. Cancelled; original kept.
2. Original kept after cancellation.
3. Cancelled without removing the original.
4. Cancellation left the original in place.
5. Original remains; operation cancelled.

### O070 — Good

**Current:** `Original removal durability needs recovery. {detail}`

**When:** The original appears absent, but syncing its removal fails. Also line 1132 after native removal. Source: [oprunner.cpp:1083](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1083).

**Five simpler alternatives:**

1. Recovery must confirm the original was fully removed. {detail}
2. Original removal needs recovery checking. {detail}
3. Cannot confirm the original removal was saved. {detail}
4. Recovery is needed to confirm the removal. {detail}
5. Original removal is not fully confirmed. {detail}

### O071 — Good

**Current:** `Both original and retirement paths are occupied.`

**When:** Both the original and the temporary removal location contain an object. Source: [oprunner.cpp:1090](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1090).

**Five simpler alternatives:**

1. Files exist at both the original and temporary removal locations.
2. Both the original and its temporary location are occupied.
3. Original and temporary removal locations both contain files.
4. Two occupied locations block removal: original and temporary.
5. Cannot continue: both original and temporary paths are occupied.

### O072 — Misleading

**Current:** `Original changed before removal: {path}`

**When:** Opening the original/temporarily moved original fails, or it no longer matches its record. Source: [oprunner.cpp:1094](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1094).

**Why misleading:** The file may be missing or inaccessible; “changed” is not established.

**Recommended:** Cannot verify the original before removal: {path}

### O073 — Good

**Current:** `Cannot prepare original removal. {detail}`

**When:** The private temporary removal directory cannot be created and fully synced. Source: [oprunner.cpp:1108](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1108).

**Five simpler alternatives:**

1. Cannot prepare to remove the original. {detail}
2. Original removal could not be prepared. {detail}
3. Could not prepare a safe original removal. {detail}
4. Removal preparation failed for the original. {detail}
5. Cannot safely start removing the original. {detail}

### O074 — Good

**Current:** `Original removal needs recovery. {detail}`

**When:** Rechecking or moving the original into its private removal folder fails. Source: [oprunner.cpp:1117](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1117).

**Five simpler alternatives:**

1. Original removal needs recovery checking. {detail}
2. Recovery is needed before original removal can continue. {detail}
3. Cannot finish removing the original without recovery. {detail}
4. Recover this operation before continuing original removal. {detail}
5. Original removal stopped for recovery. {detail}

### O075 — Good

**Current:** `Could not confirm original removal. {detail}`

**When:** Native removal of the original does not report success. Source: [oprunner.cpp:1128](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1128).

**Five simpler alternatives:**

1. Original removal could not be confirmed. {detail}
2. Cannot verify the original was removed. {detail}
3. The original removal is unconfirmed. {detail}
4. Removal of the original needs checking. {detail}
5. Could not verify removal of the original. {detail}

### O076 — Good

**Current:** `Enable undo in the Debug menu before starting an Undo.`

**When:** Defensive-only runner error for an Undo without its enable flag. Current OpManager rejects disabled Undo first and otherwise sets the flag, so the normal Console route cannot reach this guard. Source: [oprunner.cpp:1142](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1142).

**Five simpler alternatives:**

1. Enable Undo in Debug first.
2. Turn on Undo in the Debug menu.
3. Undo is off. Enable it in Debug.
4. To undo, enable Undo in Debug.
5. Enable Debug → Undo before continuing.

### O077 — Good

**Current:** `This job cannot start another Undo.`

**When:** Defensive-only guard for a corrupt/already-undone/inverse record. The current caller gets its record from latestUndoable, which already excludes those cases; normal UI execution cannot reach this guard. Source: [oprunner.cpp:1144](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1144).

**Five simpler alternatives:**

1. This operation cannot be undone again.
2. Another Undo cannot be started for this operation.
3. Undo is unavailable for this operation.
4. Cannot start Undo from this operation record.
5. This operation is not eligible for another Undo.

### O078 — Good

**Current:** `An Undo object has no saved identity.`

**When:** Defensive-only guard against a missing saved identity. Every current add() caller has already proved the supplied identity valid, so this literal has a Console error route but no normal reachable trigger. Source: [oprunner.cpp:1157](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1157).

**Five simpler alternatives:**

1. An Undo file has no recorded identity.
2. A file needed for Undo has no saved identity.
3. Undo cannot identify a file from its record.
4. The file identity needed for Undo was not saved.
5. Undo lacks the recorded identity of a file.

### O079 — Good

**Current:** `An interrupted system Trash action needs inspection before Undo.`

**When:** An unconfirmed system Trash step lacks a usable restore receipt/location. Source: [oprunner.cpp:1184](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1184).

**Five simpler alternatives:**

1. Check the interrupted system Trash move before Undo.
2. Undo needs the interrupted Trash move checked first.
3. Inspect system Trash before undoing this interrupted move.
4. The interrupted Trash move must be checked before Undo.
5. Check system Trash to resolve the interrupted move before Undo.

### O080 — Misleading

**Current:** `A relocated file changed or its original location is occupied.`

**When:** Undo cannot confirm the moved file, and the alternate already-restored condition also fails. Source: [oprunner.cpp:1194](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1194).

**Why misleading:** The condition can also mean the moved file is missing, its location is inaccessible, or an unexpected object occupies the destination; the two stated causes are incomplete.

**Recommended:** Cannot confirm the moved file or its original location. Undo stopped.

### O081 — Misleading

**Current:** `An original changed or disappeared without a removal record; Undo stopped.`

**When:** Undo planning cannot account for the original using the saved copy/removal states. Source: [oprunner.cpp:1225](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1225).

**Why misleading:** The original may be inaccessible or otherwise unverifiable; the branch does not prove it changed or disappeared.

**Recommended:** Cannot verify the original or find a recorded removal; Undo stopped.

### O082 — Good

**Current:** `This job has no completed work that can be undone.`

**When:** Undo planning produces no inverse actions. Source: [oprunner.cpp:1229](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1229).

**Five simpler alternatives:**

1. This operation has nothing to undo.
2. No completed changes can be undone.
3. Nothing in this operation can be undone.
4. No undoable changes were found.
5. This operation has no changes available for Undo.

### O083 — Good

**Current:** `Avid database retirement stopped: {detail}`

**When:** Moving an Avid database to MediaMuster Trash does not complete while preparing Rebalance/Undo. Source: [oprunner.cpp:1274](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1274).

**Five simpler alternatives:**

1. Avid database reset stopped: {detail}
2. Could not move the Avid database to MediaMuster Trash: {detail}
3. Avid database reset failed: {detail}
4. Stopped while clearing an Avid database for rebuilding: {detail}
5. Cannot prepare the Avid database for rebuilding: {detail}

### O084 — Good

**Current:** `Retrying {name}`

**When:** A retryable native copy error is being retried, up to twice. Source: [oprunner.cpp:1296](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1296).

**Five simpler alternatives:**

1. Retrying copy: {name}
2. Copy retry: {name}
3. Trying the copy again: {name}
4. Copying again: {name}
5. Retrying the file copy for {name}.

### O085 — Good

**Current:** `Cancelled before retrying the copy.`

**When:** Cancellation occurs during the short retry backoff. Source: [oprunner.cpp:1302](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1302).

**Five simpler alternatives:**

1. Cancelled before the copy retry.
2. Copy retry cancelled before starting.
3. Cancelled; the copy was not retried.
4. No copy retry: operation cancelled.
5. Cancelled before trying the copy again.

### O086 — Good

**Current:** `Cannot read the requested operation journal.`

**When:** Resume cannot load the requested operation record. Source: [oprunner.cpp:1372](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1372).

**Five simpler alternatives:**

1. Cannot read the requested operation record.
2. The selected operation record cannot be read.
3. Cannot load the record needed to resume.
4. Resume record is unavailable or unreadable.
5. Cannot read the operation record for Resume.

### O087 — Good

**Current:** `This job was abandoned or has already started Undo.`

**When:** Resume is rejected because the record is dismissed or records an Undo. Source: [oprunner.cpp:1375](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1375).

**Five simpler alternatives:**

1. This operation was stopped permanently or already has an Undo.
2. Cannot resume an abandoned operation or one already being undone.
3. Resume unavailable: operation abandoned or Undo started.
4. The operation was abandoned, or its Undo has begun.
5. Cannot resume: this operation was abandoned or Undo already started.

### O088 — Good

**Current:** `This job has already started Undo.`

**When:** An inverse operation record refers to the requested forward job. Source: [oprunner.cpp:1378](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1378).

**Five simpler alternatives:**

1. Undo has already started for this operation.
2. This operation is already being undone.
3. An Undo already exists for this operation.
4. Cannot resume: Undo already started.
5. This operation already has an Undo in progress.

### O089 — Good

**Current:** `Cannot confirm which job owns this Undo.`

**When:** The original record cannot be loaded or points at another inverse record. Source: [oprunner.cpp:1386](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1386).

**Five simpler alternatives:**

1. Cannot match this Undo to its original operation.
2. Cannot confirm the original operation for this Undo.
3. This Undo cannot be matched to its operation record.
4. Cannot verify which operation this Undo belongs to.
5. The original operation for this Undo is unconfirmed.

### O090 — Good

**Current:** `Cannot save ownership of the interrupted Undo.`

**When:** Resuming an interrupted Undo cannot claim the forward record. Source: [oprunner.cpp:1389](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1389).

**Five simpler alternatives:**

1. Cannot record the interrupted Undo against its original operation.
2. Cannot save the link between this Undo and its operation.
3. Cannot record which operation this interrupted Undo belongs to.
4. Saving the interrupted Undo link failed.
5. Cannot update the original operation with this Undo.

### O091 — Good

**Current:** `The previous job was interrupted. Resume or stop it first.`

**When:** An unresolved earlier job blocks starting another one. Source: [oprunner.cpp:1422](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1422).

**Five simpler alternatives:**

1. Resume or stop the interrupted operation first.
2. Resolve the previous interrupted operation before continuing.
3. The previous operation is unfinished. Resume or stop it first.
4. An interrupted operation is waiting. Resume or stop it first.
5. Finish handling the interrupted operation before starting another.

### O092 — Good

**Current:** `Only the most recent eligible job can be undone.`

**When:** The requested Undo is not the latest eligible operation. Source: [oprunner.cpp:1428](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1428).

**Five simpler alternatives:**

1. Only the latest eligible operation can be undone.
2. Undo is available only for the latest eligible operation.
3. Choose the latest operation available for Undo.
4. You can undo only the most recent eligible operation.
5. Earlier operations cannot be undone before the latest eligible one.

### O093 — Good

**Current:** `Choose an absolute destination folder before starting.`

**When:** Copy/Move has no absolute destination path. Normally blocked by UI validation. Source: [oprunner.cpp:1433](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1433).

**Five simpler alternatives:**

1. Choose a destination with a full path first.
2. A full destination path is required.
3. Select a destination folder with a full path.
4. Choose a complete destination path before starting.
5. Destination must be a full folder path.

### O094 — Good

**Current:** `The source and relocation paths must be absolute.`

**When:** A source path or Rebalance destination path is not absolute. Normally an internal validation failure. Source: [oprunner.cpp:1441](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1441).

**Five simpler alternatives:**

1. Source and destination must use full paths.
2. Full source and move-destination paths are required.
3. Use complete paths for the source and destination.
4. The source and destination need full file paths.
5. Cannot move files without full source and destination paths.

### O095 — Good

**Current:** `Unsupported name, folder or conflict policy. Replace is not supported.`

**When:** A filename/folder is not a single safe component, or the conflict policy is not Skip/Keep Both. Source: [oprunner.cpp:1449](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1449).

**Five simpler alternatives:**

1. Invalid name, folder or conflict choice. Replacing files is unsupported.
2. Cannot use this name, folder or conflict option. Replace is unavailable.
3. Unsupported file details or conflict choice; existing files cannot be replaced.
4. Check the name, folder and conflict choice. Replace is not available.
5. Invalid name, folder or handling choice. Use supported options; Replace is unavailable.

### O096 — Good

**Current:** `Undo was saved, but original ownership needs recovery.`

**When:** The inverse record was created but linking it from the forward record failed. Source: [oprunner.cpp:1481](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1481).

**Five simpler alternatives:**

1. Undo was saved, but its link to the original operation needs recovery.
2. Undo recorded; recovery must link it to the original operation.
3. Saved Undo needs recovery to confirm its original operation.
4. Undo saved, but its original-operation link could not be recorded.
5. Undo exists; recovery must confirm which operation it belongs to.

### O097 — Misleading

**Current:** `Rebalance group skipped: a destination folder no longer has room below 5,000 files. Rescan and replan.`

**When:** A Rebalance group would make a destination contain more than 5,000 essence files. Source: [oprunner.cpp:1576](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1576).

**Why misleading:** The code allows exactly 5,000, whereas “below 5,000” implies a maximum of 4,999.

**Recommended:** Rebalance group skipped: the destination would exceed 5,000 media files. Rescan and try again.

### O098 — Good

**Current:** `Rebalance group skipped: a destination is occupied. Rescan and replan.`

**When:** A group has an occupied or duplicate destination before its files move. Source: [oprunner.cpp:1578](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1578).

**Five simpler alternatives:**

1. Rebalance group skipped: destination conflict. Rescan and try again.
2. Destination occupied; group skipped. Rescan and try again.
3. Group skipped because a destination is unavailable. Rescan and try again.
4. Rebalance skipped this group due to a destination conflict. Rescan and try again.
5. Cannot move this group into an occupied destination. Rescan and try again.

### O099 — Misleading

**Current:** `Rebalance unavailable; source files retained.\n{detail}`

**When:** Group destination creation or directory sync fails before moving this group. Also line 1605. Source: [oprunner.cpp:1601](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1601).

**Why misleading:** This is a job-level stop message, but it only guarantees this group's source files were not moved yet. Earlier groups may already have moved.

**Recommended:** Cannot start this Rebalance group; its source files were kept. Earlier completed moves remain recorded.\n{detail}

### O100 — Good

**Current:** `Avid database relocation stopped: {detail}`

**When:** A pending database-maintenance entry cannot complete when encountered in the main run loop. Source: [oprunner.cpp:1657](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1657).

**Five simpler alternatives:**

1. Avid database move stopped: {detail}
2. Could not move the Avid database: {detail}
3. Avid database reset stopped: {detail}
4. Stopped while moving an Avid database to MediaMuster Trash: {detail}
5. Cannot finish the Avid database move: {detail}

### O101 — Good

**Current:** `Original retained because the job's required copies have not all completed safely.`

**When:** A deferred original is retained because the all-copies completion/safety barrier is unmet. Existing copy detail may follow on a new line. Source: [oprunner.cpp:1720](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1720).

**Five simpler alternatives:**

1. Original kept because not all required copies passed their checks.
2. Original kept until every required copy is complete and checked.
3. Some required copies are incomplete or unverified; original kept.
4. Original kept: the operation has unfinished or unconfirmed copies.
5. Not all required copies are ready; original kept.

### O102 — Good

**Current:** `Cannot confirm original locations before finishing Undo.`

**When:** The forward record cannot be loaded/resolved before redundant-copy disposal. Source: [oprunner.cpp:1742](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1742).

**Five simpler alternatives:**

1. Cannot verify the original locations needed to finish Undo.
2. Undo cannot finish until original locations are confirmed.
3. Cannot check where the originals belong before finishing Undo.
4. Original locations are unconfirmed; Undo cannot finish.
5. Cannot finish Undo without verifying the original locations.

### O103 — Misleading

**Current:** `A restored original changed; its remaining copy was retained.`

**When:** Before removing a redundant copy, no qualifying restored or unchanged original can be confirmed. Source: [oprunner.cpp:1765](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1765).

**Why misleading:** The original may be missing or unverifiable, rather than changed.

**Recommended:** Cannot verify the original; its remaining copy was kept.

### O104 — Misleading

**Current:** `An original changed while preparing the Trash choice; no fallback was attempted.`

**When:** Reopening/validating the original or confirming it is safe to discard fails before asking about MediaMuster Trash. Source: [oprunner.cpp:1830](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1830).

**Why misleading:** Can mean a missing/inaccessible file or failed original verification, not a proven change.

**Recommended:** Cannot verify the file before offering MediaMuster Trash. No move was attempted.

### O105 — Good

**Current:** `Original retained; the MediaMuster Trash move was not approved or the operation stopped.`

**When:** Fallback was declined, cancelled, ineligible, or blocked by an unhealthy journal. Source: [oprunner.cpp:1840](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1840).

**Five simpler alternatives:**

1. Original kept: MediaMuster Trash was not approved or the operation stopped.
2. No MediaMuster Trash move: approval missing or operation stopped. Original kept.
3. Original kept because the fallback move was declined or could not continue.
4. MediaMuster Trash move did not proceed; original kept.
5. Original kept; the MediaMuster Trash move was declined or interrupted.

### O106 — Misleading

**Current:** `A restored original changed while awaiting the Trash choice; its remaining copy was retained.`

**When:** After fallback approval, the original cannot be reconfirmed before disposing of its redundant copy. Source: [oprunner.cpp:1846](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1846).

**Why misleading:** Failure to verify does not prove a change; the original may be missing or inaccessible.

**Recommended:** Cannot verify the original after the Trash choice; its remaining copy was kept.

### O107 — Misleading

**Current:** `Operation stopped: {error}. Journal and files retained at {journal path}.`

**When:** Any exception in normal run/Resume/Undo. The path can be empty if the operation record was never created. Source: [oprunner.cpp:1898](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1898).

**Why misleading:** Media files are not stored at the journal filename. Early failures may have no journal at all. Nested error text often already ends in a full stop, producing doubled punctuation.

**Recommended:** Operation stopped: {error}\nOperation record: {journal path} — include the second line only when a record exists; report any retained file paths separately when known.

### O108 — Misleading

**Current:** `{operation}: {completed} completed, {unchanged} unchanged, {retained} source retained, {skipped} skipped, {failed} failed, {attention} need attention{cancelled suffix}. Journal: {journal path}`

**When:** End of a normal runner call; {cancelled suffix} is empty or “; cancelled”. {operation} comes from opKindName. Source: [oprunner.cpp:1903](/Users/martymclean/Developer/MediaMuster/src/oprunner.cpp:1903).

**Why misleading:** The operation is “rename” for Rebalance, counts omit items completed before Resume, and a pre-journal failure still prints an empty journal path. “source retained” also includes originals restored during the run, which need not be copies. The summary does not label its counts as this run.

**Recommended:** {user-facing operation}: this run — {completed} completed, {unchanged} unchanged, {retained} originals kept or restored, {skipped} skipped, {failed} failed, {attention} need checking{cancelled suffix}. Add “Operation record: {path}” only when present.

### O109 — Misleading

**Current:** `Isolated temporary file retained: {path}`

**When:** Recovery finds an occupied artifact path recorded by a prior operation. Source: [operationrecovery.cpp:128](/Users/martymclean/Developer/MediaMuster/src/operationrecovery.cpp:128).

**Why misleading:** Artifacts include directories as well as files, so “file” is wrong for some entries. “Isolated” adds no useful fact.

**Recommended:** Temporary item kept: {path}

### O110 — Good

**Current:** `Invalid journal retained for inspection: {journal path}`

**When:** Recovery scan cannot parse or trust an operation journal. Source: [operationrecovery.cpp:148](/Users/martymclean/Developer/MediaMuster/src/operationrecovery.cpp:148).

**Five simpler alternatives:**

1. Invalid operation record kept: {journal path}
2. Unreadable operation record kept for checking: {journal path}
3. Operation record needs inspection: {journal path}
4. Invalid recovery record kept: {journal path}
5. Cannot use this operation record; kept for checking: {journal path}

### O111 — Good

**Current:** `{error} Journal: {journal path}`

**When:** Recovery appends the record path to resolve, reconcile, cleanup or ownership-save errors. Also lines 189, 200 and 210. Source: [operationrecovery.cpp:170](/Users/martymclean/Developer/MediaMuster/src/operationrecovery.cpp:170).

**Five simpler alternatives:**

1. {error} Operation record: {journal path}
2. {error}\nRecord: {journal path}
3. {error} Recovery record: {journal path}
4. {error}\nOperation details: {journal path}
5. {error} Recorded in: {journal path}

### Checked and excluded

- `OperationPlan` and `RebalancePlanner` contain no Console message text. They build requests/paths and report structured results.
- `Rebalancer`'s `The media files are unavailable. Rescan and try again.` is a warning **dialog only**: `RebalanceDialog::onAborted` does not forward it to `logMessage`.
- `Copy ready; original kept until every required copy finishes.` (`oprunner.cpp:610`) is an intermediate `SourceRetained` result consumed by the runner's deferred-copy path at lines 1644–1654. It is not emitted to the in-app Console; the later completion/retention result is emitted instead.
- Progress-only labels `Copying {name}`, `Restoring original: {name}`, `Removing originals: {name}` and raw item names go to progress widgets, not Console.
- Test-hook-only strings are not reachable through the production UI: `Injected journal failure; further changes stopped.`, `Injected cleanup failure.`, `Injected native copy error {code}.`, `Injected publication failure.`, and `Injected relocation failure.` The test harness can emit them to its own sink; they are not normal application messages.
- Raw `error`, `journal.error()`, `cleanupError`, `restored.message`, `copied.error` and `trashed.error` are forwarding sites, not independent new templates. Their possible values are enumerated here or in the lower-level catalogue; arbitrary OS/Qt error strings remain external dynamic values.

### Most worthwhile operation wording fixes

1. Fix the global stop wrapper: a journal path is not where the media files are stored, and early failures may have no journal.
2. Give the final summary the user-facing operation name, say the counts cover this run, and omit an absent record path.
3. Replace “bin” in Trash failure text; it collides with Avid's ordinary meaning of Bin.
4. Use “cannot verify” when the code cannot distinguish changed, missing and inaccessible files.
5. Replace “below 5,000” with “would exceed 5,000”.
6. Report folders actually touched by Rebalance without claiming every one had its databases reset.
7. Replace retirement/publication/durability/ownership terminology with temporary location/final destination/saved confirmation/original-operation link.

## File access, copying, journals and system Trash

Read-only source audit, 24 September 2026. This section covers authored text from `OpFile`, `OpCopier`, `OpJournal`, `OpTrash` and `NativeFile` that can reach the Console through operation results, operation-stop exceptions, cleanup/recovery notes or journal-maintenance notes. `VolumeIdentity` supplies data but no messages. The outer result/stop/recovery templates are inventoried separately; combine them with these details rather than treating every possible combination as a different sentence.

`Good` means factually appropriate, not necessarily good plain English. Five simpler options are supplied for each Good entry. `Misleading` means its trigger can contradict or overstate the text; those entries have one recommended correction. “Retained” is read as “MediaMuster did not remove it”, not a guarantee against an unrelated process changing it. Paths, error codes and operating-system error descriptions vary. `%1`, `%2` and `%3` below are the exact source placeholders; `\n` denotes a line break. Braced placeholders describe concatenated values.

Source paths are linked to the current workspace. No production changes were made.

### File access, temporary folders and removal

### E001 — Good

**Current:** `Windows file error %1` — [opfile.cpp:55](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:55)

**When:** A native Windows file operation fails; `%1` is the Windows error number. Used by multiple file/folder operations.

**Five simpler alternatives:**

1. `File operation failed (Windows error %1).`
2. `Windows file operation failed: %1.`
3. `Windows reported file error %1.`
4. `File error: Windows code %1.`
5. `Could not complete the file operation (Windows %1).`

### E002 — Good

**Current:** `{POSIX error description}` — [opfile.cpp:103](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:103), also [opfile.cpp:644](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:644)

**When:** A macOS/POSIX file or folder call fails. Text comes from `strerror`; there is no finite app-authored list to rewrite.

**Five simpler alternatives:**

1. `File operation failed: {detail}`
2. `Could not complete the file operation: {detail}`
3. `System file error: {detail}`
4. `The system reported: {detail}`
5. `File error: {detail}`. Preserve the system detail.

### E003 — Misleading

**Current:** `A path contains a symbolic link or unsupported reparse point: %1` — [opfile.cpp:208](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:208)

**When:** Directory creation rejects `safePath`; `%1` is the folder path. The same check also rejects relative or unnormalised paths, so a link need not exist.

**Recommended:** `Cannot use this folder path: %1`. Report a more specific cause only if separately established.

### E004 — Good

**Current:** `Cannot create folder %1` — [opfile.cpp:223](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:223)

**When:** Creating the folder fails and it is not already a directory; `%1` is its path.

**Five simpler alternatives:**

1. `Could not create %1.`
2. `Folder creation failed: %1.`
3. `Unable to create folder: %1.`
4. `Could not make this folder: %1.`
5. `New folder could not be created: %1.`

### E005 — Good

**Current:** `Folder created, but its directory persistence is unconfirmed: %1\n%2` — [opfile.cpp:230](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:230)

**When:** Folder creation succeeded but saving its parent folder's update to storage was not confirmed. `%1` is the created folder; `%2` is the native sync error.

**Five simpler alternatives:**

1. `Folder created, but storage has not confirmed the change: %1\n%2`
2. `Created %1; could not confirm the folder change was saved.\n%2`
3. `Folder created at %1; saving the change could not be confirmed.\n%2`
4. `Created %1, but the storage confirmation failed.\n%2`
5. `Folder creation needs confirmation: %1\n%2`

### E006 — Good

**Current:** `A private operation folder requires a fresh, unredirected UUID path.` — [opfile.cpp:268](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:268)

**When:** The proposed private folder path fails the path/name checks. This states the helper's requirements; the exclusive creation performed afterwards also enforces the requirement that the folder is new. “UUID” exposes an implementation detail that is unnecessary here.

**Five simpler alternatives:**

1. `Cannot use this temporary folder path.`
2. `The operation needs a valid new temporary folder.`
3. `Temporary folder path does not meet the operation's requirements.`
4. `Could not use the proposed private folder location.`
5. `A new temporary folder with a supported path is required.`

### E007 — Good

**Current:** `The operation folder could not be made private: %1` — [opfile.cpp:323](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:323)

**When:** The macOS/POSIX temporary folder cannot be restricted to the current account or that restriction cannot be confirmed. `%1` is its path.

**Five simpler alternatives:**

1. `Could not restrict access to temporary folder: %1.`
2. `Could not make temporary folder private: %1.`
3. `Temporary folder permissions could not be secured: %1.`
4. `Could not confirm private access to folder: %1.`
5. `Private folder setup failed: %1.`

### E008 — Good

**Current:** `The newly created operation folder could not be identified.` — [opfile.cpp:332](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:332)

**When:** The newly created private directory has no valid identity, or the path no longer resolves to that directory.

**Five simpler alternatives:**

1. `Could not confirm the new temporary folder.`
2. `The new temporary folder could not be verified.`
3. `Could not identify the folder just created.`
4. `The temporary folder could not be checked.`
5. `New temporary folder verification failed.`

### E009 — Good

**Current:** `Folder cleanup requires its recorded private directory identity.` — [opfile.cpp:355](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:355)

**When:** Cleanup lacks a valid recorded folder identity or safe private folder path.

**Five simpler alternatives:**

1. `Cannot verify the temporary folder for cleanup.`
2. `Temporary folder cleanup needs a valid saved record.`
3. `Could not match this temporary folder to its record.`
4. `Cannot confirm which temporary folder to remove.`
5. `Temporary folder cleanup could not be verified.`

### E010 — Misleading

**Current:** `The private folder was replaced; it was retained.` — [opfile.cpp:371](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:371)

**When:** Windows cannot match the open directory identity to the recorded one. Reading identity can fail, so replacement is not established.

**Recommended:** `Could not confirm the temporary folder is unchanged; it was not removed.`

### E011 — Misleading

**Current:** `The private folder identity or permissions changed; it was retained.` — [opfile.cpp:408](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:408)

**When:** The macOS/POSIX directory, permissions, or path check fails. This includes inability to open/read the folder, not only proven changes.

**Recommended:** `Could not verify the temporary folder or its permissions; it was not removed.`

### E012 — Good

**Current:** `Folder cleanup is unconfirmed; the path remains occupied.` — [opfile.cpp:422](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:422)

**When:** After a removal request, something still exists at the private directory's path.

**Five simpler alternatives:**

1. `Cleanup could not be confirmed; the folder path is still in use.`
2. `The temporary folder path is still occupied after cleanup.`
3. `Could not confirm folder removal; something remains at its path.`
4. `Folder cleanup needs checking; the path is still occupied.`
5. `Folder removal is unconfirmed; its path is still in use.`

### E013 — Good

**Current:** `Unsupported or redirected path: %1` — [opfile.cpp:443](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:443)

**When:** Opening a file rejects a nonabsolute/unnormalised path, symbolic link, or Windows reparse point. `%1` is the file path.

**Five simpler alternatives:**

1. `Cannot use this file path: %1.`
2. `File path is not supported: %1.`
3. `Cannot open this path safely: %1.`
4. `This path cannot be used for the operation: %1.`
5. `File path check failed: %1.`

### E014 — Misleading

**Current:** `Only regular files are supported.` — [opfile.cpp:473](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:473), [opfile.cpp:499](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:499)

**When:** Either the object is a directory/link/nonregular file, or asking the OS for its file information failed. The latter does not prove an unsupported file type.

**Recommended:** `Could not confirm this is a regular file.`

### E015 — Good

**Current:** `Cannot attach file handle.` — [opfile.cpp:481](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:481)

**When:** Windows cannot convert the native file handle into the descriptor used by Qt.

**Five simpler alternatives:**

1. `Could not prepare the open file.`
2. `Could not use the system's open file.`
3. `File setup failed after opening.`
4. `Could not connect to the open file.`
5. `The open file could not be prepared for use.`

### E016 — Misleading

**Current:** `Cannot attach file to the copy stream.` — [opfile.cpp:511](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:511)

**When:** Qt cannot attach to an already opened descriptor. This general helper is used for inspection, moves, cleanup and restore as well as copying; a copy need not be occurring.

**Recommended:** `Could not prepare the open file for use.`

### E017 — Good

**Current:** `Could not preserve all file metadata: %1` — [opfile.cpp:561](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:561)

**When:** The macOS metadata copy fails after copying file contents. `%1` is the native error detail. Metadata here includes filesystem attributes/resource forks, not just media metadata.

**Five simpler alternatives:**

1. `Could not copy all file metadata: %1`
2. `Some file metadata could not be copied: %1`
3. `File metadata copy was incomplete: %1`
4. `Not all file metadata was preserved: %1`
5. `Could not preserve every file attribute: %1`

### E018 — Good

**Current:** `Encrypted Windows files require an encryption-aware copy. The source was retained.` — [opfile.cpp:577](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:577)

**When:** Windows marks the source as encrypted; this copier refuses it.

**Five simpler alternatives:**

1. `Cannot copy this encrypted Windows file; the source was kept.`
2. `Encrypted Windows files are not supported for copying here; the source remains.`
3. `Copy stopped because the Windows file is encrypted; the source was kept.`
4. `This copier cannot preserve Windows file encryption; the source was kept.`
5. `The encrypted source was left in place because it cannot be copied here.`

### E019 — Misleading

**Current:** `The file or its location changed before relocation.` — [opfile.cpp:592](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:592)

**When:** Either path is unsupported, or inspecting the current source fails or differs. It can be an invalid destination path or unavailable inspection, without a proven file change.

**Recommended:** `Cannot verify the file and paths before moving it.`

### E020 — Good

**Current:** `The file cannot be protected for relocation; it was retained.` — [opfile.cpp:598](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:598)

**When:** Windows has no handle with the access/sharing protection needed to move this file.

**Five simpler alternatives:**

1. `Could not secure the file for moving; it was kept.`
2. `Cannot move this file safely; it was left in place.`
3. `The required file access was unavailable; no move was made.`
4. `Could not get protected access to move the file; it was kept.`
5. `The file was kept because the required move access was unavailable.`

### E021 — Good

**Current:** `The relocated file could not be confirmed at %1. Files have been retained; recovery needs attention.` — [opfile.cpp:656](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:656)

**When:** The native move reports success, but the destination cannot be matched to the source. `%1` is the destination. Retained means no further cleanup by this function, not that the source still exists at the old path.

**Five simpler alternatives:**

1. `Could not verify the moved file at %1; recovery needs checking.`
2. `The move needs checking: could not confirm the file at %1.`
3. `Could not confirm the file after moving it to %1; check recovery.`
4. `Moved file verification failed at %1; no further cleanup was attempted.`
5. `Check recovery: the file could not be verified at %1 after the move.`

### E022 — Good

**Current:** `File retained at %1: protected removal is unavailable.` — [opfile.cpp:680](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:680)

**When:** The Windows copy path cannot remove its empty placeholder through a protected handle. `%1` is that temporary file, not the source media.

**Five simpler alternatives:**

1. `Could not safely remove the temporary file at %1.`
2. `Temporary file kept at %1; safe removal was unavailable.`
3. `Could not get the access needed to remove temporary file: %1.`
4. `Temporary file was not removed: %1.`
5. `Temporary cleanup stopped; file kept at %1.`

### E023 — Good

**Current:** `Partial cleanup requires its recorded isolated file and folder.` — [opfile.cpp:693](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:693)

**When:** An incomplete copy's path/identity or the saved temporary-folder identity cannot be validated.

**Five simpler alternatives:**

1. `Cannot verify the incomplete copy for cleanup.`
2. `Incomplete-copy cleanup needs valid file and folder records.`
3. `Could not match the incomplete copy to its saved records.`
4. `Cannot confirm which incomplete copy to remove.`
5. `Could not verify the temporary file and folder before cleanup.`

### E024 — Good

**Current:** `The partial file or its staging folder could not be protected.` — [opfile.cpp:708](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:708)

**When:** Windows lacks protected access to the partial copy, or cannot confirm its temporary folder.

**Five simpler alternatives:**

1. `Could not secure the incomplete copy for removal.`
2. `Cannot safely remove the incomplete copy from its temporary folder.`
3. `Could not verify access to the incomplete copy and its folder.`
4. `Temporary-copy cleanup could not be secured.`
5. `Could not protect the incomplete copy and folder during cleanup.`

### E025 — Misleading

**Current:** `The staging folder or partial file changed; it was retained.` — [opfile.cpp:739](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:739)

**When:** macOS cannot validate the folder's permissions/identity or the partial file. Failed inspection also reaches this branch; change is not established.

**Recommended:** `Could not verify the incomplete copy or its temporary folder; the file was not removed.`

### E026 — Good

**Current:** `Partial removed, but its staging folder could not be flushed: {native detail}` — [opfile.cpp:753](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:753)

**When:** The macOS incomplete copy was deleted, but saving the folder update fails. The suffix is a POSIX error description.

**Five simpler alternatives:**

1. `Incomplete copy removed, but saving the folder change failed: {detail}`
2. `Incomplete copy deleted; the folder update could not be confirmed: {detail}`
3. `Temporary copy removed; storage has not confirmed the folder change: {detail}`
4. `Removed the incomplete copy, but could not save its folder update: {detail}`
5. `Incomplete-copy deletion needs storage confirmation: {detail}`

### E027 — Good

**Current:** `Partial cleanup is unconfirmed; the path remains occupied.` — [opfile.cpp:763](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:763)

**When:** Something remains at the incomplete copy's path after deletion was requested.

**Five simpler alternatives:**

1. `Incomplete-copy removal is unconfirmed; its path is still in use.`
2. `Something remains at the incomplete copy's path after cleanup.`
3. `Could not confirm temporary-file removal; the path is occupied.`
4. `Temporary cleanup needs checking; its file path is still in use.`
5. `The incomplete copy's path is still occupied after removal.`

### E028 — Good

**Current:** `Original removal requires its recorded isolated retirement file.` — [opfile.cpp:784](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:784)

**When:** A copied original is not confirmed in the private temporary location required before its deletion.

**Five simpler alternatives:**

1. `Cannot verify the original in its temporary folder before removal.`
2. `Could not confirm the original is ready for safe removal.`
3. `Original removal needs a verified temporary location.`
4. `Cannot remove the original until its temporary location is confirmed.`
5. `Could not verify the original's temporary file for deletion.`

### E029 — Good

**Current:** `The retired original could not be protected for removal.` — [opfile.cpp:790](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:790)

**When:** Windows lacks protected access to the original in its temporary retirement folder.

**Five simpler alternatives:**

1. `Could not secure the original for removal.`
2. `Cannot safely remove the original from its temporary folder.`
3. `Could not get the access needed to remove the original.`
4. `Original removal stopped because protected access was unavailable.`
5. `The original could not be safely opened for deletion.`

### E030 — Misleading

**Current:** `Original removal is pending; the retirement path remains occupied.` — [opfile.cpp:806](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:806)

**When:** Windows requested deletion and closed the handle, but the path remains occupied. This does not prove deletion is merely pending; the path could have been replaced.

**Recommended:** `Original removal is unconfirmed; its temporary path is still occupied.`

### E031 — Good

**Current:** `The retirement directory or original identity is not protected.` — [opfile.cpp:831](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:831)

**When:** macOS cannot confirm private folder protection or match the original file before deletion.

**Five simpler alternatives:**

1. `Could not verify the original and its temporary folder for safe removal.`
2. `Cannot safely delete the original from this temporary folder.`
3. `Original removal stopped; file or folder checks failed.`
4. `Could not confirm safe access to the original before removal.`
5. `The original or its temporary folder could not be verified for deletion.`

### E032 — Good

**Current:** `Original removed, but its retirement directory could not be flushed: {native detail}` — [opfile.cpp:842](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:842)

**When:** macOS deleted the original, but saving the temporary folder update fails. The suffix is the POSIX error description.

**Five simpler alternatives:**

1. `Original removed, but saving the folder change failed: {detail}`
2. `Original deleted; storage has not confirmed the folder update: {detail}`
3. `Removed the original, but its folder update could not be confirmed: {detail}`
4. `Original deletion needs storage confirmation: {detail}`
5. `Original removed; could not save the temporary folder's change: {detail}`

### Native copying

### E033 — Good

**Current:** `Cannot start native copying with these source or staging identities.` — [opcopier.cpp:151](/Users/martymclean/Developer/MediaMuster/src/opcopier.cpp:151)

**When:** Source identity/location or the fresh empty temporary destination fails validation.

**Five simpler alternatives:**

1. `Cannot start copying; the source or temporary file could not be verified.`
2. `Copy stopped before starting because file checks failed.`
3. `Could not verify the files needed to start copying.`
4. `Cannot copy until the source and temporary destination are confirmed.`
5. `Source or temporary-file verification failed before copying.`

### E034 — Good

**Current:** `Cannot position the native copy handles.` — [opcopier.cpp:162](/Users/martymclean/Developer/MediaMuster/src/opcopier.cpp:162)

**When:** macOS cannot seek to the start of the source or temporary destination.

**Five simpler alternatives:**

1. `Could not move to the start of the files for copying.`
2. `Could not reset the files before copying.`
3. `Cannot copy from the start of the files.`
4. `File positioning failed before copying.`
5. `Could not prepare the file positions for copying.`

### E035 — Good

**Current:** `Cannot allocate native copy state.` — [opcopier.cpp:168](/Users/martymclean/Developer/MediaMuster/src/opcopier.cpp:168)

**When:** macOS `copyfile_state_alloc` fails.

**Five simpler alternatives:**

1. `Could not prepare the system copy operation.`
2. `System copy setup failed.`
3. `Could not allocate the resources needed to copy.`
4. `Cannot start the system copier.`
5. `The system could not prepare copying.`

### E036 — Good

**Current:** `Native copying failed: %1 (POSIX %2).` — [opcopier.cpp:182](/Users/martymclean/Developer/MediaMuster/src/opcopier.cpp:182)

**When:** macOS's copy call returns an error; `%1` is its description and `%2` its numeric code. An intentional cancellation can also make the copy call fail; the surrounding result then says Cancelled.

**Five simpler alternatives:**

1. `Copy did not finish: %1 (error %2).`
2. `System copy failed: %1 (%2).`
3. `Could not complete copying: %1 (error %2).`
4. `Copy stopped: %1 (system error %2).`
5. `Copy error %2: %1.`

### E037 — Misleading

**Current:** `A native copying handle referred to a changed file.` — [opcopier.cpp:255](/Users/martymclean/Developer/MediaMuster/src/opcopier.cpp:255)

**When:** A Windows copy callback cannot validate the actual source/destination handle. The callback also treats inability to read/duplicate a handle as a changed file.

**Recommended:** `Could not verify the files opened by the system copier.`

### E038 — Misleading

**Current:** `Native copying could not be completed and protected (Windows %1). %2 %3` — [opcopier.cpp:255](/Users/martymclean/Developer/MediaMuster/src/opcopier.cpp:255)

**When:** Windows copying, reopening, destination identity or restoring attributes fails. `%1` is `GetLastError` after `CopyFileEx`; `%2` and `%3` are optional source/destination reopen errors. When the copy succeeded and a later check failed, `%1` can be stale and describe an unrelated error.

**Recommended:** `Could not complete or verify the copy. {relevant details}`. Include the Windows copy error only when that copy call failed; retain the actual later check's reason otherwise.

### E039 — Misleading

**Current:** `The source changed during copying; it has been retained.` — [opcopier.cpp:282](/Users/martymclean/Developer/MediaMuster/src/opcopier.cpp:282)

**When:** The original cannot be confirmed at its source path after copying. Inability to inspect it also fails the check.

**Recommended:** `Could not confirm the source is unchanged after copying; MediaMuster has not removed it.`

### E040 — Good

**Current:** `The destination could not confirm its writes.` — [opcopier.cpp:296](/Users/martymclean/Developer/MediaMuster/src/opcopier.cpp:296)

**When:** Flushing the copied file to storage fails.

**Five simpler alternatives:**

1. `Could not confirm the copy was saved to storage.`
2. `Storage did not confirm the copied data was saved.`
3. `Could not finish saving the copied file.`
4. `The copied file's storage update failed.`
5. `Copy completion could not be confirmed by storage.`

### E041 — Misleading

**Current:** `The destination length differs from the source.` — [opcopier.cpp:302](/Users/martymclean/Developer/MediaMuster/src/opcopier.cpp:302)

**When:** The copied file's inspected size differs from the original size. A failed inspection produces an invalid stamp whose size is also compared, so an actual size difference is not always known.

**Recommended:** `Could not confirm the copy has the source file's size.`

### E042 — Misleading

**Current:** `The source changed before copying finished; it has been retained.` — [opcopier.cpp:307](/Users/martymclean/Developer/MediaMuster/src/opcopier.cpp:307)

**When:** A second source identity/location check fails after metadata and flush work. As above, failed inspection does not establish change.

**Recommended:** `Could not confirm the source is unchanged at the end of copying; MediaMuster has not removed it.`

### Journal and storage confirmation

### E043 — Misleading

**Current:** `Another file operation or recovery owns the journal. Try again after it finishes.` — [opjournal.cpp:322](/Users/martymclean/Developer/MediaMuster/src/opjournal.cpp:322)

**When:** `QLockFile::tryLock` fails. That can mean a competing lock, but also insufficient permissions or another lock-file error; waiting need not fix it.

**Recommended:** `Could not lock the operation journal. {lock failure reason}`. Use the existing “another operation” wording only for an actual competing lock.

### E044 — Good

**Current:** `The operation journal could not be saved. Further changes have stopped; files and recovery records were retained.` — [opjournal.cpp:336](/Users/martymclean/Developer/MediaMuster/src/opjournal.cpp:336)

**When:** Appending or flushing the operation record fails and the journal is marked unhealthy. Earlier completed actions are not undone; no claim is made that every original remains at its former path.

**Five simpler alternatives:**

1. `Could not save the operation record. Further changes stopped; files and recovery records were kept.`
2. `Operation stopped because its record could not be saved. Remaining files and recovery records were kept.`
3. `Could not update the operation history; further changes stopped and recovery records were kept.`
4. `Saving the operation record failed. No further changes will be made; files and recovery information were kept.`
5. `Operation record could not be saved; work stopped with files and recovery information retained.`

### E045 — Good

**Current:** `{Qt file error description}` — [opjournal.cpp:353](/Users/martymclean/Developer/MediaMuster/src/opjournal.cpp:353), [opjournal.cpp:436](/Users/martymclean/Developer/MediaMuster/src/opjournal.cpp:436), [opjournal.cpp:443](/Users/martymclean/Developer/MediaMuster/src/opjournal.cpp:443)

**When:** Creating, reopening, or trimming an incomplete final journal record fails. Text comes from `QFile::errorString`, not an app-authored fixed message.

**Five simpler alternatives:**

1. `Operation record error: {detail}`
2. `Could not prepare the operation record: {detail}`
3. `Operation journal error: {detail}`
4. `Could not access or repair the operation record: {detail}`
5. `Operation record could not be prepared: {detail}`. Preserve the supplied detail.

### E046 — Good

**Current:** `Cannot persist the journal directory.\n{sync error}` — [opjournal.cpp:420](/Users/martymclean/Developer/MediaMuster/src/opjournal.cpp:420)

**When:** The new journal was written, but saving its directory update is unconfirmed. The suffix is one of the native directory messages below.

**Five simpler alternatives:**

1. `Could not confirm the new operation record was saved.\n{detail}`
2. `Could not save the operation record's folder update.\n{detail}`
3. `Storage did not confirm the new journal's folder change.\n{detail}`
4. `New operation record needs storage confirmation.\n{detail}`
5. `Could not confirm the operation folder update.\n{detail}`

### E047 — Good

**Current:** `Journal contains an invalid record; it was preserved for inspection.` — [opjournal.cpp:429](/Users/martymclean/Developer/MediaMuster/src/opjournal.cpp:429)

**When:** Resuming a journal marked corrupt is refused.

**Five simpler alternatives:**

1. `The operation record is invalid; it was kept for checking.`
2. `Invalid journal kept for inspection.`
3. `Cannot resume an invalid operation record; it was kept.`
4. `The saved operation contains invalid data; it was kept for review.`
5. `Operation record could not be used; the invalid record was retained.`

### E048 — Good

**Current:** `Cannot determine the journal retention date.` — [opjournal.cpp:747](/Users/martymclean/Developer/MediaMuster/src/opjournal.cpp:747)

**When:** Journal pruning receives an invalid date. Production supplies the current date; this is a defensive branch, not a normal message.

**Five simpler alternatives:**

1. `Cannot determine which operation records have expired.`
2. `Could not calculate the operation-record cleanup date.`
3. `Operation-record cleanup needs a valid current date.`
4. `Could not determine the cutoff date for old operation records.`
5. `Cannot check operation-record ages without a valid date.`

### E049 — Good

**Current:** `Cannot remove expired journal: {path}. {Qt file error}` — [opjournal.cpp:805](/Users/martymclean/Developer/MediaMuster/src/opjournal.cpp:805)

**When:** Removing an eligible expired operation record fails. The controller adds `Journal cleanup:`.

**Five simpler alternatives:**

1. `Could not delete old operation record: {path}. {detail}`
2. `Old operation record could not be removed: {path}. {detail}`
3. `Operation-record cleanup failed for {path}: {detail}`
4. `Could not remove expired operation record {path}: {detail}`
5. `Old journal was not deleted: {path}. {detail}`

### E050 — Good

**Current:** `Cannot read recovery record.` — [opjournal.cpp:820](/Users/martymclean/Developer/MediaMuster/src/opjournal.cpp:820)

**When:** Stopping an unfinished job cannot load its journal. Can appear directly through the controller's error log.

**Five simpler alternatives:**

1. `Could not read the recovery record.`
2. `Cannot load the saved operation.`
3. `Could not open the operation record for recovery.`
4. `The recovery record could not be loaded.`
5. `Saved operation could not be read.`

### E051 — Good

**Current:** `Cannot establish the recorded source storage for restoration. Originals were retained.` — [opjournal.cpp:858](/Users/martymclean/Developer/MediaMuster/src/opjournal.cpp:858)

**When:** Restoring interrupted originals cannot associate the saved source/retirement paths with a valid recorded storage root. This is record/path validation, not necessarily an unplugged drive.

**Five simpler alternatives:**

1. `Cannot identify the source storage needed to restore originals; they were kept.`
2. `Originals were kept because their recorded storage could not be identified.`
3. `Could not determine where to restore the originals; they were retained.`
4. `Original restoration stopped; the saved source storage could not be confirmed.`
5. `Could not match the original files to their recorded storage; no originals were removed.`

### E052 — Good

**Current:** `The recorded volume has more than one possible mount; files were retained.` — [opjournal.cpp:900](/Users/martymclean/Developer/MediaMuster/src/opjournal.cpp:900)

**When:** More than one distinct mounted path matches a recorded volume.

**Five simpler alternatives:**

1. `More than one mounted volume matches the saved record; files were kept.`
2. `Cannot choose between matching storage locations; files were kept.`
3. `The saved volume matches multiple locations; no files were removed.`
4. `Could not uniquely identify the recorded volume; files were retained.`
5. `Several mounted locations match this volume; files were left alone.`

### E053 — Good

**Current:** `Cannot establish the recorded volume at %1. Reconnect the original storage; recovery records are retained.` — [opjournal.cpp:909](/Users/martymclean/Developer/MediaMuster/src/opjournal.cpp:909)

**When:** No currently detected volume matches the saved volume identity. `%1` is its former root path. Reconnection is a reasonable recovery suggestion, but not proof that disconnection was the cause.

**Five simpler alternatives:**

1. `Could not identify the original storage at %1. Reconnect it; recovery records were kept.`
2. `Original volume at %1 could not be confirmed. Reconnect the storage to continue recovery.`
3. `Cannot match the saved volume at %1. Check that the original storage is connected; recovery records remain.`
4. `Recovery is waiting for the original storage from %1; its records were kept.`
5. `Could not confirm the original volume at %1. Check its connection; recovery information was retained.`

### E054 — Good

**Current:** `CreateFileW(directory) failed for %1 (Windows error %2).` — [nativefile.cpp:75](/Users/martymclean/Developer/MediaMuster/src/nativefile.cpp:75)

**When:** Windows cannot open a directory to save its pending changes. `%1` is the directory; `%2` the Windows error number.

**Five simpler alternatives:**

1. `Could not open folder %1 to save its changes (Windows %2).`
2. `Cannot prepare folder %1 for saving (Windows error %2).`
3. `Could not access folder %1 for storage confirmation (Windows %2).`
4. `Folder update could not be opened: %1 (Windows error %2).`
5. `Could not open %1 to confirm its changes (Windows %2).`

### E055 — Good

**Current:** `FlushFileBuffers(directory) failed for %1 (Windows error %2).` — [nativefile.cpp:84](/Users/martymclean/Developer/MediaMuster/src/nativefile.cpp:84)

**When:** Windows cannot confirm a directory's update is saved. `%1` is the directory; `%2` the Windows error number. Some storage simply does not support this request.

**Five simpler alternatives:**

1. `Could not confirm folder changes were saved: %1 (Windows %2).`
2. `Folder update could not be confirmed: %1 (Windows error %2).`
3. `Storage did not confirm changes to %1 (Windows %2).`
4. `Could not finish saving folder changes at %1 (Windows error %2).`
5. `Saving the folder update failed for %1 (Windows %2).`

### E056 — Good

**Current:** `open(directory) failed for %1 (POSIX error %2: %3).` — [nativefile.cpp:101](/Users/martymclean/Developer/MediaMuster/src/nativefile.cpp:101)

**When:** macOS/POSIX cannot open the directory for confirming its saved state. `%1` is the path; `%2` and `%3` are the system error number and description.

**Five simpler alternatives:**

1. `Could not open folder %1 to save its changes (error %2: %3).`
2. `Cannot prepare folder %1 for saving (error %2: %3).`
3. `Could not access folder %1 for storage confirmation (error %2: %3).`
4. `Could not open folder %1: %3 (error %2).`
5. `Folder update could not be opened: %1 (error %2: %3).`

### E057 — Good

**Current:** `fsync(directory) failed for %1 (POSIX error %2: %3).` — [nativefile.cpp:111](/Users/martymclean/Developer/MediaMuster/src/nativefile.cpp:111)

**When:** macOS/POSIX cannot confirm the directory update was saved. `%1` is the path; `%2` and `%3` are the system error number and description.

**Five simpler alternatives:**

1. `Could not confirm folder changes were saved: %1 (error %2: %3).`
2. `Folder update could not be confirmed: %1 (%3, error %2).`
3. `Storage did not confirm changes to %1 (error %2: %3).`
4. `Could not finish saving folder changes at %1: %3 (error %2).`
5. `Saving the folder update failed for %1 (error %2: %3).`

### System Trash: shared and macOS

The ordinary “system Trash unavailable” path defers to a choice dialog and often replaces the provider's detail with the final fallback outcome. The provider failures below reach the Console when returned as failed/cancelled/uncertain results, when restoring from Trash, or as nested recovery details. Windows `refused(...)` prefixes are included because the same native failure becomes Failed if source verification fails, or Cancelled if cancellation occurs. They are not necessarily printed for a routine fallback that succeeds.

### E058 — Good

**Current:** `Cancelled before system Trash.` — [optrash.cpp:61](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:61), [optrash_mac.mm:45](/Users/martymclean/Developer/MediaMuster/src/optrash_mac.mm:45)

**When:** Cancellation is already requested before the system Trash call.

**Five simpler alternatives:**

1. `Cancelled before moving the file to Trash.`
2. `Trash move cancelled before starting.`
3. `Cancelled; no system Trash move was started.`
4. `Stopped before moving to system Trash.`
5. `System Trash move was cancelled before it began.`

### E059 — Misleading

**Current:** `The original changed before system Trash.` — [optrash.cpp:63](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:63)

**When:** The expected identity is invalid, the path unsupported, or current file inspection fails or differs. This need not establish an actual change.

**Recommended:** `Could not verify the original before moving it to Trash.`

### E060 — Good

**Current:** `Cancelled before Trash restoration.` — [optrash.cpp:77](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:77), [optrash_mac.mm:93](/Users/martymclean/Developer/MediaMuster/src/optrash_mac.mm:93)

**When:** Cancellation is requested before restoring the file from Trash.

**Five simpler alternatives:**

1. `Cancelled before restoring the file from Trash.`
2. `Trash restore cancelled before starting.`
3. `Cancelled; the Trash item was not restored.`
4. `Stopped before restoring from Trash.`
5. `Restoring from Trash was cancelled before it began.`

### E061 — Good

**Current:** `The Trash receipt or restore destination cannot be confirmed.` — [optrash.cpp:88](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:88)

**When:** The saved Trash record, identity or restore path is invalid, the path unsafe, or the restore destination occupied.

**Five simpler alternatives:**

1. `Cannot verify the Trash record or restore location.`
2. `Could not confirm the saved Trash item or where to restore it.`
3. `Trash restore stopped because record or destination checks failed.`
4. `Cannot restore this item until its record and destination are verified.`
5. `The saved Trash details or destination could not be checked.`

### E062 — Good

**Current:** `The system returned no Trash result.` — [optrash_mac.mm:15](/Users/martymclean/Developer/MediaMuster/src/optrash_mac.mm:15)

**When:** macOS supplies no error object after a failed or incomplete Trash response. No usable confirmed result is available; the function's boolean return alone is insufficient.

**Five simpler alternatives:**

1. `macOS did not confirm the Trash result.`
2. `No confirmed Trash result was returned.`
3. `Could not confirm the system Trash outcome.`
4. `The Trash result could not be determined.`
5. `macOS returned no usable Trash result.`

### E063 — Good

**Current:** `%1 (%2, code %3)` — [optrash_mac.mm:19](/Users/martymclean/Developer/MediaMuster/src/optrash_mac.mm:19)

**When:** macOS returns an `NSError`. `%1` is its localised description, `%2` its error domain, `%3` its code. Up to four nested error descriptions are joined with `;`. Their prose is supplied by macOS and cannot be exhaustively enumerated as app text.

**Five simpler alternatives:**

1. `%1 (error %3, %2)`
2. `%1 [%2: %3]`
3. `%1 — %2 error %3`
4. `%1; error code %3 (%2)`
5. `%1 (%2/%3)`. Keep descriptions and codes; merely removing useful native details would not improve accuracy.

### E064 — Misleading

**Current:** `The source changed before system Trash.` — [optrash_mac.mm:47](/Users/martymclean/Developer/MediaMuster/src/optrash_mac.mm:47)

**When:** macOS's final pre-Trash path/identity check fails, including inability to inspect the file.

**Recommended:** `Could not verify the source before moving it to Trash.`

### E065 — Good

**Current:** `The system Trash result needs identity or location reconciliation.` — [optrash_mac.mm:73](/Users/martymclean/Developer/MediaMuster/src/optrash_mac.mm:73)

**When:** macOS returns a Trash location, but its path/identity or the source's disappearance cannot be verified.

**Five simpler alternatives:**

1. `Could not verify the file's location after moving it to Trash.`
2. `The system Trash result needs checking.`
3. `Could not confirm which file reached Trash or where it is.`
4. `Trash move returned a result that could not be verified.`
5. `The file and location in Trash need confirmation.`

### E066 — Good

**Current:** `File is in system Trash, but directory persistence needs confirmation. {sync error}` — [optrash_mac.mm:80](/Users/martymclean/Developer/MediaMuster/src/optrash_mac.mm:80)

**When:** The file is confirmed in macOS Trash, but saving source/Trash folder changes is unconfirmed.

**Five simpler alternatives:**

1. `File is in Trash, but storage has not confirmed the folder changes. {detail}`
2. `Moved to Trash; saving the folder updates could not be confirmed. {detail}`
3. `File reached Trash, but the storage update needs checking. {detail}`
4. `File is in Trash; the folder changes still need confirmation. {detail}`
5. `Trash move completed, but storage confirmation failed. {detail}`

### E067 — Good

**Current:** `This is not a macOS Trash receipt.` — [optrash_mac.mm:92](/Users/martymclean/Developer/MediaMuster/src/optrash_mac.mm:92)

**When:** The saved provider in a restore record is not `macos`.

**Five simpler alternatives:**

1. `This Trash record is not for macOS.`
2. `Cannot restore a non-macOS Trash record here.`
3. `The saved Trash record belongs to another provider.`
4. `macOS cannot use this Trash record.`
5. `This is not a macOS system Trash record.`

### E068 — Misleading

**Current:** `The recorded Trash item changed or is missing. {file error}` — [optrash_mac.mm:99](/Users/martymclean/Developer/MediaMuster/src/optrash_mac.mm:99)

**When:** Opening or identifying the recorded macOS Trash item fails. A permissions error also reaches this branch; absence/change is not established. The appended file error may be empty.

**Recommended:** `Could not verify the recorded Trash item. {file error}`

### E069 — Good

**Current:** `Restored file needs directory-persistence confirmation. {sync error}` — [optrash_mac.mm:108](/Users/martymclean/Developer/MediaMuster/src/optrash_mac.mm:108)

**When:** The file was restored from macOS Trash, but saving the affected folder changes is unconfirmed.

**Five simpler alternatives:**

1. `File restored, but storage has not confirmed the folder changes. {detail}`
2. `Restore completed; saving the folder updates could not be confirmed. {detail}`
3. `Restored file's folder changes need confirmation. {detail}`
4. `File is restored, but the storage update needs checking. {detail}`
5. `File restored; folder updates could not be confirmed. {detail}`

### System Trash: Windows

`{code}` in E075–E081 and E083–E085 below is the hexadecimal Windows HRESULT generated by the shared formatter at [optrash.cpp:159](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:159), including the displayed `0x` prefix. Entries show the final expanded template rather than separately enumerating the prefix and wrapper as two messages.

### E070 — Good

**Current:** `The Shell operation has no confirmed recoverable result (HRESULT %1).` — [optrash.cpp:290](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:290)

**When:** The Windows Trash restore completion callback reports failure or supplies no created item. `%1` is the hexadecimal HRESULT, without an added `0x` prefix in this template.

**Five simpler alternatives:**

1. `Could not confirm the restored file (Windows error %1).`
2. `Windows did not return a confirmed restore result (%1).`
3. `Trash restore result could not be verified (Windows %1).`
4. `Could not confirm where the restored file is (Windows %1).`
5. `Restoring from Trash returned no confirmed result (Windows %1).`

### E071 — Good

**Current:** `The Shell result needs identity or location reconciliation.` — [optrash.cpp:299](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:299)

**When:** Windows's returned restored item does not match the expected identity/location, or the source still appears occupied.

**Five simpler alternatives:**

1. `Could not verify the restored file or its location.`
2. `The restored file's identity or location needs checking.`
3. `Windows returned a restore result that could not be confirmed.`
4. `Could not confirm where the expected file was restored.`
5. `Trash restore result needs file and location checks.`

### E072 — Good

**Current:** `The Shell operation completed, but directory persistence needs confirmation. {sync error}` — [optrash.cpp:306](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:306)

**When:** Windows restored the file from Trash, but the folder updates could not be confirmed on storage.

**Five simpler alternatives:**

1. `File restored, but storage has not confirmed the folder changes. {detail}`
2. `Restore completed; the folder updates need confirmation. {detail}`
3. `Windows restored the file, but saving its folder changes could not be confirmed. {detail}`
4. `Restored file's folder changes need checking. {detail}`
5. `File restored; storage confirmation failed for the folder updates. {detail}`

### E073 — Good

**Current:** `Windows Shell operations are unavailable.` — [optrash.cpp:320](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:320)

**When:** Windows cannot create the system file-operation service used to restore from Trash.

**Five simpler alternatives:**

1. `Windows file operations could not be started.`
2. `Windows could not prepare the Trash restore.`
3. `The Windows file-operation service is unavailable.`
4. `Could not start the Windows restore operation.`
5. `Windows could not provide its file-operation service.`

### E074 — Good

**Current:** `System Trash operation failed (HRESULT %1%2).` — [optrash.cpp:349](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:349)

**When:** Windows Trash restore did not produce a successful result and no more specific callback error exists. `%1` is the hexadecimal result; `%2` is empty or `; aborted`. “Operation” includes restore, but naming restore would be clearer.

**Five simpler alternatives:**

1. `Restoring from Trash failed (Windows %1%2).`
2. `Could not restore the Trash item (Windows %1%2).`
3. `Windows Trash restore did not finish (%1%2).`
4. `Trash restore was unsuccessful (Windows %1%2).`
5. `System restore from Trash failed (Windows %1%2).`

### E075 — Good

**Current:** `The system bin could not initialize its Shell thread (Windows HRESULT 0x{code}).` — [optrash.cpp:361](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:361)

**When:** Windows cannot initialise the COM environment needed for recycling. See the Console-routing note above.

**Five simpler alternatives:**

1. `Could not prepare Windows recycling (error 0x{code}).`
2. `Windows could not start the Recycle Bin operation (0x{code}).`
3. `Recycle Bin setup failed (Windows 0x{code}).`
4. `Could not start system recycling (Windows 0x{code}).`
5. `Windows recycling could not be initialised (0x{code}).`

### E076 — Good

**Current:** `The system bin could not identify the source (Windows HRESULT 0x{code}).` — [optrash.cpp:367](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:367)

**When:** Windows cannot create its Shell item for the source file.

**Five simpler alternatives:**

1. `Windows could not identify the file for recycling (0x{code}).`
2. `Could not prepare the source for the Recycle Bin (Windows 0x{code}).`
3. `Recycle Bin could not identify this file (Windows 0x{code}).`
4. `Source file could not be identified by Windows (0x{code}).`
5. `Could not locate the source through Windows recycling (0x{code}).`

### E077 — Good

**Current:** `The system bin could not identify the source folder (Windows HRESULT 0x{code}).` — [optrash.cpp:370](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:370)

**When:** Windows cannot obtain the parent Shell folder of the source item.

**Five simpler alternatives:**

1. `Windows could not identify the file's folder (0x{code}).`
2. `Could not find the source folder for recycling (Windows 0x{code}).`
3. `Recycle Bin could not identify the source folder (Windows 0x{code}).`
4. `The source folder could not be resolved by Windows (0x{code}).`
5. `Could not prepare the file's folder for recycling (Windows 0x{code}).`

### E078 — Misleading

**Current:** `This folder does not provide native recycling (Windows HRESULT 0x{code}).` — [optrash.cpp:374](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:374)

**When:** Binding the folder's transfer handler fails. That could be unavailable support, access failure, or another native error; it does not necessarily prove the folder never supports recycling.

**Recommended:** `Could not start recycling from this folder (Windows error 0x{code}).`

### E079 — Good

**Current:** `The system bin could not install its failure handler (Windows HRESULT 0x{code}).` — [optrash.cpp:380](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:380)

**When:** Windows refuses the recycling status/error callback needed to prevent unsafe fallback behaviour.

**Five simpler alternatives:**

1. `Could not set up safe recycling (Windows 0x{code}).`
2. `Recycle Bin error handling could not be set up (Windows 0x{code}).`
3. `Windows recycling checks could not be prepared (0x{code}).`
4. `Could not register the recycling error checks (Windows 0x{code}).`
5. `Recycling setup failed while enabling error handling (Windows 0x{code}).`

### E080 — Good

**Current:** `The system bin is unavailable (Windows HRESULT 0x{code}).` — [optrash.cpp:389](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:389)

**When:** Windows cannot obtain the virtual Recycle Bin folder.

**Five simpler alternatives:**

1. `Recycle Bin is unavailable (Windows 0x{code}).`
2. `Could not access the Windows Recycle Bin (0x{code}).`
3. `Windows could not open its Recycle Bin (0x{code}).`
4. `The system Recycle Bin could not be accessed (Windows 0x{code}).`
5. `Recycle Bin access failed (Windows 0x{code}).`

### E081 — Good

**Current:** `Cancelled before system bin recycling (Windows HRESULT 0x{code}).` — [optrash.cpp:391](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:391)

**When:** Cancellation is requested before `RecycleItem`; the code is the Windows cancelled result.

**Five simpler alternatives:**

1. `Cancelled before moving to the Recycle Bin (Windows 0x{code}).`
2. `Recycling cancelled before starting (Windows 0x{code}).`
3. `Stopped before the Recycle Bin move (Windows 0x{code}).`
4. `Cancelled; recycling was not started (Windows 0x{code}).`
5. `Recycle Bin move cancelled before it began (Windows 0x{code}).`

### E082 — Misleading

**Current:** `The source changed before system bin recycling.` — [optrash.cpp:394](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:394)

**When:** Windows cannot confirm the source identity/path, or its Shell path does not match. Failed inspection also triggers this.

**Recommended:** `Could not verify the source before moving it to the Recycle Bin.`

### E083 — Good

**Current:** `The system bin could not recycle this file (Windows HRESULT 0x{code}).` — [optrash.cpp:411](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:411), [optrash.cpp:416](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:416)

**When:** Windows's dedicated recycling call fails. A returned location may still need recovery checks, so avoid claiming the file definitely stayed at its source.

**Five simpler alternatives:**

1. `Could not complete the Recycle Bin move (Windows 0x{code}).`
2. `Windows could not finish recycling this file (0x{code}).`
3. `Recycling failed (Windows 0x{code}).`
4. `The Recycle Bin move did not complete successfully (Windows 0x{code}).`
5. `Could not finish moving this file to the Recycle Bin (Windows 0x{code}).`

### E084 — Good

**Current:** `Native failure detail (Windows HRESULT 0x{code}).` — [optrash.cpp:413](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:413)

**When:** A recycling callback supplies a different error code from the main recycling call. Appended to E083 with a space.

**Five simpler alternatives:**

1. `Additional Windows error: 0x{code}.`
2. `Windows also reported error 0x{code}.`
3. `More error detail: Windows 0x{code}.`
4. `Related Windows error: 0x{code}.`
5. `Underlying recycling error: Windows 0x{code}.`

### E085 — Good

**Current:** `The system bin result needs identity or location reconciliation (Windows HRESULT 0x{code}).` — [optrash.cpp:428](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:428)

**When:** Windows reports success but supplies no verifiable matching file/location/restore identifier, or the source still exists. The displayed code can be a success code because the validation, not the call, failed.

**Five simpler alternatives:**

1. `Could not verify the Recycle Bin result (Windows result 0x{code}).`
2. `The recycled file or its location needs checking (Windows result 0x{code}).`
3. `Windows returned a recycling result that could not be confirmed (0x{code}).`
4. `Could not confirm which file reached the Recycle Bin (Windows result 0x{code}).`
5. `Recycle Bin file and location could not be verified (Windows result 0x{code}).`

### E086 — Good

**Current:** `File is in the system bin, but directory persistence needs confirmation. {sync error}` — [optrash.cpp:435](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:435)

**When:** The file is confirmed in the Windows Recycle Bin, but saving folder changes is unconfirmed.

**Five simpler alternatives:**

1. `File is in the Recycle Bin, but storage has not confirmed the folder changes. {detail}`
2. `Moved to the Recycle Bin; folder updates need confirmation. {detail}`
3. `File reached the Recycle Bin, but saving the folder changes could not be confirmed. {detail}`
4. `File is recycled; the storage update needs checking. {detail}`
5. `Recycle Bin move completed, but folder changes remain unconfirmed. {detail}`

### E087 — Good

**Current:** `This is not a Windows Trash receipt.` — [optrash.cpp:446](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:446)

**When:** The saved restore provider is not `windows`.

**Five simpler alternatives:**

1. `This Trash record is not for Windows.`
2. `Cannot restore a non-Windows Trash record here.`
3. `The saved Trash record belongs to another provider.`
4. `Windows cannot use this Trash record.`
5. `This is not a Windows Recycle Bin record.`

### E088 — Good

**Current:** `The Shell Trash identifier is invalid.` — [optrash.cpp:449](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:449)

**When:** The saved Windows item identifier fails decoding/structure checks.

**Five simpler alternatives:**

1. `The saved Recycle Bin identifier is invalid.`
2. `Cannot use the saved Recycle Bin item ID.`
3. `The saved Trash item ID is invalid.`
4. `Windows Trash record contains an invalid item ID.`
5. `Could not read a valid identifier for the recycled item.`

### E089 — Good

**Current:** `A Shell STA thread could not be initialized.` — [optrash.cpp:452](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:452)

**When:** Windows cannot initialise COM for restoring from the Recycle Bin.

**Five simpler alternatives:**

1. `Could not prepare Windows to restore this file.`
2. `Windows restore setup failed.`
3. `Could not start the Windows file-restore service.`
4. `Windows could not prepare the Recycle Bin restore.`
5. `System restore from the Recycle Bin could not be set up.`

### E090 — Good

**Current:** `Cancelled while locating the Trash item.` — [optrash.cpp:476](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:476)

**When:** Cancellation occurs while searching the Windows Recycle Bin for a saved file at a remapped location.

**Five simpler alternatives:**

1. `Cancelled while finding the Recycle Bin item.`
2. `Recycle Bin search cancelled.`
3. `Cancelled while searching for the deleted file.`
4. `Stopped searching the Recycle Bin.`
5. `Finding the saved Trash item was cancelled.`

### E091 — Good

**Current:** `More than one Trash item matches the saved identity.` — [optrash.cpp:486](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:486)

**When:** Windows Recycle Bin enumeration finds multiple items matching the recorded path/identity.

**Five simpler alternatives:**

1. `More than one Recycle Bin item matches the saved file.`
2. `Several Trash items match the record.`
3. `Could not choose a single matching Recycle Bin item.`
4. `The saved file matches multiple items in the Recycle Bin.`
5. `More than one matching Trash item was found.`

### E092 — Misleading

**Current:** `The recorded Recycle Bin item changed or is missing.` — [optrash.cpp:494](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:494)

**When:** Windows cannot obtain or verify the saved item. Access/inspection failures also trigger this; actual absence/change is not established.

**Recommended:** `Could not find and verify the recorded Recycle Bin item.`

### E093 — Good

**Current:** `Trash restoration requires its original local filesystem.` — [optrash.cpp:498](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:498)

**When:** Windows Trash restore's source/destination device check is unavailable, crosses devices, or targets network storage. This implementation requires the same confirmed local device.

**Five simpler alternatives:**

1. `Can only restore this file to its original local storage.`
2. `Restore needs the original local drive to be confirmed.`
3. `Cannot restore until the original local storage is available and verified.`
4. `This Trash restore requires the same local drive.`
5. `The restore destination must be on the original local storage.`

### Similar text excluded from the Console inventory

- `Network storage uses MediaMuster Trash.` ([optrash.cpp:65](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:65)): the runner normally detects network storage before calling this provider. If the classification changes between checks, the provider returns Unavailable and its reason goes into the fallback choice; the final Console message comes from the runner. No direct Console route found for this exact sentence.
- `System Trash is unavailable on this platform.` ([optrash.cpp:69](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:69)): non-macOS/non-Windows branch, outside the app's supported builds; also preceded by an always-true network classification on those platforms.
- `System Trash restoration is unavailable.` ([optrash.cpp:96](/Users/martymclean/Developer/MediaMuster/src/optrash.cpp:96)), `Native no-overwrite relocation is not implemented on this platform.` ([opfile.cpp:650](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:650)), `Protected partial removal is unavailable on this platform.` ([opfile.cpp:758](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:758)), `Protected original removal is unavailable on this platform.` ([opfile.cpp:845](/Users/martymclean/Developer/MediaMuster/src/opfile.cpp:845)), and `Native copying is unavailable on this platform.` ([opcopier.cpp:272](/Users/martymclean/Developer/MediaMuster/src/opcopier.cpp:272)): compile-time fallback branches, not present in supported macOS/Windows builds.
- File identity JSON keys, journal step names, Trash receipt providers and filesystem IDs are data, not Console sentences.
- Test-injected faults are not production Console inputs.

### Coverage and cautions

There are **93 entries: 73 Good and 20 Misleading**. Two Good entries represent borrowed Qt/POSIX error prose, one represents the macOS error wrapper, and the rest are authored fixed messages or templates. Repeated literals were combined; Windows/macOS wording differences remain separate.

This is a source-based inventory of reachable message families, not evidence that each failure has been reproduced on both operating systems. Some fail-closed branches are exceptional or require external changes during an operation. Five alternatives are wording choices, not five proposed messages to add. The five options for a message should not all be used.

## Follow-up: selected Console wording implemented

The original catalogue and IDs above are retained for the ongoing review. The following choices were subsequently implemented in the application:

- M003–M009, M011–M014, M016, M018, M020, M022, M024 and M025: selected wording applied. M003 retains the later workspace edit: `I quit unexpectedly. Go to Help > Reveal Logs and send them to developer.`
- M015: the dialog-opening message was removed. `Rebalance started.` is now emitted by `Rebalancer` when execution begins. Existing outcomes remain.
- M023: the CSV completion message was removed. The initial message distinguishes selected from visible rows; the failure message remains.
- M008/M009: subsequently renamed to `Precompute filters are ON for this session.` and `Precompute filters are OFF for this session.`
- M010: subsequently renamed to `Precompute filter removed.` Clearing this filter still leaves the feature enabled.
- M011: subsequently renamed to `Precompute filter active: {choices} in {location}.`
- M026: the redundant empty-match branch and its message were removed. A selected visible row supplies each matched MasterMobId, so at least one visible match is guaranteed before the selection is updated.
- M027: subsequently simplified back to total matching visible files, including the original selection. One aggregate message is emitted: `Selected 1 file across 1 master clip.`, `Selected 2 files across 1 master clip.`, or `Selected 8 files across 3 master clips.` Distinct master clips are counted by MasterMobId. Repeating the command reports the same totals. Existing selections are kept, including those hidden by filters.

Validation covers the app build, the UI suite and the file-operation suite. Relatives regression cases cover solo clips, multiple tracks, repeated selection, filters, and distinct master IDs sharing a clip name. Selections temporarily hidden by filters are retained when adding visible relatives.

Further selected wording was applied for M028, M030, M032, M034–M036, M038, M041–M043, M050 and M051. Skipped, Cancelled and Failed now precede the item name; an absent detail does not leave a trailing colon. Done and Original returned keep the optional explanation after a dash. Sentence punctuation does not duplicate an existing final full stop. The Resume notice is emitted only after dispatch accepts the request.

M031 now reads `{name}: Source kept: {detail}.` SourceRetained means the source file was kept, including declined Trash cases where no copy occurred. Without a detail, it reads `{name}: Source kept.` The detail separator is omitted when there is no detail, and an existing final full stop is not duplicated.

The Console now displays `HH:mm:ss module: message`, using a small formatter without severity labels or brackets. The diagnostic log uses Qt's `qSetMessagePattern` and `qFormatLogMessage` for `yyyy-MM-dd HH:mm:ss.zzz severity category: message`, with Qt's full severity names. Available warning/error source locations are still appended. Qt's application-wide message pattern belongs to the diagnostic log, so it does not change the Console presentation. Both outputs share the original message data; scanner batching and the Console's line limit are unchanged.

The 30-day ages are written directly at each use in the diagnostic log (including saved Console messages), crash-report collection and journal cleanup, with no shared retention constant. Crash reports are selected for copying by age; this does not delete old reports. Journal cleanup retains its recovery and Undo protections. The duplicate diagnostic-log path storage and redundant existence check before reading the log creation date were removed; the Console's session-only 2,000-line limit is separate from retention.
