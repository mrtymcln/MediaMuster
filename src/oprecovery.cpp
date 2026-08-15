#include "oprecovery.h"

#include "opjournal.h"
#include "probesweep.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSysInfo>

#include <optional>

#if defined(Q_OS_UNIX)
#include <csignal>
#include <cerrno>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace
{
	enum class OpResult
	{
		Reversed,	 ///< Put a file back.
		NothingToDo, ///< Forward op never happened, or was already undone.
		Flagged		 ///< Couldn't undo it; the user needs to know.
	};

	// Is the journal's owner still running? Only meaningful on this host; a
	// journal from another machine isn't a live process here, so recovery can
	// take it. We bias toward 'alive': a wrong 'dead' would fight a running
	// instance, which we must never do, whilst a wrong 'alive' only delays
	// recovery. That's why EPERM (process exists, just not ours to signal)
	// counts as alive.
	bool ownerStillAlive(qint64 pid, const QString &host)
	{
		if (pid <= 0 || host != QSysInfo::machineHostName())
			return false;
#if defined(Q_OS_UNIX)
		if (::kill(static_cast<pid_t>(pid), 0) == 0)
			return true;
		return errno == EPERM;
#elif defined(Q_OS_WIN)
		HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
		if (!h)
			return false;
		DWORD code = 0;
		const bool alive = ::GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
		::CloseHandle(h);
		return alive;
#else
		return false;
#endif
	}

	// MARK: - Parked-original restore

	// The one user-facing sentence for a parked original that could not be
	// returned to its slot. One builder so the move and copy walks can't
	// drift apart on the app's most important recovery message.
	QString strandedParkNote(const OpJournal::Entry &op, bool afterCopy)
	{
		// %1 is the FULL destination path on purpose: this note is the
		// user's only surviving pointer once the journal is pruned, and
		// a bare filename can't be found across a 300-folder Nexis.
		return QStringLiteral("Couldn't put the original %1 back%2 — it's still in "
							  "the same folder, named '%3'.")
			.arg(op.dst,
				 afterCopy ? QStringLiteral(" after an interrupted copy") : QString(),
				 QFileInfo(op.parked).fileName());
	}

	/// Try to return the parked original to its slot; on failure append
	/// the stranded note. Owns ONLY the rename and the note — callers
	/// keep their own guards (slot-free checks, dst removal) and result
	/// mapping, which differ deliberately between the move and copy
	/// walks. A false return must never be swallowed: every walk ends
	/// with the journal deleted, taking the only record of the park path
	/// with it, so an unreported failure strands the user's original
	/// under a temp name forever.
	bool restoreParked(const OpJournal::Entry &op, QStringList &notes, bool afterCopy)
	{
		if (QFile::rename(op.parked, op.dst))
			return true;
		notes << strandedParkNote(op, afterCopy);
		return false;
	}

	// Undo a Move/Rebalance: move dst back to src, then restore any original
	// a Move-Replace had parked aside.
	OpResult reverseMoveLike(const OpJournal::Entry &op, QStringList &notes)
	{
		// A plain failed or skipped op never touched disk. A dirty fail
		// (rollbackIncomplete) is the exception: its rollback stalled with
		// the original still parked, and the whole point of the flag is that
		// recovery walks in here to finish the job.
		if ((op.failed && !op.rollbackIncomplete) || op.skipped)
			return OpResult::NothingToDo;

		const bool srcExists = QFile::exists(op.src);
		const bool dstExists = QFile::exists(op.dst);

		// src being there means the forward op didn't finish (or we already
		// reversed it on an earlier pass). Never overwrite a live src; clean up
		// what's safe and leave the rest.
		if (srcExists)
		{
			// An interrupted cross-volume copy can leave a short dst; delete it.
			if (!op.completed && dstExists && op.bytes > 0 && QFileInfo(op.dst).size() != op.bytes)
				QFile::remove(op.dst);
			// Move-Replace parked the old dst but never moved src in: put the
			// parked original back if its spot is free. Restored counts as a
			// reversal on a dirty fail (that op exists in the journal purely
			// so the parked original gets home); still stranded — dst
			// occupied by a full-size copy we must not destroy, or the
			// rename failed again — gets a note naming the park path so the
			// user can act even if every retry loses. A failed attempt flags
			// on clean ops too: silence here would strand the original with
			// no record once the journal is deleted.
			const bool parkedWaiting = !op.parked.isEmpty() && QFile::exists(op.parked);
			if (parkedWaiting && !QFile::exists(op.dst))
			{
				if (!restoreParked(op, notes, false))
					return OpResult::Flagged;
				return op.rollbackIncomplete ? OpResult::Reversed : OpResult::NothingToDo;
			}
			if (parkedWaiting)
			{
				// dst occupied (a full-size copy we must not destroy — e.g.
				// power loss during the verify pass of a cross-volume
				// Move-Replace): recovery can't restore, so it must at least
				// TELL. Dirty or clean makes no difference to the user; the
				// journal holding the park path is deleted either way.
				notes << strandedParkNote(op, false);
				return OpResult::Flagged;
			}
			return OpResult::NothingToDo;
		}

		// src is gone, so putting it back needs the file still sitting at dst.
		if (!dstExists)
		{
			notes << QStringLiteral("Couldn't put %1 back — neither the original nor the "
									"moved copy is there now.")
						 .arg(op.src);
			return OpResult::Flagged;
		}

		// An in-flight cross-volume op only counts if dst is full size; a short
		// dst is a partial copy we can't trust as the real file.
		if (!op.completed && op.bytes > 0 && QFileInfo(op.dst).size() != op.bytes)
		{
			notes << QStringLiteral("Left a partial copy of %1 where it was — the original "
									"was already gone, so nothing got overwritten.")
						 .arg(op.src);
			return OpResult::Flagged;
		}

		QDir().mkpath(QFileInfo(op.src).absolutePath());
		if (!QFile::rename(op.dst, op.src))
		{
			notes << QStringLiteral("Couldn't move %1 back to %2.").arg(op.dst, op.src);
			return OpResult::Flagged;
		}

		// Put the parked original back. A failed rename must flag, not count
		// as Reversed — markRecovered lands after this walk and the next
		// launch deletes the journal, the only record of the park path. This
		// was the one silent copy of the restore (its two siblings already
		// noted and flagged).
		if (!op.parked.isEmpty() && QFile::exists(op.parked) && !restoreParked(op, notes, false))
			return OpResult::Flagged;
		return OpResult::Reversed;
	}

	// Undo a Copy. Copy never removes src, so reverseMoveLike's "src gone ==
	// the forward op finished" rule says nothing here — src is always there.
	// The signal Copy leaves instead is `parked`: replacing a live destination
	// renames the original aside and only deletes it once the new file has
	// landed and verified. So parked still being on disk is exact proof the
	// copy never committed, and the destination slot is ours to roll back.
	//
	// Rollback (not roll-forward) is the only safe reading of that state: a
	// full-size dst with parked still alive is either mid-verify or a copy
	// whose verify just failed, and those are indistinguishable from here.
	//
	// With no parked original there is no safe move. Either the copy wrote
	// into an empty slot, or it committed and we died before the done line —
	// and in that second case dst is a finished, verified copy that deleting
	// would destroy with nothing to put back. A partial left in an empty slot
	// keeps the user's chosen name at the user's chosen path, so it stays for
	// them to deal with rather than being guessed at.
	OpResult reverseCopy(const OpJournal::Entry &op, QStringList &notes)
	{
		if (op.skipped || op.completed)
			return OpResult::NothingToDo;
		// Plain fail: the rollback already ran; disk is as if the op never
		// happened. Dirty fail: the rollback stalled — fall through, because
		// the parked-original logic below is exactly the retry it needs.
		if (op.failed && !op.rollbackIncomplete)
			return OpResult::NothingToDo;

		if (op.parked.isEmpty() || !QFile::exists(op.parked))
			return OpResult::NothingToDo;

		QFile::remove(op.dst);
		if (!restoreParked(op, notes, true))
			return OpResult::Flagged;
		return OpResult::Reversed;
	}

	// Undo a Delete: move it from the trash (finalPath) back to src.
	OpResult reverseDelete(const OpJournal::Entry &op, QStringList &notes)
	{
		if (op.failed || op.skipped)
			return OpResult::NothingToDo;
		if (QFile::exists(op.src))
			return OpResult::NothingToDo; // already back, or never left

		// We only know where it landed if the done line recorded the trash
		// path. No path (platform didn't report it, or we crashed first) or an
		// already-emptied trash means there's nothing to restore.
		if (op.finalPath.isEmpty() || !QFile::exists(op.finalPath))
		{
			notes << QStringLiteral("Couldn't restore %1 — it's no longer in the Trash.")
						 .arg(QFileInfo(op.src).fileName());
			return OpResult::Flagged;
		}

		QDir().mkpath(QFileInfo(op.src).absolutePath());
		if (!QFile::rename(op.finalPath, op.src))
		{
			notes << QStringLiteral("Couldn't restore %1 from the Trash.").arg(op.src);
			return OpResult::Flagged;
		}
		return OpResult::Reversed;
	}

	// One place to map a journal's Kind to its reverser, so the crash sweep
	// (run) and the user-initiated undo can't drift apart on the dispatch.
	// An unknown Kind does nothing rather than guessing: a wrong reverser is
	// how a recovery pass turns a crash into data loss.
	OpResult reverseOp(OpJournal::Kind kind, const OpJournal::Entry &op, QStringList &notes)
	{
		switch (kind)
		{
		case OpJournal::Kind::Delete:
			return reverseDelete(op, notes);
		case OpJournal::Kind::Copy:
			return reverseCopy(op, notes);
		case OpJournal::Kind::Move:
		case OpJournal::Kind::Rebalance:
			return reverseMoveLike(op, notes);
		}
		return OpResult::NothingToDo;
	}
} // namespace

OpRecovery::Summary OpRecovery::run(const QString &dir)
{
	Summary sum;
	const QVector<OpJournal::Record> records = OpJournal::scan(dir);

	for (const OpJournal::Record &rec : records)
	{
		// Already rolled back, or finished with every rollback intact:
		// nothing left to do. A finished-but-dirty journal stays — it holds
		// the only record of a parked original that never made it home.
		if (rec.recovered || (rec.complete && !rec.dirty))
		{
			QFile::remove(rec.path);
			continue;
		}

		// A live owner is a second instance mid-write, not a crash. Leave it.
		if (ownerStillAlive(rec.pid, rec.host))
			continue;

		// An interrupted run is reversed wholesale. A finished-but-dirty run
		// is NOT — its completed ops are work the user watched conclude, and
		// only the ops whose rollback stalled get retried.
		const bool dirtyOnly = rec.complete;

		// Undo newest op first so any ordering dependency unwinds the way
		// it was built.
		int reversed = 0;
		int flagged = 0;
		QStringList journalNotes;
		for (int i = rec.ops.size() - 1; i >= 0; --i)
		{
			if (dirtyOnly && !rec.ops[i].rollbackIncomplete)
				continue;
			const OpResult r = reverseOp(rec.kind, rec.ops[i], journalNotes);
			if (r == OpResult::Reversed)
				++reversed;
			else if (r == OpResult::Flagged)
				++flagged;
		}

		// A crashed rebalance may have stranded a probe file the journal
		// never heard about (older build, or the journal degraded before
		// the probe's op line landed). The begin line carries the MXF root,
		// so sweep it. Journalled probes were already renamed home by the
		// op walk above, leaving nothing here — every step idempotent.
		if (rec.kind == OpJournal::Kind::Rebalance)
		{
			const QString mxfRoot = rec.meta.value(QStringLiteral("mxfRoot")).toString();
			if (!mxfRoot.isEmpty())
			{
				const ProbeSweep::Result swept = ProbeSweep::recoverStranded(mxfRoot);
				reversed += swept.restored.size();
				for (const QString &name : swept.stuck)
				{
					journalNotes << QStringLiteral("A pre-flight rename is still stranded as "
												   "'%1' — its original name may be taken.")
										.arg(name);
					++flagged;
				}
			}
		}

		// Stamp it recovered last: a crash mid-rollback leaves it unstamped,
		// so the next launch retries. Every step is idempotent.
		OpJournal::markRecovered(rec.path, reversed, flagged);

		// Only surface journals that did something; a crash before any op
		// completed has nothing to report.
		if (reversed == 0 && flagged == 0)
			continue;

		++sum.journalsRecovered;
		sum.opsReversed += reversed;
		sum.opsFlagged += flagged;

		QString head = QStringLiteral("Recovered an interrupted %1 — %2 file(s) put back.")
						   .arg(OpJournal::kindName(rec.kind))
						   .arg(reversed);
		if (flagged > 0)
			head += QLatin1Char(' ') + QStringLiteral("%1 couldn't be undone.").arg(flagged);
		sum.notes << head;
		sum.notes += journalNotes;
	}

	return sum;
}

// MARK: - User-initiated undo

OpRecovery::Summary OpRecovery::undo(const QString &journalPath)
{
	Summary sum;

	const std::optional<OpJournal::Record> rec = OpJournal::readOne(journalPath);
	if (!rec)
		return sum;

	// Unlike run(), this is a deliberate reversal of a *completed* run, so we
	// don't gate on the end line or the owning pid. The per-op steps still read
	// disk state, so undo stays safe and idempotent: an op already back (src
	// present) is a no-op, so undoing twice can't clobber anything.
	int reversed = 0;
	int flagged = 0;
	QStringList notes;
	for (int i = rec->ops.size() - 1; i >= 0; --i)
	{
		const OpResult r = reverseOp(rec->kind, rec->ops[i], notes);
		if (r == OpResult::Reversed)
			++reversed;
		else if (r == OpResult::Flagged)
			++flagged;
	}

	// Spend the journal: run() deletes it at next launch, and a repeat undo has
	// nothing left to put back.
	OpJournal::markRecovered(journalPath, reversed, flagged);

	if (reversed == 0 && flagged == 0)
		return sum;

	++sum.journalsRecovered;
	sum.opsReversed = reversed;
	sum.opsFlagged = flagged;

	QString head = QStringLiteral("Undo complete — %1 file(s) put back.").arg(reversed);
	if (flagged > 0)
		head += QLatin1Char(' ') + QStringLiteral("%1 couldn't be undone.").arg(flagged);
	sum.notes << head;
	sum.notes += notes;

	return sum;
}