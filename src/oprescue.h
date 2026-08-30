#pragma once

#include "fileidentity.h"
#include "opledger.h"
#include "oprequest.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

// MARK: - OpRescue
//
// Crash recovery for the OpLedger write-ahead ledgers. Run once at
// launch, off the GUI thread.
//
// The rule everything else follows: FINISHED WORK STAYS. A run that was
// cut off has usually finished most of its files, and undoing that at
// launch — only to offer to redo it — destroys hours of work nobody
// asked to undo. So the sweep tidies the one file that was in flight
// (removes a partial, puts a parked original back) and leaves the rest
// exactly as it is; what was not done is then offered to the user as a
// Resumable.
//
// What v2 adds on top of the v1 decision tables (kept nearly verbatim —
// they encode two years of reviewed edge cases):
//
//   Identity before reversal — a file about to be renamed back or
//   restored from the trash must still be the media the ledger
//   recorded (size + Avid UMID; see FileIdentity::verifyRelocated).
//   Mismatch = flag and explain, never rename.
//
//   Volume resolution — every ledgered path is resolved through the
//   run's recorded volume fingerprints first: same volume at the
//   recorded root → proceed; the volume found mounted under a NEW root
//   → re-anchor the paths there (narrated); nothing trustworthy →
//   leave the ledger for a later launch, and NEVER touch a different
//   volume squatting at the recorded address.
//
//   Retention — a finished, clean ledger is the undo candidate now, so
//   the sweep keeps the newest one (for up to 7 days) instead of
//   deleting every finished ledger on sight.
//
//   Undo runs — an interrupted undo is itself recovered: its ops are
//   already written in inverse orientation, so the sweep reverses them
//   with the machinery matching what the undo was DOING (its
//   `effective` kind), and finished-work-stays applies as everywhere.
//
//   Legacy — anything that is not schema 2 is invisible (OpLedger's
//   readers skip it) and stays untouched on disk, by decision.
class OpRescue
{
public:
	/// An interrupted run that wrote a plan and still has files it never
	/// finished. Everything a caller needs to hand the remainder back to
	/// the engine — or to delete the ledger if the user says no.
	struct Resumable
	{
		QString ledgerPath;
		OpKind kind = OpKind::Copy;
		QString dest;		   ///< Copy/Move destination root; empty otherwise.
		bool preserve = false;
		QString started;	   ///< ISO timestamp from the begin line, for the message.
		int total = 0;		   ///< Files in the plan.
		int finished = 0;	   ///< Concluded (done/skipped/evidenced) before the cut.
		/// Delete only: at least one finished file went to a per-volume
		/// MediaMuster Trash rather than the system one. The message
		/// must not send the user to a Trash that hasn't got their files.
		bool usedMediaMusterTrash = false;
		QVector<OpItem> remaining; ///< Dispatch these straight back to the engine.
	};

	struct Summary
	{
		int ledgersRecovered = 0; ///< Interrupted runs that put something back.
		int opsReversed = 0;	  ///< Files put back or partials cleaned up.
		int opsFlagged = 0;		  ///< Couldn't undo; needs the user to look.
		QStringList notes;		  ///< Lines ready to drop into a dialog.
		QVector<Resumable> resumable;

		bool anything() const
		{
			return ledgersRecovered > 0 || opsFlagged > 0 || !resumable.isEmpty();
		}
		bool hadTrouble() const { return opsFlagged > 0; }
		QString message() const { return notes.join(QLatin1Char('\n')); }
	};

	/// Scan `dir` (standard oplog dir when empty), roll back interrupted
	/// ledgers, prune the superseded ones, keep the undo candidate.
	/// Idempotent: a crash mid-rollback re-runs safely next launch.
	///
	/// `mountedOverride` replaces the live mounted-volume table — tests
	/// drive the re-anchor and impostor rules with hand-built tables.
	/// Empty (the default) means "capture the real one".
	static Summary run(const QString &dir = QString(),
					   const QVector<VolumeIdentity> &mountedOverride = {});

	/// Read-only listing of the interrupted runs that can be resumed
	/// right now — the same set run() reports, recomputed with no
	/// rollback and no deletion. Uses the same classifier as run(), so
	/// the two can't disagree.
	static QVector<Resumable> pending(const QString &dir = QString());

	/// The shared classifier. Empty optional when the record has no
	/// plan, has finished (end line), is an undo run, or has nothing
	/// left to do.
	static std::optional<Resumable> resumableFrom(const OpLedger::Record &rec);

	// MARK: - Volume resolution (public: tests drive it with tables)

	struct ResolvedPath
	{
		enum class State
		{
			Proceed,	///< The recorded volume is where it was; use the path.
			Reanchored, ///< Volume found under a new root; `path` is rewritten.
			Wait,		///< Volume absent (or an impostor at its address, with
						///< the real one nowhere) — touch nothing this launch.
		};
		State state = State::Proceed;
		QString path;
		QString note; ///< Plain-words narration for Reanchored / Wait.
	};

	/// Resolve one ledgered path through the run's recorded volume
	/// fingerprints against what is mounted NOW.
	static ResolvedPath resolvePath(const QString &ledgeredPath,
									const QVector<VolumeIdentity> &recorded,
									const QVector<VolumeIdentity> &mounted);

	/// Resolve EVERY path a record carries (ops, plan items, dest)
	/// through its recorded volume fingerprints, rewriting them in place
	/// when a volume moved. True = the record may be acted on; false =
	/// leave it entirely alone this launch (a volume is missing, or an
	/// impostor sits at its address). Shared by the sweep and by Undo,
	/// so the two can never disagree about a drive.
	static bool resolveRecord(OpLedger::Record &rec, const QVector<VolumeIdentity> &mounted,
							  QStringList &notes);

	/// The live mounted-volume table (one VolumeIdentity per mount).
	static QVector<VolumeIdentity> mountedVolumes();
};
