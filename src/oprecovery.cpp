#include "oprecovery.h"

#include "avidlayout.h"
#include "formatutil.h"
#include "opjournal.h"
#include "probesweep.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
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

	// Where this platform mounts removable and network volumes, i.e. the
	// folder whose direct children are mount points. Overridable so tests
	// can exercise the guard below (they cannot create a real mount).
	QString mountRoot()
	{
		const QString override = qEnvironmentVariable("MEDIAMUSTER_MOUNT_ROOT");
		if (!override.isEmpty())
			return override.endsWith(QLatin1Char('/')) ? override : override + QLatin1Char('/');
#ifdef Q_OS_MAC
		return QStringLiteral("/Volumes/");
#else
		// Windows refuses to create anything on a drive letter that isn't
		// there, so mkpath fails harmlessly; nothing to guard.
		return {};
#endif
	}

	// Is this path waiting for a volume that isn't mounted? An unmounted
	// volume leaves its mount point simply absent, and creating that folder
	// is actively harmful: the real volume then mounts beside it under a
	// different name ("EDIT 1"), and everything the user knows about their
	// paths quietly stops being true. Recovery may recreate ordinary
	// missing folders — that is how it puts a file back into a folder the
	// user has since deleted — but never a mount point.
	bool pathAwaitsItsVolume(const QString &path)
	{
		const QString root = mountRoot();
		if (root.isEmpty() || !path.startsWith(root))
			return false;
		const int slash = path.indexOf(QLatin1Char('/'), root.size());
		const QString mountPoint = slash < 0 ? path : path.left(slash);
		return !QFileInfo::exists(mountPoint);
	}

	// Is the source's folder there to be looked at? Absence of a file only
	// proves the run removed it if the folder it lived in still exists; an
	// unmounted volume makes every path under it "missing" for a reason
	// that has nothing to do with us. Used as evidence only (opConcluded),
	// never to decide whether a restore may proceed.
	bool sourceLocationReachable(const OpJournal::Entry &op)
	{
		const QString parent = QFileInfo(op.src).absolutePath();
		return !parent.isEmpty() && QFileInfo::exists(parent);
	}

	// Does the destination hold a WHOLE file rather than a fragment?
	//
	// The live source is the authority whenever it can be read: sizes are
	// compared directly, so a source that grew or shrank after the scan is
	// judged as it is NOW. The journalled size is only the fallback for a
	// source that is gone (the Move case — a move removes its source once
	// the copy verified). Order matters: reading the stale journalled size
	// first would call a 2 GB fragment of a 5 GB file "whole" merely
	// because the scan had recorded 1 GB.
	bool dstHoldsWholeFile(const OpJournal::Entry &op)
	{
		const QFileInfo dstInfo(op.dst);
		if (!dstInfo.exists())
			return false;
		const QFileInfo srcInfo(op.src);
		if (srcInfo.exists())
			return dstInfo.size() == srcInfo.size();
		return op.bytes > 0 && dstInfo.size() >= op.bytes;
	}

	// An empty-slot Copy op (nothing parked) whose destination holds a whole
	// file. Only meaningful with no parked original: with one, dst may hold
	// the RESTORED original, whose size says nothing about the copy.
	bool emptySlotCopyLandedWhole(const OpJournal::Entry &op)
	{
		return op.parked.isEmpty() && dstHoldsWholeFile(op);
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
			// An interrupted cross-volume copy can leave a short dst. Delete
			// it and SAY SO: this is the one file the sweep takes off the
			// user's disk, and a silent removal is exactly what the partial
			// -copy fix set out to end.
			bool removedPartial = false;
			if (!op.completed && dstExists && !dstHoldsWholeFile(op))
			{
				const qint64 dstSize = QFileInfo(op.dst).size();
				const qint64 expected = QFileInfo(op.src).size();
				if (QFile::remove(op.dst))
				{
					notes << QStringLiteral("Removed a partial copy at %1 (%2 of %3) left by an "
											"interrupted run. The original at %4 is untouched.")
								 .arg(op.dst, Format::bytes(dstSize), Format::bytes(expected),
									  op.src);
					removedPartial = true;
				}
				else
				{
					notes << QStringLiteral("Couldn't remove the partial copy at %1 (%2 of %3) "
											"left by an interrupted run — delete it by hand "
											"before trying again.")
								 .arg(op.dst, Format::bytes(dstSize), Format::bytes(expected));
					return OpResult::Flagged;
				}
			}
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
				return (op.rollbackIncomplete || removedPartial) ? OpResult::Reversed
																 : OpResult::NothingToDo;
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
			return removedPartial ? OpResult::Reversed : OpResult::NothingToDo;
		}

		// src is gone — but if its VOLUME is the thing that's missing, do
		// nothing: the mkpath below would create a folder where the volume
		// should mount, which stops it mounting under its own name. The op
		// stays unfinished, so the run is still offered once the drive is
		// back.
		if (pathAwaitsItsVolume(op.src))
			return OpResult::NothingToDo;

		// Putting it back needs the file still sitting at dst.
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

	// Did this op's forward work actually conclude, even though no 'done'
	// line reached disk? (The line is written after the work, so a crash in
	// that gap leaves finished work looking unfinished.) The evidence is
	// per kind:
	//
	//   Copy   — an empty destination slot now holds a whole file.
	//   Move   — the source is gone AND the destination holds a whole file
	//            (a move only removes its source once the copy verified).
	//   Delete — the source is gone; that is the whole job.
	//
	// Both absence rules demand a reachable source location first, so an
	// unmounted volume reads as "don't know" rather than "done".
	//
	// A plain fail line beats any of this: it is the run's own statement
	// that disk is as if the op never happened, and a file the engine
	// refused to trust must go back on the list, not be reported finished.
	//
	// The launch sweep leaves concluded work alone and the resume offer
	// counts it finished, so both agree about every file. One benign
	// exception, deliberately left: a Copy-Replace whose park is recorded
	// but gone (committed, or restored after a failed copy — from here
	// they are indistinguishable) is never called concluded, so the sweep
	// keeps whatever is at dst and the offer still lists the file. Resume
	// then re-copies at most that one file. Wasting one copy is the cheap
	// mistake; calling an unverified file "finished" is the expensive one.
	bool opConcluded(OpJournal::Kind kind, const OpJournal::Entry &op)
	{
		if (op.completed)
			return true;
		if (op.failed && !op.rollbackIncomplete)
			return false;
		switch (kind)
		{
		case OpJournal::Kind::Copy:
			return emptySlotCopyLandedWhole(op);
		case OpJournal::Kind::Move:
			return sourceLocationReachable(op) && !QFile::exists(op.src) && dstHoldsWholeFile(op);
		case OpJournal::Kind::Delete:
			return sourceLocationReachable(op) && !QFile::exists(op.src);
		case OpJournal::Kind::Rebalance:
			// Never: a rebalance is always unwound (see run()), and its
			// pre-flight probes MUST be renamed home.
			return false;
		}
		return false;
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
	// A recorded park path that is GONE means the op already concluded one
	// way or the other before the crash — committed (dst is the new whole
	// file) or restored (dst is the user's ORIGINAL again, of whatever
	// size). Both keep dst; nothing about its size may be read into it.
	//
	// Only when the slot was genuinely empty (no park path at all) is the
	// question "whole file or fragment?" — and the journalled size plus the
	// live source answer it. A provably-short dst is a partial, and removing
	// it loses nothing because Copy never touched the source. Leaving it
	// would be worse than a guess: a truncated file under the real name
	// reads as media to Avid and as "already exists" to a later Skip-policy
	// run (seen 2026-08-16). A whole dst is the "committed, died before the
	// done line" case: a finished copy we keep.
	OpResult reverseCopy(const OpJournal::Entry &op, QStringList &notes)
	{
		if (op.skipped || op.completed)
			return OpResult::NothingToDo;
		// Plain fail: the rollback already ran; disk is as if the op never
		// happened. Dirty fail: the rollback stalled — fall through, because
		// the parked-original logic below is exactly the retry it needs.
		if (op.failed && !op.rollbackIncomplete)
			return OpResult::NothingToDo;

		if (!op.parked.isEmpty())
		{
			if (!QFile::exists(op.parked))
				return OpResult::NothingToDo; // committed or already restored
			QFile::remove(op.dst);
			if (!restoreParked(op, notes, true))
				return OpResult::Flagged;
			return OpResult::Reversed;
		}

		// Empty-slot copy. Nothing to put back; decide whether dst is whole.
		const QFileInfo dstInfo(op.dst);
		if (!dstInfo.exists() || emptySlotCopyLandedWhole(op))
			return OpResult::NothingToDo;
		const qint64 dstSize = dstInfo.size();
		const QFileInfo srcInfo(op.src);
		const qint64 srcSize = srcInfo.exists() ? srcInfo.size() : -1;
		// Unknowable when the journal has no size and the source is gone:
		// keep, as before, rather than guess.
		if (op.bytes <= 0 && srcSize < 0)
			return OpResult::NothingToDo;

		const QString expected = Format::bytes(op.bytes > 0 ? op.bytes : srcSize);
		if (!QFile::remove(op.dst))
		{
			notes << QStringLiteral("Couldn't remove the partial copy at %1 (%2 of %3) left by an "
									"interrupted run — delete it by hand before copying again.")
						 .arg(op.dst, Format::bytes(dstSize), expected);
			return OpResult::Flagged;
		}
		if (srcSize >= 0)
			notes << QStringLiteral("Removed a partial copy at %1 (%2 of %3) left by an "
									"interrupted run. The original at %4 is untouched — copy it "
									"again.")
						 .arg(op.dst, Format::bytes(dstSize), expected, op.src);
		else
			notes << QStringLiteral("Removed a partial copy at %1 (%2 of %3) left by an "
									"interrupted run. The source %4 was never touched but is not "
									"reachable right now — copy it again once it is.")
						 .arg(op.dst, Format::bytes(dstSize), expected, op.src);
		return OpResult::Reversed;
	}

	// Undo a Delete: move it from the trash (finalPath) back to src.
	OpResult reverseDelete(const OpJournal::Entry &op, QStringList &notes)
	{
		if (op.failed || op.skipped)
			return OpResult::NothingToDo;
		if (QFile::exists(op.src))
			return OpResult::NothingToDo; // already back, or never left

		// Same mount-point rule as reverseMoveLike.
		if (pathAwaitsItsVolume(op.src))
			return OpResult::NothingToDo;

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

// MARK: - Resumable classification

std::optional<OpRecovery::Resumable> OpRecovery::resumableFrom(const OpJournal::Record &rec)
{
	// Only an interrupted run is resumable: a finished or cancelled run
	// (end line) concluded on the user's watch, and a run with no plan
	// (older build) can't say what it meant to do. A journal this build
	// can't fully read (unknown kind, newer schema) is rolled back as best
	// it can be but never EXECUTED from.
	if (!rec.hasPlan || rec.complete || rec.plan.isEmpty())
		return std::nullopt;
	if (!rec.kindKnown || rec.schema != 1)
		return std::nullopt;
	// A Rebalance is never resumable, and the reader says so rather than
	// leaning on the writer never having written a plan: run() unwinds it
	// wholesale, so "what is left to do" is not a question this record can
	// answer. (Its plan would also be a folder redistribution computed for
	// a disk layout that no longer exists.)
	if (rec.kind == OpJournal::Kind::Rebalance)
		return std::nullopt;

	// A planned file is finished when its op says so (done or skipped) or
	// when the disk says so (opConcluded — the work landed but the 'done'
	// line didn't). The launch sweep leaves exactly those files alone, so
	// the offer and the sweep can never disagree about the same file.
	// Everything else is remaining: never started, failed, or cut off
	// mid-file (the sweep has already tidied that one up).
	QSet<QString> finished;
	bool sawMediaMusterTrash = false;
	for (const OpJournal::Entry &op : rec.ops)
	{
		if (!(op.skipped || opConcluded(rec.kind, op)))
			continue;
		finished.insert(op.src);
		// Where a finished delete actually landed, so the offer can name
		// the right bin (see Resumable::usedMediaMusterTrash).
		if (op.finalPath.contains(AvidLayout::kMediaMusterTrashDir))
			sawMediaMusterTrash = true;
	}

	Resumable r;
	r.journalPath = rec.path;
	r.kind = rec.kind;
	r.dest = rec.planDest;
	r.preserve = rec.planPreserve;
	r.started = rec.started;
	r.total = rec.plan.size();
	r.usedMediaMusterTrash = sawMediaMusterTrash;
	for (const OpJournal::PlanItem &it : rec.plan)
	{
		if (finished.contains(it.src))
			++r.finished;
		else
			r.remaining.append(it);
	}
	if (r.remaining.isEmpty())
		return std::nullopt;
	return r;
}

QVector<OpRecovery::Resumable> OpRecovery::pending(const QString &dir)
{
	QVector<Resumable> out;
	for (const OpJournal::Record &rec : OpJournal::scan(dir))
	{
		// Only journals the launch sweep has already rolled back and
		// stamped. run() stamps every interrupted journal it processes, so
		// an unstamped one either belongs to a live run (this instance's,
		// or another's) or has not been swept yet (another instance died
		// while this one ran) — and resuming it before its rollback could
		// dispatch over a stranded parked original and then delete the only
		// record of it. A stamped journal's owner was dead at sweep time by
		// definition, so no liveness check here (a reused pid must not hide
		// it); run() applies the same rule to stamped journals.
		if (!rec.recovered)
			continue;
		if (const auto r = resumableFrom(rec))
			out.append(*r);
	}
	return out;
}

// MARK: - Launch sweep

OpRecovery::Summary OpRecovery::run(const QString &dir)
{
	Summary sum;
	const QVector<OpJournal::Record> records = OpJournal::scan(dir);

	for (const OpJournal::Record &rec : records)
	{
		// Already rolled back on an earlier launch. If it still has files
		// the user never got to (a plan with remaining work), it stays on
		// disk and is offered again — the user said "not now" last time,
		// or nobody answered. Otherwise, or once finished with every
		// rollback intact, there is nothing left to do and it goes. A
		// finished-but-dirty journal stays — it holds the only record of a
		// parked original that never made it home.
		if (rec.recovered)
		{
			if (const auto r = resumableFrom(rec))
			{
				sum.resumable.append(*r);
				continue;
			}
			QFile::remove(rec.path);
			continue;
		}
		if (rec.complete && !rec.dirty)
		{
			QFile::remove(rec.path);
			continue;
		}

		// A live owner is a second instance mid-write, not a crash. Leave it.
		if (ownerStillAlive(rec.pid, rec.host))
			continue;

		// A finished-but-dirty run is NOT unwound — its completed ops are
		// work the user watched conclude, and only the ops whose rollback
		// stalled get retried.
		const bool dirtyOnly = rec.complete;

		// Finished work stays. The sweep tidies the ONE file that was in
		// flight when the run was cut off, and nothing else: undoing an
		// hour of completed moves at launch — only to offer to redo them —
		// is the opposite of help, and the user never asked for it.
		//
		// Two exceptions keep their old wholesale rollback:
		//   Rebalance — no plan, so it can never be resumed, and its
		//               pre-flight probes MUST be renamed home.
		//   A journal with no plan (the write failed before the plan line)
		//               — nothing can be offered from it, so putting the
		//               files back is the only help left.
		// The same gate resumableFrom() applies: a journal this build cannot
		// fully read (unknown kind, newer schema) is never offered, so it
		// must get the wholesale rollback instead of being left half-done
		// with no way to finish it.
		const bool keepFinishedWork = rec.hasPlan && rec.kindKnown && rec.schema == 1 &&
									  rec.kind != OpJournal::Kind::Rebalance;

		// Undo newest op first so any ordering dependency unwinds the way
		// it was built.
		int reversed = 0;
		int flagged = 0;
		QStringList journalNotes;
		for (int i = rec.ops.size() - 1; i >= 0; --i)
		{
			const OpJournal::Entry &op = rec.ops[i];
			if (dirtyOnly && !op.rollbackIncomplete)
				continue;

			if (keepFinishedWork && !op.rollbackIncomplete && opConcluded(rec.kind, op))
			{
				// The work stands. One loose end can outlive it: a Replace
				// parked the old destination aside and the crash beat the
				// commit that deletes it. Deleting the user's previous file
				// on an inference is not ours to do, so name it instead.
				if (!op.parked.isEmpty() && QFile::exists(op.parked))
				{
					journalNotes << strandedParkNote(op, rec.kind == OpJournal::Kind::Copy);
					++flagged;
				}
				continue;
			}

			const OpResult r = reverseOp(rec.kind, op, journalNotes);
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
		// so the next launch retries. Every step is idempotent. The journal
		// itself stays on disk: if it carries a plan with unfinished files
		// the user can still resume it (below), and either way the next
		// launch's sweep deletes it once there is nothing left to offer.
		OpJournal::markRecovered(rec.path, reversed, flagged);

		if (const auto r = resumableFrom(rec))
			sum.resumable.append(*r);

		// Only narrate journals whose rollback did something; a crash
		// before any op completed has nothing to put back (the resume
		// offer, if any, speaks for itself).
		if (reversed == 0 && flagged == 0)
			continue;

		++sum.journalsRecovered;
		sum.opsReversed += reversed;
		sum.opsFlagged += flagged;

		QString head = QStringLiteral("Tidied up after an interrupted %1 — %2 file(s) put back or "
									  "removed. Finished files were left alone.")
						   .arg(OpJournal::kindName(rec.kind))
						   .arg(reversed);
		if (flagged > 0)
			head += QLatin1Char(' ') + QStringLiteral("%1 need%2 a look.")
										   .arg(flagged)
										   .arg(flagged == 1 ? QString() : QStringLiteral("s"));
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