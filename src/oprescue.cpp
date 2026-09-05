#include "oprescue.h"

#include "conventions.h"
#include "formatutil.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStorageInfo>
#include <QSysInfo>

#include <optional>

#if defined(Q_OS_UNIX)
#include <cerrno>
#include <csignal>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace
{
	// How long a finished journal may sit as the undo candidate before
	// the sweep ages it out — a machine where MediaMuster crashed and
	// was never used again shouldn't hoard journals forever.
	constexpr qint64 kUndoCandidateMaxAgeDays = 7;

	enum class OpResult
	{
		Reversed,	 ///< Put a file back.
		NothingToDo, ///< Forward op never happened, or was already undone.
		Flagged		 ///< Couldn't undo it; the user needs to know.
	};

	// Is the journal's owner still running? Only meaningful on this host;
	// a journal from another machine isn't a live process here. We bias
	// toward 'alive': a wrong 'dead' would fight a running instance,
	// which we must never do, whilst a wrong 'alive' only delays
	// recovery. That's why EPERM (process exists, just not ours to
	// signal) counts as alive.
	bool ownerStillAlive(qint64 pid, const QString &host)
	{
		if (pid <= 0 || host != QSysInfo::machineHostName())
			return false;
#if defined(Q_OS_UNIX)
		if (::kill(static_cast<pid_t>(pid), 0) == 0)
			return true;
		return errno == EPERM;
#elif defined(Q_OS_WIN)
		HANDLE h =
			::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
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

	// MARK: - Identity guards
	//
	// v2's addition to every reversal: before a file is renamed home or
	// restored, it must still be the MEDIA the journal recorded. The
	// relocated comparison (size + Avid UMID) is used because the file
	// has legitimately moved since capture. An op journaled without an
	// identity (hand-written test journals, non-identity paths) simply
	// has no check to apply — v1 behaviour.

	bool identityBlocks(const QString &path, const FileIdentity &expected)
	{
		if (expected.confidence == FileIdentity::Confidence::Low)
			return false;
		return FileIdentity::verifyRelocated(path, expected) != FileIdentity::Verdict::Match;
	}

	// MARK: - Parked-original restore

	// The one user-facing sentence for a parked original that could not
	// be returned to its slot. One builder so the walks can't drift
	// apart on the app's most important recovery message.
	QString strandedParkNote(const OpJournal::Entry &op, bool afterCopy)
	{
		// %1 is the FULL destination path on purpose: this note is the
		// user's only surviving pointer once the journal is pruned, and a
		// bare filename can't be found across a 300-folder Nexis.
		return QStringLiteral("Couldn't put the original %1 back%2 — it's still in "
							  "the same folder, named '%3'.")
			.arg(op.dst, afterCopy ? QStringLiteral(" after an interrupted copy") : QString(),
				 QFileInfo(op.parked).fileName());
	}

	/// Try to return the parked original to its slot; on failure append
	/// the stranded note. Owns ONLY the identity check, the rename and
	/// the note — callers keep their own guards (slot-free checks, dst
	/// removal) and result mapping. A false return must never be
	/// swallowed: an unreported failure strands the user's original
	/// under a temp name with no surviving record.
	bool restoreParked(const OpJournal::Entry &op, QStringList &notes, bool afterCopy)
	{
		// The file at the park path must still be the file that was
		// parked — the journal recorded its identity for exactly this.
		if (identityBlocks(op.parked, op.parkedOriginalId))
		{
			notes << QStringLiteral("Couldn't put the original %1 back — the parked file at "
									"'%2' no longer matches what was set aside.")
						 .arg(op.dst, QFileInfo(op.parked).fileName());
			return false;
		}
		if (QFile::rename(op.parked, op.dst))
			return true;
		notes << strandedParkNote(op, afterCopy);
		return false;
	}

	// Where this platform mounts removable and network volumes.
	// Overridable so tests can exercise the guard below.
	QString mountRoot()
	{
		const QString override = qEnvironmentVariable("MEDIAMUSTER_MOUNT_ROOT");
		if (!override.isEmpty())
			return override.endsWith(QLatin1Char('/')) ? override : override + QLatin1Char('/');
#ifdef Q_OS_MAC
		return QStringLiteral("/Volumes/");
#else
		// Windows refuses to create anything on a drive letter that
		// isn't there, so mkpath fails harmlessly; nothing to guard.
		return {};
#endif
	}

	// Is this path waiting for a volume that isn't mounted? An unmounted
	// volume leaves its mount point simply absent, and creating that
	// folder is actively harmful: the real volume then mounts beside it
	// under a different name ("EDIT 1"), and everything the user knows
	// about their paths quietly stops being true. Recovery may recreate
	// ordinary missing folders — that is how it puts a file back into a
	// folder the user has since deleted — but never a mount point.
	bool pathAwaitsItsVolume(const QString &path)
	{
		const QString root = mountRoot();
		if (root.isEmpty() || !path.startsWith(root))
			return false;
		const int slash = path.indexOf(QLatin1Char('/'), root.size());
		const QString mountPoint = slash < 0 ? path : path.left(slash);
		return !QFileInfo::exists(mountPoint);
	}

	// Is the source's folder there to be looked at? Absence of a file
	// only proves the run removed it if the folder it lived in still
	// exists. Used as evidence only, never to decide whether a restore
	// may proceed.
	bool sourceLocationReachable(const OpJournal::Entry &op)
	{
		const QString parent = QFileInfo(op.src).absolutePath();
		return !parent.isEmpty() && QFileInfo::exists(parent);
	}

	// Is the file at dst safe to remove as OUR partial? The engine's own
	// partial is a PREFIX of the source, so an MXF partial big enough to
	// parse carries the source's own UMID in its header. A parseable file
	// whose UMID DIFFERS is another program's media that landed at this
	// path after the crash — never ours to delete (the racer variant of
	// review finding 3, met at recovery time instead of copy time).
	// Unparseable (tiny partial, non-MXF) or no recorded content id → no
	// evidence either way → treated as ours, as v1 did.
	bool partialLooksLikeOurs(const OpJournal::Entry &op)
	{
		if (op.srcId.contentUmid.isEmpty())
			return true;
		const FileIdentity dstNow = FileIdentity::capture(op.dst);
		return dstNow.contentUmid.isEmpty() || dstNow.contentUmid == op.srcId.contentUmid;
	}

	// Does the destination hold a WHOLE file rather than a fragment?
	// The live source is the authority whenever it can be read; the
	// journaled size is only the fallback for a source that is gone.
	// Order matters: reading the stale journaled size first would call a
	// 2 GB fragment of a grown 5 GB file "whole".
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

	// An empty-slot Copy op (nothing parked) whose destination holds a
	// whole file. Only meaningful with no parked original: with one,
	// dst may hold the RESTORED original, whose size says nothing.
	bool emptySlotCopyLandedWhole(const OpJournal::Entry &op)
	{
		return op.parked.isEmpty() && dstHoldsWholeFile(op);
	}

	// MARK: - Reversers (the v1 decision tables, with identity guards)

	// Undo a Move-like op (Move, Rename, and an interrupted undo whose
	// original run was moving files): move dst back to src, then
	// restore any original a Replace had parked aside.
	OpResult reverseMoveLike(const OpJournal::Entry &op, QStringList &notes)
	{
		// A plain failed or skipped op never touched disk. A dirty fail
		// is the exception: its rollback stalled with the original still
		// parked, and the whole point of the flag is that recovery walks
		// in here to finish the job.
		if ((op.failed && !op.rollbackIncomplete) || op.skipped)
			return OpResult::NothingToDo;

		const bool srcExists = QFile::exists(op.src);
		const bool dstExists = QFile::exists(op.dst);

		// src being there means the forward op didn't finish (or we
		// already reversed it). Never overwrite a live src; clean up
		// what's safe and leave the rest.
		if (srcExists)
		{
			// An interrupted cross-volume copy can leave a short dst.
			// Delete it and SAY SO: this is the one file the sweep takes
			// off the user's disk — which is why TWO identity guards run
			// before it (review finding 2 and the finding-3 racer).
			bool removedPartial = false;
			if (!op.completed && dstExists && !dstHoldsWholeFile(op))
			{
				// Guard 1 — the restored original. A rollback that stalled
				// (dirty flag) but then completed leaves the REPLACED
				// ORIGINAL back at dst: a different size from src, so the
				// size test above reads it as "partial". If the file at
				// dst matches the identity of the file this op parked,
				// the rollback is already complete — keep it.
				if (op.parkedOriginalId.confidence != FileIdentity::Confidence::Low &&
					FileIdentity::verifyRelocated(op.dst, op.parkedOriginalId) ==
						FileIdentity::Verdict::Match)
				{
					notes << QStringLiteral("The file this run had set aside at %1 is already "
											"back in place; nothing needed doing.")
								 .arg(op.dst);
					return OpResult::NothingToDo;
				}
				// Guard 2 — a stranger's file (see partialLooksLikeOurs).
				if (!partialLooksLikeOurs(op))
				{
					notes << QStringLiteral(
								 "A file at %1 doesn't match this run's records — it was left "
								 "untouched. Rescan to see what's there.")
								 .arg(op.dst);
					return OpResult::Flagged;
				}
				const qint64 dstSize = QFileInfo(op.dst).size();
				const qint64 expected = QFileInfo(op.src).size();
				if (QFile::remove(op.dst))
				{
					notes << QStringLiteral(
								 "Removed a partial copy at %1 (%2 of %3) left by an "
								 "interrupted run. The original at %4 is untouched.")
								 .arg(op.dst, Format::bytes(dstSize), Format::bytes(expected),
									  op.src);
					removedPartial = true;
				}
				else
				{
					notes << QStringLiteral(
								 "Couldn't remove the partial copy at %1 (%2 of %3) left by "
								 "an interrupted run — delete it by hand before trying again.")
								 .arg(op.dst, Format::bytes(dstSize), Format::bytes(expected));
					return OpResult::Flagged;
				}
			}
			// Move-Replace parked the old dst but never moved src in:
			// put the parked original back if its spot is free.
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
				// dst occupied (a full-size copy we must not destroy):
				// recovery can't restore, so it must at least TELL.
				notes << strandedParkNote(op, false);
				return OpResult::Flagged;
			}
			return removedPartial ? OpResult::Reversed : OpResult::NothingToDo;
		}

		// src is gone — but if its VOLUME is the thing that's missing,
		// do nothing: the mkpath below would create a folder where the
		// volume should mount. The op stays unfinished, so the run is
		// still offered once the drive is back.
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

		// An in-flight cross-volume op only counts if dst is full size;
		// a short dst is a partial copy we can't trust as the real file.
		if (!op.completed && op.bytes > 0 && QFileInfo(op.dst).size() != op.bytes)
		{
			notes << QStringLiteral("Left a partial copy of %1 where it was — the original "
									"was already gone, so nothing got overwritten.")
						 .arg(op.src);
			return OpResult::Flagged;
		}

		// v2: the file about to be renamed home must still be the media
		// the journal recorded — never rename a stranger into the user's
		// folder.
		if (identityBlocks(op.dst, op.srcId))
		{
			notes << QStringLiteral("Couldn't put %1 back — the file now at %2 doesn't match "
									"the one this operation recorded, so it was left alone.")
						 .arg(op.src, op.dst);
			return OpResult::Flagged;
		}

		QDir().mkpath(QFileInfo(op.src).absolutePath());
		if (!QFile::rename(op.dst, op.src))
		{
			notes << QStringLiteral("Couldn't move %1 back to %2.").arg(op.dst, op.src);
			return OpResult::Flagged;
		}

		// Put the parked original back. A failed restore must flag, not
		// count as Reversed — the journal (the only record of the park
		// path) is deleted after recovery concludes.
		if (!op.parked.isEmpty() && QFile::exists(op.parked) &&
			!restoreParked(op, notes, false))
			return OpResult::Flagged;
		return OpResult::Reversed;
	}

	// Did this op's forward work actually conclude, even though no
	// 'done' line reached disk? (The line is written after the work, so
	// a crash in that gap leaves finished work looking unfinished.)
	//
	// A plain fail line beats any of this: it is the run's own statement
	// that disk is as if the op never happened, and a file the engine
	// refused to trust must go back on the list, not be reported
	// finished.
	//
	// One benign exception, deliberately kept from v1: a Copy-Replace
	// whose park is recorded but gone (committed or restored — from
	// here indistinguishable) is never called concluded; resume then
	// re-copies at most that one file. Wasting one copy is the cheap
	// mistake; calling an unverified file "finished" is the expensive
	// one.
	bool opConcluded(OpKind kind, const OpJournal::Entry &op)
	{
		if (op.completed)
			return true;
		if (op.failed && !op.rollbackIncomplete)
			return false;
		switch (kind)
		{
		case OpKind::Copy:
			// A DIRTY copy is one whose rollback stalled: the destination
			// holds an unfinished write the engine could not remove. It is
			// never finished work, whatever size the fragment came out —
			// a copy that failed verification is exactly the same size as
			// its source, and calling that "concluded" would drop the file
			// from the resume offer without ever having copied it.
			return !op.rollbackIncomplete && emptySlotCopyLandedWhole(op);
		case OpKind::Move:
		case OpKind::Rename:
			return sourceLocationReachable(op) && !QFile::exists(op.src) &&
				   dstHoldsWholeFile(op);
		case OpKind::Delete:
			return sourceLocationReachable(op) && !QFile::exists(op.src);
		case OpKind::Undo:
			// Undo records are reversed under the original run's kind and
			// never reach here with Undo itself.
			return false;
		}
		return false;
	}

	// Undo a Copy. Copy never removes src, so the "src gone == finished"
	// rule says nothing here. The signal Copy leaves instead is
	// `parked`: it still being on disk is exact proof the copy never
	// committed, and the destination slot is ours to roll back.
	OpResult reverseCopy(const OpJournal::Entry &op, QStringList &notes)
	{
		if (op.skipped || op.completed)
			return OpResult::NothingToDo;
		// Plain fail: the rollback already ran. Dirty fail: the rollback
		// stalled — fall through, the parked-original logic below is
		// exactly the retry it needs.
		if (op.failed && !op.rollbackIncomplete)
			return OpResult::NothingToDo;

		if (!op.parked.isEmpty())
		{
			if (!QFile::exists(op.parked))
				return OpResult::NothingToDo; // committed or already restored
			// The discard before the restore may only hit OUR partial —
			// a file another program landed at this path since the crash
			// is not ours to delete (finding-3 racer, recovery edition).
			if (QFile::exists(op.dst) && !partialLooksLikeOurs(op))
			{
				notes << QStringLiteral("A file at %1 doesn't match this run's records — it was "
										"left untouched, and the file set aside as '%2' was NOT "
										"restored over it. Rescan to see what's there.")
							 .arg(op.dst, QFileInfo(op.parked).fileName());
				return OpResult::Flagged;
			}
			QFile::remove(op.dst);
			if (!restoreParked(op, notes, true))
				return OpResult::Flagged;
			return OpResult::Reversed;
		}

		// Empty-slot copy. Nothing to put back; decide whether dst is
		// whole. A truncated file under the real name reads as media to
		// Avid and as "already exists" to a later Skip-policy run.
		const QFileInfo dstInfo(op.dst);
		if (!dstInfo.exists() || emptySlotCopyLandedWhole(op))
			return OpResult::NothingToDo;
		const qint64 dstSize = dstInfo.size();
		const QFileInfo srcInfo(op.src);
		const qint64 srcSize = srcInfo.exists() ? srcInfo.size() : -1;
		// Unknowable when the journal has no size and the source is gone:
		// keep rather than guess.
		if (op.bytes <= 0 && srcSize < 0)
			return OpResult::NothingToDo;

		// The stranger guard, one last time: an empty-slot copy's short dst
		// is only removable when nothing says it is somebody else's file.
		if (!partialLooksLikeOurs(op))
		{
			notes << QStringLiteral("A file at %1 doesn't match this run's records — it was "
									"left untouched. Rescan to see what's there.")
						 .arg(op.dst);
			return OpResult::Flagged;
		}

		const QString expected = Format::bytes(op.bytes > 0 ? op.bytes : srcSize);
		if (!QFile::remove(op.dst))
		{
			notes << QStringLiteral("Couldn't remove the partial copy at %1 (%2 of %3) left by "
									"an interrupted run — delete it by hand before copying "
									"again.")
						 .arg(op.dst, Format::bytes(dstSize), expected);
			return OpResult::Flagged;
		}
		if (srcSize >= 0)
			notes << QStringLiteral("Removed a partial copy at %1 (%2 of %3) left by an "
									"interrupted run. The original at %4 is untouched — copy "
									"it again.")
						 .arg(op.dst, Format::bytes(dstSize), expected, op.src);
		else
			notes << QStringLiteral("Removed a partial copy at %1 (%2 of %3) left by an "
									"interrupted run. The source %4 was never touched but is "
									"not reachable right now — copy it again once it is.")
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

		if (pathAwaitsItsVolume(op.src))
			return OpResult::NothingToDo;

		// We only know where it landed if the done line recorded the
		// trash path. No path, or an already-emptied trash, means
		// there's nothing to restore.
		if (op.finalPath.isEmpty() || !QFile::exists(op.finalPath))
		{
			notes << QStringLiteral("Couldn't restore %1 — it's no longer in the Trash.")
						 .arg(QFileInfo(op.src).fileName());
			return OpResult::Flagged;
		}

		// v2: the catch in the trash must still be the media that was
		// deleted — someone emptying and refilling a trash path is
		// exactly the confusion identity exists to catch.
		if (identityBlocks(op.finalPath, op.srcId))
		{
			notes << QStringLiteral("Couldn't restore %1 — the file in the Trash no longer "
									"matches the one that was deleted, so it was left alone.")
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

	// One place to map a kind to its reverser, so nothing can drift on
	// the dispatch. An unknown kind does nothing rather than guessing.
	OpResult reverseOp(OpKind kind, const OpJournal::Entry &op, QStringList &notes)
	{
		switch (kind)
		{
		case OpKind::Delete:
			return reverseDelete(op, notes);
		case OpKind::Copy:
			return reverseCopy(op, notes);
		case OpKind::Move:
		case OpKind::Rename:
			return reverseMoveLike(op, notes);
		case OpKind::Undo:
			return OpResult::NothingToDo; // mapped away before dispatch
		}
		return OpResult::NothingToDo;
	}

	// The kind whose machinery reverses this RECORD's ops. Normal runs
	// reverse as themselves. An undo run's ops are already written in
	// inverse orientation, so they reverse under the machinery matching
	// what the undo was DOING: undoing a move meant moving files (Move),
	// undoing a copy meant trashing the copies (Delete semantics, with
	// finalPath), undoing a delete meant restoring (Move semantics).
	// Unknown originalKind = nullopt = touch nothing.
	std::optional<OpKind> reverserKindFor(const OpJournal::Record &rec)
	{
		if (rec.kind != OpKind::Undo)
			return rec.kindKnown ? std::optional<OpKind>(rec.kind) : std::nullopt;
		if (!rec.originalKind)
			return std::nullopt;
		switch (*rec.originalKind)
		{
		case OpKind::Move:
		case OpKind::Rename:
			return OpKind::Move;
		case OpKind::Copy:
			return OpKind::Delete;
		case OpKind::Delete:
			return OpKind::Move;
		case OpKind::Undo:
			return std::nullopt;
		}
		return std::nullopt;
	}

	// MARK: - Volume resolution plumbing

	// Does `root` sit at the start of `path`, on a path-component
	// boundary? ("/Volumes/EDIT 1" is a prefix of "/Volumes/EDIT 1/x"
	// but not of "/Volumes/EDIT 10/x".)
	bool rootPrefixes(const QString &root, const QString &path)
	{
		if (root.isEmpty() || !path.startsWith(root))
			return false;
		if (path.size() == root.size())
			return true;
		if (root.endsWith(QLatin1Char('/')))
			return true;
		return path.at(root.size()) == QLatin1Char('/');
	}

	QString reanchor(const QString &path, const QString &oldRoot, const QString &newRoot)
	{
		QString tail = path.mid(oldRoot.size());
		QString base = newRoot;
		while (base.endsWith(QLatin1Char('/')))
			base.chop(1);
		if (!tail.startsWith(QLatin1Char('/')))
			tail.prepend(QLatin1Char('/'));
		return base + tail;
	}

	// Per-recorded-volume disposition against the mounted table.
	struct VolumeDisposition
	{
		enum class State
		{
			InPlace,
			Reanchored,
			Missing
		};
		State state = State::InPlace;
		QString newRoot;
		QString note;
	};

	VolumeDisposition disposeVolume(const VolumeIdentity &recorded,
									const QVector<VolumeIdentity> &mounted)
	{
		VolumeDisposition d;
		const VolumeIdentity *atRecordedRoot = nullptr;
		const VolumeIdentity *matchElsewhere = nullptr;
		for (const VolumeIdentity &m : mounted)
		{
			if (m.rootPath == recorded.rootPath)
				atRecordedRoot = &m;
			else if (recorded.matches(m))
				matchElsewhere = &m;
		}

		if (atRecordedRoot && recorded.matches(*atRecordedRoot))
			return d; // same drive, same address: business as usual

		if (matchElsewhere)
		{
			// The drive is back under a new name/letter. Follow it.
			d.state = VolumeDisposition::State::Reanchored;
			d.newRoot = matchElsewhere->rootPath;
			d.note = QStringLiteral("The volume '%1' is now mounted at %2 (it was %3); "
									"recovery followed it there.")
						 .arg(recorded.label, matchElsewhere->rootPath, recorded.rootPath);
			return d;
		}

		if (atRecordedRoot)
		{
			// THE impostor case: a different volume sits at the recorded
			// address, and the real one is nowhere. Touch nothing.
			d.state = VolumeDisposition::State::Missing;
			d.note = QStringLiteral("A different volume is mounted at %1 than the one this "
									"operation used ('%2'). Nothing there was touched; "
									"reconnect the original drive and relaunch.")
						 .arg(recorded.rootPath, recorded.label);
			return d;
		}

		d.state = VolumeDisposition::State::Missing;
		d.note = QStringLiteral("The volume '%1' (%2) isn't connected. This operation's "
								"recovery will wait for it.")
					 .arg(recorded.label, recorded.rootPath);
		return d;
	}

} // namespace

// Resolve every path a record carries through its recorded volume
// fingerprints. True = the record may be acted on (paths possibly
// rewritten in place); false = leave it entirely for a later launch (no
// recovered stamp, so it is retried).
bool OpRescue::resolveRecord(OpJournal::Record &rec, const QVector<VolumeIdentity> &mounted,
							 QStringList &notes)
{
	if (rec.volumes.isEmpty())
		return true; // nothing recorded; v1 per-op guards still apply

	QVector<QPair<QString, QString>> rewrites; // oldRoot -> newRoot
	for (const VolumeIdentity &vol : rec.volumes)
	{
		const VolumeDisposition d = disposeVolume(vol, mounted);
		if (d.state == VolumeDisposition::State::Missing)
		{
			notes << d.note;
			return false;
		}
		if (d.state == VolumeDisposition::State::Reanchored)
		{
			notes << d.note;
			rewrites.append({vol.rootPath, d.newRoot});
		}
	}
	if (rewrites.isEmpty())
		return true;

	const auto fix = [&rewrites](QString &path)
	{
		if (path.isEmpty())
			return;
		for (const auto &[oldRoot, newRoot] : rewrites)
		{
			if (rootPrefixes(oldRoot, path))
			{
				path = reanchor(path, oldRoot, newRoot);
				return;
			}
		}
	};

	for (OpJournal::Entry &op : rec.ops)
	{
		fix(op.src);
		fix(op.dst);
		fix(op.parked);
		fix(op.finalPath);
		fix(op.parkedFinal);
	}
	for (OpItem &it : rec.plan)
	{
		fix(it.src);
		fix(it.renameDst);
	}
	fix(rec.planDest);
	return true;
}

// MARK: - Mounted volumes

QVector<VolumeIdentity> OpRescue::mountedVolumes()
{
	QVector<VolumeIdentity> out;
	const QList<QStorageInfo> mounts = QStorageInfo::mountedVolumes();
	out.reserve(mounts.size());
	for (const QStorageInfo &m : mounts)
	{
		if (!m.isValid() || !m.isReady())
			continue;
		const VolumeIdentity v = VolumeIdentity::capture(m.rootPath());
		if (v.confidence != VolumeIdentity::Confidence::Low)
			out.append(v);
	}
	return out;
}

OpRescue::ResolvedPath OpRescue::resolvePath(const QString &journaledPath,
											 const QVector<VolumeIdentity> &recorded,
											 const QVector<VolumeIdentity> &mounted)
{
	ResolvedPath out;
	out.path = journaledPath;

	// Longest matching recorded root wins ("/" would otherwise shadow
	// every real mount under it).
	const VolumeIdentity *owner = nullptr;
	for (const VolumeIdentity &vol : recorded)
		if (rootPrefixes(vol.rootPath, journaledPath))
			if (!owner || vol.rootPath.size() > owner->rootPath.size())
				owner = &vol;
	if (!owner)
		return out; // no fingerprint recorded for this path; proceed as-is

	const VolumeDisposition d = disposeVolume(*owner, mounted);
	switch (d.state)
	{
	case VolumeDisposition::State::InPlace:
		return out;
	case VolumeDisposition::State::Reanchored:
		out.state = ResolvedPath::State::Reanchored;
		out.path = reanchor(journaledPath, owner->rootPath, d.newRoot);
		out.note = d.note;
		return out;
	case VolumeDisposition::State::Missing:
		out.state = ResolvedPath::State::Wait;
		out.note = d.note;
		return out;
	}
	return out;
}

// MARK: - Resumable classification

std::optional<OpRescue::Resumable> OpRescue::resumableFrom(const OpJournal::Record &rec)
{
	// Only an interrupted run is resumable: a finished or cancelled run
	// concluded on the user's watch, and a run with no plan can't say
	// what it meant to do. An undo run is not re-dispatched as a plain
	// operation — the user can simply press Undo again.
	if (!rec.hasPlan || rec.complete || rec.plan.isEmpty())
		return std::nullopt;
	if (!rec.kindKnown || rec.kind == OpKind::Undo)
		return std::nullopt;

	// A planned file is finished when its op says so (done or skipped)
	// or when the disk says so (opConcluded — the work landed but the
	// 'done' line didn't). The launch sweep leaves exactly those files
	// alone, so the offer and the sweep can never disagree.
	QSet<QString> finished;
	bool sawMediaMusterTrash = false;
	for (const OpJournal::Entry &op : rec.ops)
	{
		if (!(op.skipped || opConcluded(rec.kind, op)))
			continue;
		finished.insert(op.src);
		if (op.finalPath.contains(Conventions::kMediaMusterTrashDir))
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
	for (const OpItem &it : rec.plan)
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

QVector<OpRescue::Resumable> OpRescue::pending(const QString &dir)
{
	QVector<Resumable> out;
	for (const OpJournal::Record &rec : OpJournal::scan(dir))
	{
		// Only journals the launch sweep has already tidied and stamped.
		// Resuming an unswept one could dispatch over a stranded parked
		// original and then supersede the only record of it.
		if (!rec.recovered)
			continue;
		if (const auto r = resumableFrom(rec))
			out.append(*r);
	}
	return out;
}

// MARK: - Launch sweep

OpRescue::Summary OpRescue::run(const QString &dir, const QVector<VolumeIdentity> &mountedOverride)
{
	Summary sum;
	const QVector<OpJournal::Record> records = OpJournal::scan(dir);
	if (records.isEmpty())
		return sum;

	const QVector<VolumeIdentity> mounted =
		mountedOverride.isEmpty() ? mountedVolumes() : mountedOverride;

	// The one finished journal worth keeping: the undo candidate.
	const auto undoCandidate = OpJournal::latestUndoable(dir);
	const QString undoCandidatePath = undoCandidate ? undoCandidate->path : QString();

	for (OpJournal::Record rec : records)
	{
		// Already rolled back on an earlier launch. If it still has
		// files the user never got to, it stays and is offered again;
		// otherwise it goes.
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

		// Finished and clean: the v2 retention rule. The newest
		// undoable run stays (it is what Edit ▸ Undo acts on) until a
		// new operation supersedes it or it ages out; every other
		// finished journal is spent.
		if (rec.complete && !rec.dirty)
		{
			bool keep = (rec.path == undoCandidatePath);
			if (keep)
			{
				const QDateTime started =
					QDateTime::fromString(rec.started, Qt::ISODateWithMs);
				if (started.isValid() &&
					started.daysTo(QDateTime::currentDateTimeUtc()) > kUndoCandidateMaxAgeDays)
					keep = false;
			}
			if (!keep)
				QFile::remove(rec.path);
			continue;
		}

		// A live owner is a second instance mid-write, not a crash.
		if (ownerStillAlive(rec.pid, rec.host))
			continue;

		// Volume resolution: follow drives that moved, and never touch
		// a volume that isn't (or isn't really) there. A record left
		// unresolved is NOT stamped, so a later launch retries it.
		if (!resolveRecord(rec, mounted, sum.notes))
			continue;

		// What machinery reverses these ops (undo runs map to their
		// original kind). Nothing trustworthy = reverse nothing, but
		// still stamp: an unreadable record must not haunt every launch.
		const std::optional<OpKind> reverser = reverserKindFor(rec);

		// A finished-but-dirty run is NOT unwound — only the ops whose
		// rollback stalled get retried.
		const bool dirtyOnly = rec.complete;

		// Finished work stays, except for a journal nothing can be
		// offered from — no plan, or a kind this build can't read —
		// where putting files back is the only help left.
		const bool keepFinishedWork = rec.hasPlan && reverser.has_value();

		int reversed = 0;
		int flagged = 0;
		QStringList journalNotes;
		// The run's own notes (durability degrades, reroutes) resurface
		// with the recovery report — they were written to be read.
		journalNotes += rec.notes;

		// Undo newest op first so any ordering dependency unwinds the
		// way it was built.
		for (int i = rec.ops.size() - 1; i >= 0; --i)
		{
			const OpJournal::Entry &op = rec.ops[i];
			if (dirtyOnly && !op.rollbackIncomplete)
				continue;

			if (keepFinishedWork && !op.rollbackIncomplete && opConcluded(*reverser, op))
			{
				// The work stands. One loose end can outlive it: a
				// Replace parked the old destination aside and the crash
				// beat the disposal. With the parked file's identity
				// verified, finishing that disposal would be safe — but
				// deleting a user's file on ANY inference stays out of
				// scope for the sweep; name it instead.
				if (!op.parked.isEmpty() && QFile::exists(op.parked) &&
					op.parkedFinal.isEmpty())
				{
					journalNotes << strandedParkNote(op, rec.kind == OpKind::Copy);
					++flagged;
				}
				continue;
			}

			const OpResult r = reverser ? reverseOp(*reverser, op, journalNotes)
										: OpResult::NothingToDo;
			if (r == OpResult::Reversed)
				++reversed;
			else if (r == OpResult::Flagged)
				++flagged;
		}

		// Stamp it recovered LAST: a crash mid-rollback leaves it
		// unstamped, so the next launch retries. Every step above is
		// idempotent.
		OpJournal::markRecovered(rec.path, reversed, flagged);

		if (const auto r = resumableFrom(rec))
			sum.resumable.append(*r);

		// Only narrate journals whose rollback did something.
		if (reversed == 0 && flagged == 0)
			continue;

		++sum.journalsRecovered;
		sum.opsReversed += reversed;
		sum.opsFlagged += flagged;

		const QString kindWord = rec.kind == OpKind::Undo
									 ? QStringLiteral("undo")
									 : opKindName(rec.kind);
		QString head = QStringLiteral("Tidied up after an interrupted %1 — %2 file(s) put "
									  "back or removed. Finished files were left alone.")
						   .arg(kindWord)
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
