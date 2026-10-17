# Disposable file-operation verification — 18 October 2026

The user approved checking the retained file operations with disposable media,
including Undo after restarting and interrupted-job recovery. This checkpoint
validates the existing implementation. It adds no application code or permanent
tests.

## Completed file-level checks

All 14 core scenarios passed. Each starts with copies of a genuine MXF in a
temporary managed-media layout, scans through the current `ScanEngine`, formats
the compact rows through the production adapter, and releases the temporary scan
data before performing the operation. The original fixture's SHA-256 is unchanged.

| Completed operation | Confirmed behavior | Fresh-process Undo |
| --- | --- | --- |
| Copy | Both original and destination contain the original bytes. | Passed |
| Move within one filesystem | Real relocation moves the file and removes its former location. | Passed |
| Move using copy then removal | The forced copy/remove route produces the correct destination and removes the source. | Passed |
| Delete to macOS Trash | The real system Trash provider handles the disposable file. | Passed |
| Delete to MediaMuster Trash | The locally forced network-routing branch keeps the disposable file in MediaMuster Trash. | Passed |
| Rebalance | The real planner groups two same-master physical copies in different numbered folders and plans one relocation. | Passed |

Each Undo runs in a separate executable process, reads its saved journal, finds
the latest eligible job and returns the file to its original location. Restored
content matches SHA-256 exactly. Rebalance's other file and unrelated sentinels
remain unchanged. Final histories have no unfinished jobs, retained originals
awaiting restoration or further Undo candidate. Journal records remain valid.

## Actual process interruptions

Eight additional scenarios end their operation process with `std::_Exit(81)` at
an existing test checkpoint, without unwinding C++ objects. Recovery and Undo
run in separate processes.

| Interrupted operation | Stop point | Next process |
| --- | --- | --- |
| Copy | Temporary copy ready, before publication | Recover and Resume |
| Copy | Destination published | Recover the completed copy |
| Copy/remove Move | Temporary copy ready | Recover and Resume |
| Copy/remove Move | Original moved to its private retirement location | Recover and Resume |
| Copy/remove Move | Original moved to its private retirement location | Restore Originals |
| Same-filesystem Move | Relocation intent saved | Recover and Resume |
| MediaMuster Trash Delete | Relocation intent saved | Recover and Resume |
| Rebalance | Relocation intent saved | Recover and Resume |

All eight then pass Undo in another fresh process, exact original-content and
location checks, sentinel checks and final-history checks. The six completed
routes and eight interruption cases use 36 distinct stage processes.

## Native window checks

The Cocoa window probe also passes Copy, Move and native Trash Delete through
the actual Manage Media dialog, controller, progress and table model. Each
completed operation is followed by another process invoking the actual Undo
menu and confirmation dialog. Copy adds a destination row, Move updates its
location, Delete removes the row, and Undo leaves one original row at the
original path with the exact original bytes.

A ninth deliberately terminated operation, a Copy stopped before publication,
is presented by the real startup recovery handler. The probe clicks Resume in
its unfinished-job dialog, confirms both files and two model rows, then invokes
Undo and confirms one original row and no unfinished job or further Undo.

Eight successful native phase processes supply 170 probe assertions, including
Undo/cleanup of the initial Copy described below. Process IDs distinguish each
forward/Undo pair and the original crash from its recovery. Copy Undo's own
discarded native Trash copies are collected back into scratch storage using
their exact journal receipts; their native Trash paths are checked absent.
The Delete test restores its own native Trash item. No unrelated Trash items
are changed.

These are automated interactions with real native Qt windows, using production
objects linked into a separate test executable. `MainWindow::UiOnly` avoids
ordinary drive discovery and the normal application history. The probe calls
the production startup recovery handler explicitly; it is not a full normal
application-startup or human usability acceptance run. Rebalance is covered by
the core planner/operation checks and existing UI regression suite, rather than
an additional Cocoa Rebalance-dialog walkthrough.

One existing behavior surfaced: successful ordinary Copy and Move supply no
per-file Console completion line when their result message is empty. The files,
model changes and Undo history are correct. Delete, Undo and recovery messages
are present. The initial native Copy probe incorrectly required such a success
line; that failed probe receipt is preserved, its completed Copy is successfully
undone, and a fresh Copy scenario passes the corrected expectations. No product
behavior is changed to satisfy the probe.

## Existing regression tests

The Release arm64 app and configured targets are current (`cmake --build
build-mediaengine-release --parallel 4`: no rebuild needed). Five relevant suites
pass in 39.07 seconds, with no failures or skips:

| Suite | QtTest passes, including setup/cleanup |
| --- | ---: |
| `tst_fileoperations` | 172 |
| `tst_opjournal` | 86 |
| `tst_opcleanup` | 17 |
| `tst_rebalanceplanner` | 40 |
| `tst_operationui` | 93 |

These existing suites supply the wider mixed-format, changed-source, collision,
cancellation, failed-transfer, restoration, UI gating and journal protection
coverage. The standalone checks above add genuine scan-to-operation results and
actual fresh-process operation/Undo/recovery cycles.

## Build and scope

This is the current working tree based on
`b60453f50eb9271cc47b89d484a7f25db336d22e`, including the earlier uncommitted
unmatched-bin Console notice. The environment is macOS 15.8.2 (24H225), arm64,
Apple Clang 17.0.0, C++17 and pinned Qt 6.5.3, with the existing Release build.
Each scenario has its own temporary journal directory; ordinary application
history is outside the test setup.

The core seed is the repository's 8,909,409-byte genuine MXF
`tests/fixtures/TONE_100A01.EA7D504A.611740.mxf`, SHA-256
`e545ab93dbf5227813b7cbb7ce2db50e9ff5660d2273cb7875e4dbe03071fe2b`.
Its original content is read and copied, never used as a mutation operand.

The native window seed is a separate 8,917,601-byte complete MXF copied during
the earlier local workflow comparison, `TONE_100A01.1DAFE820.612550.mxf`, SHA-256
`7c87cdd3d58019eef319b201ef918f9c4e6ad29d1a55e8858e50018b2e5c2421`.
Its content also remains unchanged.

All operations here use local APFS storage. Forcing copy/remove and network-Trash
routing on that storage checks those code branches; it does not validate an
actual second filesystem, Windows, SMB or NEXIS. Controlled process exit does
not reproduce power loss or hardware write failures. Existing regression tests
cover other formats; these standalone positive workflows use MXF. This is a
correctness check, with no measured speed or memory improvement claimed.

Raw results, source fingerprints, probe sources, commands and test logs are saved
in [the evidence receipt](evidence/file-operation-acceptance-2026-10-18.json).
Scratch drivers and media remain under
`/private/tmp/copland-file-operations-20261018/`.
