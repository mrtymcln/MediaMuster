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

## Verification

Regression coverage includes cancellation before/after retirement, restoration
with changed/missing destinations, occupied originals, durable restoration crash
boundaries, dismissed/older jobs, Undo ownership, cleanup crashes, replacement
files, unexpected directory contents, permission failures and repeated recovery.
Platform results are recorded after validation.
