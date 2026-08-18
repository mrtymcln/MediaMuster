#pragma once

#include "opjournal.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

// MARK: - OpRecovery
//
// Crash recovery for the OpJournal write-ahead logs. Run once at launch.
//
// The rule everything else follows: FINISHED WORK STAYS. A run that was cut
// off has usually finished most of its files, and undoing that at launch —
// only to offer to redo it — destroys hours of work nobody asked to undo.
// So the sweep tidies the one file that was in flight (removes a partial,
// puts a parked original back) and leaves the rest exactly as it is; what
// was not done is then offered to the user as a Resumable.
//
// One case keeps the old wholesale rollback, because nothing can be
// offered from it: a journal whose plan line never reached disk (or that
// this build cannot fully read).
//
// Per journal: a finished or cancelled one gets deleted (unless it still
// holds a stranded parked original — a dirty fail — which keeps it alive);
// one whose owner is still alive is left for that live instance; an
// interrupted one (owner dead) is tidied and stamped recovered. A recovered
// journal that still has unfinished files STAYS on disk and is reported as
// resumable until the user resumes or discards it; one with nothing left to
// offer is deleted on the next sweep.
//
// What counts as "finished" is one predicate (opConcluded) shared by the
// sweep and the resume offer, so they can never disagree about a file: the
// op's own done/skip line, or the disk's evidence when the crash beat that
// line — a whole file in a Copy's empty destination slot, a Move whose
// source is gone and whose destination is whole, a Delete whose source is
// gone. Filesystem-only, safe off-thread.
//
// undo() is the opposite tool and keeps its own rule: it deliberately
// reverses a run that COMPLETED, so it unwinds every op, finished or not.
class OpRecovery
{
public:
	/// An interrupted run that wrote a plan (see OpJournal::writePlan) and
	/// still has files it never finished. Everything a caller needs to hand
	/// the remainder back to the engine — or to delete the journal if the
	/// user says no. `remaining` is every planned file that did not finish:
	/// never started, failed, or cut off mid-file (the sweep has already
	/// tidied that one).
	struct Resumable
	{
		QString journalPath;
		OpJournal::Kind kind = OpJournal::Kind::Copy;
		QString dest;		 ///< Copy/Move destination root; empty for Delete.
		bool preserve = false; ///< Copy/Move: mirror Avid MediaFiles/MXF/<n>.
		QString started;	 ///< ISO timestamp from the begin line, for the message.
		int total = 0;		 ///< Files in the plan.
		int finished = 0;	 ///< Marked done or skipped before the interruption.
		/// Delete only: at least one finished file went to a per-volume
		/// MediaMuster Trash rather than the system one (network volumes
		/// have no usable OS trash). The message must not send the user to
		/// a Trash that hasn't got their files.
		bool usedMediaMusterTrash = false;
		QVector<OpJournal::PlanItem> remaining;
	};

	struct Summary
	{
		int journalsRecovered = 0; ///< Interrupted runs that put something back.
		int opsReversed = 0;	   ///< Files put back or partials cleaned up.
		int opsFlagged = 0;		   ///< Couldn't undo; needs the user to look.
		QStringList notes;		   ///< Lines ready to drop into a dialog.
		QVector<Resumable> resumable; ///< Interrupted runs the user can finish.

		/// True when there's something worth telling the user about.
		bool anything() const
		{
			return journalsRecovered > 0 || opsFlagged > 0 || !resumable.isEmpty();
		}

		/// True when an op couldn't be undone, so the caller warns instead
		/// of showing a plain notice.
		bool hadTrouble() const { return opsFlagged > 0; }

		QString message() const { return notes.join(QLatin1Char('\n')); }
	};

	/// Scan `dir` (standard oplog dir when empty), roll back interrupted
	/// journals, delete the clean ones. Idempotent: if we crash again
	/// mid-rollback, re-running is safe.
	static Summary run(const QString &dir = QString());

	/// Read-only listing of the interrupted runs that can be resumed right
	/// now — the same set run() reports, recomputed from disk with no
	/// rollback and no deletion. For refreshing a menu item after the user
	/// resumed or discarded one. Uses the same classifier as run(), so the
	/// two can't disagree.
	static QVector<Resumable> pending(const QString &dir = QString());

	/// The plan is what makes a journal resumable; this is the shared
	/// classifier. Empty optional when the record has no plan, has finished
	/// (end line), or has nothing left to do.
	static std::optional<Resumable> resumableFrom(const OpJournal::Record &rec);

	/// Deliberately reverse one *completed* journal on the user's command —
	/// the "Undo last operation" path. Reuses run()'s per-op reversal, so it
	/// stays safe and idempotent: an op already back (src present) is a no-op,
	/// so a double undo can't clobber. Marks the journal recovered when done.
	/// Filesystem-only; safe off the GUI thread.
	///
	/// Not yet wired to a caller: the UI (journal retention + an Edit-menu
	/// Undo) is parked for post-beta, and cross-volume moves still need a
	/// copy-back path before undo covers them (deferred to v1.1/v1.2). The
	/// same-volume Move/Delete reversal it does today is covered by tests.
	static Summary undo(const QString &journalPath);
};