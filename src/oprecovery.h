#pragma once

#include <QString>
#include <QStringList>

// MARK: - OpRecovery
//
// Crash recovery for the OpJournal write-ahead logs. Run once at launch.
// Per journal: a finished, cancelled, or already-recovered one gets deleted
// (unless it still holds a stranded parked original — a dirty fail — which
// keeps it alive); one whose owner is still alive is left for that live
// instance; an interrupted one (owner dead) is rolled back and stamped
// recovered.
//
// Rollback undoes ops newest-first. The rule that keeps it safe: a
// finished forward op leaves src gone, so never reverse onto a src that's
// still there (see OpJournal::scan). Filesystem-only, safe off-thread.
class OpRecovery
{
public:
	struct Summary
	{
		int journalsRecovered = 0; ///< Interrupted runs that put something back.
		int opsReversed = 0;	   ///< Files put back.
		int opsFlagged = 0;		   ///< Couldn't undo; needs the user to look.
		QStringList notes;		   ///< Lines ready to drop into a dialog.

		/// True when there's something worth telling the user about.
		bool anything() const { return journalsRecovered > 0 || opsFlagged > 0; }

		/// True when an op couldn't be undone, so the caller warns instead
		/// of showing a plain notice.
		bool hadTrouble() const { return opsFlagged > 0; }

		QString message() const { return notes.join(QLatin1Char('\n')); }
	};

	/// Scan `dir` (standard oplog dir when empty), roll back interrupted
	/// journals, delete the clean ones. Idempotent: if we crash again
	/// mid-rollback, re-running is safe.
	static Summary run(const QString &dir = QString());

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