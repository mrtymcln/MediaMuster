# Interrupted original restoration and temporary cleanup

Repairs 3 and 4 address retained originals and working files from Copy/Move.

## Original restoration

If cancellation arrives while an original is in its private retirement folder,
the worker records restoration intent and returns the matching original to its
old location without overwriting anything. Completed destination copies remain.
Restoration ignores the already-set cancellation flag until that protective step
finishes. A distinct completed state prevents Resume from removing that original
again.

A blocked restoration keeps both its evidence and its actual retained path.
File > Restore Interrupted Originals remains available after dismissal, restart
and later jobs, independently of Undo. Its Restore/Close dialog lists current
recorded locations; multiple jobs can be selected individually. Restore requires
only the original storage, not a usable or connected destination copy. Changed
originals, occupied locations and unsafe paths are retained and reported.

The source and retirement folders are flushed before restoration completion is
recorded. Recovery recognises a rename that completed before its final append.
Successful restoration refreshes the media table by scanning the actual media
roots. A claimed Undo remains the sole recovery owner of its forward job; its
completed inverse evidence can settle the forward retirement-folder cleanup.

Resume and Restore remain distinct actions: Resume continues an unfinished job;
Restore returns stranded originals, including those from dismissed jobs, while
keeping completed copies. Both report an explicit `OriginalRestored` result so
the UI refreshes restored media regardless of which route performed the return.
The refresh preserves the scan roots already represented in the table.

## Temporary cleanup

New working directories are exclusively created with recorded identities before
payload creation or original retirement. On Mac, ownership, mode and extended
ACLs are checked; the directory must be private. Windows deletes only through
protected object handles. Partial-file deletion requires the recorded directory
and file identities, saved disposal intent and a revalidated surviving original
or completed copy.

Empty-directory removal is non-recursive and identity checked. Directory changes
must be confirmed before cleanup evidence is cleared. Publication, original
removal or restoration must be durably recorded before their necessary working
directories disappear. Cleanup can finish after a crash, including for completed
and dismissed jobs. Failed cleanup retains its paths and is reported.

Nothing is deleted by a folder-name search. Old partials without directory
identity evidence, unrecorded folders, changed objects and nonempty directories
are retained. A crash between directory creation and its identity append can
leave an unproven directory requiring manual inspection. Journal pruning keeps
pending cleanup and original-restoration evidence.

## C++ review

The follow-up review against the supplied C++ coding standards replaced manual
native directory/ACL cleanup with scoped guards, retaining the required handle
close order before directory persistence checks. Qt parent-owned widgets keep
their framework-managed ownership. Recovery journal write failures are reported
and stop further cleanup for the affected job. Undo settlement uses named
helpers and resolves its evidence once per forward job instead of once per file.

## Verification

Regression coverage includes cancellation before/after retirement, restoration
with changed/missing destinations, occupied originals, durable restoration crash
boundaries, dismissed/older jobs, Undo ownership, cleanup crashes, replacement
files, unexpected directory contents, permission failures and repeated recovery.

Mac validation on 16 September 2026: the application builds, its code signature
verifies, and all 27 suites pass. The affected suites contain 127 file-operation,
78 journal, 32 operation-UI and 16 native-cleanup passed cases. The native tests
include actual inherited ACL removal and refusal after an ACL broadens access.

Windows validation on 16 September 2026: the full application builds and all
27 suites pass in [GitHub Actions run 35077564288](https://github.com/mrtymcln/MediaMuster/actions/runs/35077564288),
at diagnostic commit `d4ead6e65071a945fa79f5f741677ecda2dc3292`.
The affected suites contain 131 file-operation, 78 journal, 32 operation-UI and
12 native-cleanup passed cases, with no skips or failures in those four suites.
The full Windows run has four unrelated conditional skips for external toolkit
fixtures, case-sensitive directories and a POSIX symlink fixture.

These checks use local Mac storage and a hosted Windows runner, with interrupted
operations and storage failures exercised by controlled test hooks. Actual
SMB/NEXIS hardware and an airgapped Avid system have not been tested directly.
