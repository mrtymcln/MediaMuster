#pragma once

#include "opjournal.h"
#include "oprunner.h"

#include <QString>

#include <atomic>

// MARK: - OpUndo
//
// Edit ▸ Undo for the file-operations engine: reverses the most recent
// completed run, using the journal that run left behind as the exact
// record of what to put back where.
//
// What qualifies is decided by OpJournal::latestUndoable (newest finished,
// clean, not-yet-undone journal) — and re-checked here at run time, since
// the menu's answer may be minutes old.
//
// The ground rules, same as everywhere in the engine:
//
//   IDENTITY BEFORE ACTION — every file about to be moved, trashed or
//   restored is verified against the identity the journal recorded for it
//   (size + Avid UMID for files that have legitimately moved; the full
//   filesystem identity for files expected to be untouched). A file that
//   doesn't match is refused with a plain sentence naming the clip;
//   the run continues with the rest.
//
//   TRASH, NEVER UNLINK — undoing a Copy moves the copy to the trash;
//   undoing a cross-volume Move moves the far copy to the trash after
//   the file is verifiably back home. Nothing is ever hard-deleted.
//
//   THE UNDO IS ITSELF A JOURNALED RUN — every inverse step is write-
//   ahead journaled (kind Undo, with `undoes` naming the original and
//   `originalKind` naming its kind), so a crash MID-UNDO is recovered by
//   the same launch sweep as any other run, with finished-work-stays.
//
//   ALREADY-UNDONE IS A SKIP, NOT AN ERROR — a cancelled or partially
//   failed undo can be run again; items whose inverse already holds
//   (file back home, copy already gone) skip quietly, so pressing Undo
//   twice converges instead of erroring.
//
// Per kind, the inverse of one completed op:
//
//   Copy    → verify the landed copy, move it to the trash; if the copy
//             had REPLACED a file (parkedFinal), restore that file from
//             the trash back into the slot. Replace round-trips.
//   Move    → same volume: verify and rename the file home. Across
//             volumes: verify the far copy against the journaled checksum,
//             copy it back with full verification and the Platter
//             barrier, then trash the far copy — at every instant at
//             least one complete verified copy exists. Then restore any
//             replaced original from the trash.
//   Delete  → verify the trash catch and rename it back to where it
//             lived (works for both the OS trash and _MediaMuster_Trash,
//             because the journal recorded the exact landing path).
//   Rename  → rename back and reset the Avid folder databases, exactly
//             as the forward run did.
//
// On a clean finish (nothing failed, not cancelled) the ORIGINAL journal
// is stamped `undone`, which takes it out of undo candidacy — single-
// level undo, no redo. A cancelled or partly-failed undo leaves the
// original unstamped so Undo can be pressed again to finish the job.
class OpUndo
{
public:
	/// `sink` receives progress/itemDone/log/trashUsed exactly like a
	/// forward run; `cancel` is polled between items and inside copies.
	OpUndo(OpSink &sink, const std::atomic<bool> &cancel);

	/// Reverse the run recorded at `originalJournalPath`. The undo's own
	/// journal is written into `journalDir` (standard journal dir when
	/// empty — tests pass a temp dir). `mountedOverride` substitutes the
	/// live volume table for tests, as in OpRescue::run.
	OpRunner::Totals run(const QString &originalJournalPath, const QString &journalDir = {},
						 const QVector<VolumeIdentity> &mountedOverride = {});

private:
	struct ItemOutcome
	{
		bool ok = false;
		bool skipped = false;
	};

	ItemOutcome undoCopyOp(const OpJournal::Entry &op, OpJournal &journal, class TrashRouter &router,
						   const QString &name, int index, int total);
	ItemOutcome undoMoveOp(const OpJournal::Entry &op, OpJournal &journal, class TrashRouter &router,
						   const QString &name, int index, int total);
	ItemOutcome undoDeleteOp(const OpJournal::Entry &op, OpJournal &journal, const QString &name);
	ItemOutcome undoRenameOp(const OpJournal::Entry &op, OpJournal &journal, const QString &name,
							 QSet<QString> &touchedFolders);

	/// Restore a replaced original from its trash address back into the
	/// now-free destination slot (the tail step of Copy and Move undo).
	/// True on success or nothing-to-do; false → the caller's item fails
	/// with `*why` filled in plain words.
	bool restoreReplacedOriginal(const OpJournal::Entry &op, QString *why);

	OpSink &m_sink;
	const std::atomic<bool> &m_cancel;
	OpCopier m_copier;
};
