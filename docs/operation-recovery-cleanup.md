# Interrupted original restoration and temporary cleanup

This page describes restoration of retained originals and cleanup of private
Copy/Move working files. See [current behaviour](current-behaviour.md)
for the app's scanning and operation flow.

## Original restoration

If cancellation arrives while an original is in its private retirement folder,
the worker records restoration intent and returns the matching original to its
old location without overwriting anything. Completed destination copies remain.
Restoration ignores the already-set cancellation flag until that protective step
finishes. A distinct completed state prevents Resume from removing that original
again.

A blocked restoration keeps both its evidence and its actual retained path.
File > Unfinished Business… remains available after dismissal, restart
and later jobs, independently of Undo. Its unified recovery dialog lists current
recorded locations; multiple jobs can be selected individually. Restore requires
only the original storage, not a usable or connected destination copy. Changed
originals, occupied locations and unsafe paths are retained and reported.

The source and retirement folders are flushed before restoration completion is
recorded. Recovery recognises a rename that completed before its final append.
Successful restoration refreshes the media table by scanning the actual media
roots. A claimed Undo remains the sole recovery owner of its forward job; its
completed inverse evidence can settle the forward retirement-folder cleanup.

Resume and Restore Originals remain distinct actions in the same dialog:
Resume continues an unfinished job;
Restore returns stranded originals, including those from dismissed jobs, while
keeping completed copies. Both report an explicit `OriginalRestored` result so
the UI refreshes restored media regardless of which route performed the return.
The refresh preserves the scan roots already represented in the table.

The dialog merges each job's available choices into one entry and uses the same
button-and-description rows as Filter by Bin. Stop explicitly abandons
unfinished work while preserving completed results and restoration records.
Escape and window close dismiss the dialog without changing the job. There is
no footer button or default action; Enter leaves the dialog open. The dialog asks
"Resume the interrupted job?" before showing the job details. Files remaining
excludes resolved entries such as skipped files and originals already restored.
Started dates and job-selector dates use local time in `yyyy-MM-dd HH:mm`, matching
the main table. Stop mentions restoring files listed below only when the
selected job has recorded original locations to show.
Recorded original paths appear in a plain read-only list without a group heading.
If no selectable job remains, the controller returns without opening a dialog.
Before starting a new operation, its prerequisite job is selected first; choosing
Resume or Restore starts only that recovery, never the waiting new operation.
The File menu contains one recovery command, enabled whenever either kind of
unfinished work is available and the application is idle.

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
