# File Check and scanner retirement

## Approved separation — 7 November 2026

The user approved Bella remaining the scanner/parser engine, a separate File
Check component verifying operation inputs, and File Operations performing Copy,
Move, Delete, Undo and Rebalance with the existing journal/recovery machinery.
File Check may call Bella's format-reading helpers through the already-open
operation file. Preserve all existing checks, warning messages and operation
checkpoints, including saved-job identity requirements. Bin Filter's AVB reader,
reference matching and behavior are unchanged.

The user requested recording and implementing this separation, testing the
operations and recovery, then removing the retired scanner and all its current
project names/paths/references. Suggested names are File Check (used for now),
File Verifier and Operation Verifier. No new scan-time physical-ID capture,
content hashing, journal schema, or additional Undo header checks are approved.

## Compatibility finding and approved scope

The current Bella scan reader intentionally admits a narrower format/layout set
than the former operation reader. Its MXF entry requires the initial supported
closed/complete OP-Atom header; the operation decoder can examine additional
header copies. The Bella OMF scanner supports the agreed single Bento 1 / OMF 1
container; the operation decoder can also inspect OMF 2 and multiple embedded
containers. No newly surveyed real-file failure has been established here.

The existing operation check does not require a complete technical descriptor
merely to establish file identity, and can require every independently captured
master ID from a saved job. Reading only Bella's selected public scan report
would therefore change its qualification or results.

The retained decoding dependency inventory is approximately 1,993 MXF lines and
2,163 OMF/container lines across 13 files before pruning. An identity-only graph
walk can replace the full metadata projection/selection engine. Keeping these
pieces privately inside Bella preserves broad operation decoding but leaves a
second decoding path beside Bella's narrow scan readers. Generalizing the
existing Bella decoder for both uses avoids that second path, but is a larger
parser refactor and must demonstrate unchanged scan reports as well as unchanged
operation checks. The user subsequently chose neither broader path: match Bella's existing,
deliberately simplified format support. No broader operation-only decoders are
retained. File Check uses the current Bella mapping helpers through the already
open file. A required identity that cannot be established stops the affected
operation with the existing rescan message. This deliberately narrows former
operation layout compatibility; physical safeguards, identity comparison rules,
warning messages, saved-job requirements and checkpoints remain unchanged.
Existing explicit-empty versus absent saved receipts retain their meanings.
The path-based scan reports and warnings are unchanged.

The source baseline for comparison is revision b685605. No live user media will
be moved, copied, deleted or rebalanced for destructive verification; use
disposable files and isolated journals. Native Windows execution is unavailable
on this Mac and cannot be represented by Mac test results.

## Implementation and genuine-file verification

`src/filecheck.h/.cpp` extracts the existing operation source gate without
changing its order, comparison policy or messages. `OpRunner` still owns
execution and calls that gate at the same point. Native copy/publication/removal,
Trash, Undo and recovery checkpoints remain in place. `OpFile`, `OpCopier`,
`OpJournal`, `OperationRecovery`, native file primitives and the Bella operation
handoff retain their production behavior and saved format.

Bella's borrowed-file identity functions reuse its existing mapping helpers.
They preserve the caller's opened file and cursor, compare full canonical IDs
exactly, check every supplied saved master, and require no complete technical
metadata merely to establish an independent identity. Temporary ID sets do not
extend the public scan reports. Normal path-based reader reports remain intact.

The former app/scanner selection and presentation branches are removed. Bella
owns location admission/rescan helpers directly. One current table/CSV schema
remains; its precompute filter still names the selectable unknown category while
the corresponding table/CSV cell remains blank. Shared Compression lookups move
unchanged to `src/mediaengine/`. Bin Filter's reader, resolver, loader, dialog
and full-ID matching behavior remain unchanged.

Against frozen revision b685605, the read-only genuine-header comparison confirms
all **2,415 full file IDs**, all **2,415 captured master sets**, and all **2,413
CSV-selected file IDs**. Every borrowed file stays open and its cursor is
preserved. There are zero differences in this cohort. Each genuine header has
one applicable master; multi-master saved-job coverage remains authored testing.

The full app Cancel/rescan/export test on EDIT and Shared returns **2,413 rows**.
Its CSV is byte-for-byte identical before and after this change, with zero field
differences. The inventory used for selecting genuine samples contains 4,471
MXF/WAVE/AIFF paths, including unmanaged non-Avid audio; this is not the number of
rows in the managed scan. No genuine standalone video OMF was found here.

A disposable rehearsal using five copied genuine originals passes **39 checks,
zero failures/skips**: MXF/WAVE/AIFF Copy, Move, Delete, Undo and crash/recovery;
MXF Rebalance relocation and wrong-track refusal; saved master/location checks;
unreadable or different expected database IDs; and protected opened-file reads.
All five originals retain identical native identity, size, modification time and
SHA-256 after testing. No original user media or databases are changed.

The retired scan-only test suites and comparison tools are removed. Important
operation/UI/AVB tests are migrated to Bella values or explicit saved receipts.
The obsolete selection/evidence machinery and scan-result types are removed;
shared AVB reading facts, enum values and decoding behavior remain. The live
KelpieId exhaustion/no-reuse test moves intact to the table-model suite.

Final totals are recorded below. Scratch logs and JSON receipts are under
`/private/tmp/bella-retirement-20261107.8OgiYVNB`.

One historical saved-job qualification remains: Bella uses its already-agreed
canonical byte order for 32-byte big-endian OMF IDs. A job saved with the former
byte convention can refuse verification and require a new scan/job; no lenient
ID fallback is added. No such genuine header was established in this survey.

## Final retirement and checks

The obsolete scanner source tree, build targets, app selection, presentation
branches and comparison probes are removed. Seventeen retired scan/parser/
selection-only suites are removed; retained operation/UI tests are migrated.
Compression lookup files are moved without byte changes. Current source,
build definitions and documentation have no retired scanner name/path references;
dated decisions and findings remain as historical records. Git history is unchanged.

After source retirement, all **35 retained normal suites pass**: **2,240 passed,
zero failed, 54 skipped**. The skips are unavailable shared-fixture or opt-in
cases, not newly exercised verification. The four focused ASan/UBSan suites
(MXF, OMF, File Check and operations) pass **624 cases, zero failed, 43
fixture-dependent skips**, with no sanitizer findings. The genuine disposable
rehearsal and real scan comparison above run separately rather than claiming
those missing default fixtures were present.

The normal Mac `build/MediaMuster.app` is rebuilt as arm64 + x86_64 using C++17 /
Qt 6.5.3 and its existing signing identity; strict signature verification passes.
Tests execute on arm64. Windows/Actions source and CMake wiring are reviewed,
but native Windows build/execution is not performed on this Mac. No changes are
published or pushed.
