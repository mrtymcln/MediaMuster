#include "opundo.h"

#include "conventions.h"
#include "formatutil.h"
#include "oprescue.h"
#include "parkedfile.h"
#include "trashrouter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>

namespace
{
	// The display name for messages, mirroring the runner's rule: the
	// clip name the editor knows when the journal recorded one, else the
	// file name. Undo works from the ORIGINAL run's plan, so look the op
	// up there by source path.
	QString displayNameFor(const OpJournal::Record &rec, const OpJournal::Entry &op)
	{
		for (const OpItem &it : rec.plan)
		{
			if (it.src == op.src)
				return it.clipName.isEmpty() ? it.name : it.clipName;
		}
		return QFileInfo(op.src).fileName();
	}

	// The identity to check the LANDED file against. Runs record a
	// fresh capture of the landed file when they can; a rename-fast-path
	// move recorded the source's identity re-captured at the new path.
	// Either way, prefer the landed capture and fall back to the source
	// identity RELOCATED (size + UMID only — the file has legitimately
	// changed address, so filesystem ids prove nothing).
	FileIdentity::Verdict verifyLanded(const QString &path, const OpJournal::Entry &op)
	{
		if (op.landedId.confidence != FileIdentity::Confidence::Low)
			return FileIdentity::verify(path, op.landedId);
		if (op.srcId.confidence != FileIdentity::Confidence::Low)
			return FileIdentity::verifyRelocated(path, op.srcId);
		// No identity in the journal (shouldn't happen for v2-written
		// runs) — no check to apply, same stance recovery takes.
		return FileIdentity::Verdict::Match;
	}
} // namespace

// MARK: - Construction

OpUndo::OpUndo(OpSink &sink, const std::atomic<bool> &cancel)
	: m_sink(sink), m_cancel(cancel)
{
}

// MARK: - The run

OpRunner::Totals OpUndo::run(const QString &originalJournalPath, const QString &journalDir,
							 const QVector<VolumeIdentity> &mountedOverride)
{
	OpRunner::Totals t;

	// Re-read and RE-QUALIFY at run time. The Edit-menu answer may be
	// minutes old; anything that disqualifies now (another undo finished,
	// the sweep stamped it, the file vanished) must refuse cleanly rather
	// than act on a stale belief.
	std::optional<OpJournal::Record> maybe = OpJournal::readOne(originalJournalPath);
	if (!maybe || !maybe->complete || maybe->dirty || maybe->undone || maybe->recovered ||
		!maybe->kindKnown || maybe->kind == OpKind::Undo || maybe->doneCount() == 0)
	{
		m_sink.log(QtWarningMsg,
				   QStringLiteral("Nothing to undo — the last operation's record is no longer "
								  "undoable (it may already have been undone, or a newer "
								  "operation replaced it)."));
		return t;
	}
	OpJournal::Record rec = *maybe;

	// Volume resolution, shared with the launch sweep so the two can
	// never disagree about a drive: paths are re-anchored when a volume
	// came back under a new name, and NOTHING runs when a volume is
	// missing or an impostor sits at its address.
	const QVector<VolumeIdentity> mounted =
		mountedOverride.isEmpty() ? OpRescue::mountedVolumes() : mountedOverride;
	QStringList volumeNotes;
	if (!OpRescue::resolveRecord(rec, mounted, volumeNotes))
	{
		for (const QString &n : volumeNotes)
			m_sink.log(QtWarningMsg, n);
		m_sink.log(QtWarningMsg,
				   QStringLiteral("Can't undo right now — a drive this operation used isn't "
								  "available. Reconnect it and try again."));
		return t;
	}
	for (const QString &n : volumeNotes)
		m_sink.log(QtInfoMsg, n);

	// The ops that actually landed, newest first — undoing in reverse
	// order unwinds any ordering dependency the way it was built.
	QVector<OpJournal::Entry> undoable;
	for (int i = rec.ops.size() - 1; i >= 0; --i)
	{
		if (rec.ops[i].completed)
			undoable.append(rec.ops[i]);
	}
	const int total = undoable.size();

	m_sink.log(QtInfoMsg, QStringLiteral("Undoing %1 (%2 files)")
							  .arg(opKindName(rec.kind))
							  .arg(total));

	// The undo is itself a write-ahead-journaled run: kind Undo, naming
	// what it reverses and the original's kind, so a CRASHED undo is
	// recovered by the same launch sweep as any other run. The original
	// journal is passed as the prune's spare — deleting it here would
	// destroy the very record being read.
	OpJournal journal(OpKind::Undo,
					  QJsonObject{{QStringLiteral("undoes"), QFileInfo(rec.path).fileName()},
								  {QStringLiteral("originalKind"), opKindName(rec.kind)}},
					  journalDir, rec.path);
	if (!journal.isOpen())
		m_sink.log(QtWarningMsg, OpJournal::openFailedText(OpKind::Undo));

	// The undo's plan: one item per inverse step, at the file's CURRENT
	// location, so an interrupted undo classifies and narrates like any
	// other interrupted run.
	{
		OpRequest inverse;
		inverse.kind = OpKind::Undo;
		for (const OpJournal::Entry &op : undoable)
		{
			OpItem it;
			it.src = rec.kind == OpKind::Delete ? op.finalPath : op.dst;
			it.name = QFileInfo(op.src).fileName();
			it.bytes = op.srcId.size >= 0 ? op.srcId.size : op.bytes;
			inverse.items.append(it);
		}
		journal.writePlan(QString(), false, inverse.items, OpRunner::volumesFor(inverse));
	}

	TrashRouter router(m_sink);
	QSet<QString> touchedFolders; // Rename undo: Avid DB resets, once per folder
	bool cancelled = false;

	for (int i = 0; i < total; ++i)
	{
		if (m_cancel.load(std::memory_order_acquire))
		{
			cancelled = true;
			break;
		}

		const OpJournal::Entry &op = undoable[i];
		const QString name = displayNameFor(rec, op);
		m_sink.progress(name, i + 1, total, 0);

		ItemOutcome out;
		switch (rec.kind)
		{
		case OpKind::Copy:
			out = undoCopyOp(op, journal, router, name, i + 1, total);
			break;
		case OpKind::Move:
			out = undoMoveOp(op, journal, router, name, i + 1, total);
			break;
		case OpKind::Delete:
			out = undoDeleteOp(op, journal, name);
			break;
		case OpKind::Rename:
			out = undoRenameOp(op, journal, name, touchedFolders);
			break;
		case OpKind::Undo:
			break; // unreachable: qualification refuses undo-of-undo
		}

		if (out.skipped)
			++t.skipped;
		else if (out.ok)
			++t.succeeded;
		else
			++t.failed;

		// A cancel that fired inside a copy-back surfaces as a failed
		// item with the cancel flag set; don't double-count it.
		if (m_cancel.load(std::memory_order_acquire) && !out.ok && !out.skipped)
		{
			--t.failed;
			cancelled = true;
			break;
		}
	}

	// Same stop-and-keep close as every run: landed inverse work stays.
	journal.finish(t.succeeded, t.failed, t.skipped, cancelled);

	if (router.mediaMusterCount() > 0)
		m_sink.trashUsed(router.mediaMusterFolder(), router.mediaMusterCount());

	// The original is stamped `undone` — taken out of undo candidacy —
	// only when this run finished CLEAN. A cancelled or partly-failed
	// undo leaves it unstamped, so Undo can be pressed again: items whose
	// inverse already holds skip quietly, and only the leftovers retry.
	if (!cancelled && t.failed == 0)
		OpJournal::markUndone(rec.path, QFileInfo(journal.path()).fileName());

	m_sink.log(t.failed > 0 ? QtWarningMsg : QtInfoMsg,
			   QStringLiteral("Undo %1: %2 put back, %3 couldn't be undone%4")
				   .arg(cancelled ? QStringLiteral("stopped") : QStringLiteral("complete"))
				   .arg(t.succeeded)
				   .arg(t.failed)
				   .arg(t.skipped > 0
							? QStringLiteral(", %1 already undone").arg(t.skipped)
							: QString()));
	return t;
}

// MARK: - Replaced-original restore (shared tail of Copy and Move undo)

bool OpUndo::restoreReplacedOriginal(const OpJournal::Entry &op, QString *why)
{
	if (op.parkedFinal.isEmpty())
		return true; // this op replaced nothing
	if (!QFile::exists(op.parkedFinal))
	{
		*why = QStringLiteral("the file it replaced is no longer in the trash (it may have "
							  "been emptied), so that file couldn't be put back");
		return false;
	}
	// The catch went to the trash by RENAME, so its identity survives
	// intact — but it has changed address, so compare as relocated.
	if (op.parkedOriginalId.confidence != FileIdentity::Confidence::Low &&
		FileIdentity::verifyRelocated(op.parkedFinal, op.parkedOriginalId) !=
			FileIdentity::Verdict::Match)
	{
		*why = QStringLiteral("the file in the trash no longer matches the one that was "
							  "replaced, so it was left where it is");
		return false;
	}
	if (!QFile::rename(op.parkedFinal, op.dst))
	{
		*why = QStringLiteral("the replaced file couldn't be moved back out of the trash");
		return false;
	}
	return true;
}

// MARK: - Copy undo

OpUndo::ItemOutcome OpUndo::undoCopyOp(const OpJournal::Entry &op, OpJournal &journal,
									   TrashRouter &router, const QString &name, int index,
									   int total)
{
	Q_UNUSED(index);
	Q_UNUSED(total);
	ItemOutcome out;

	// Already undone? The copy is gone from its landing place. One loose
	// end can survive a crashed earlier undo: the copy was trashed but
	// the replaced original never made it back — heal that here instead
	// of skipping past it, so pressing Undo again converges.
	if (!QFile::exists(op.dst))
	{
		QString why;
		if (!op.parkedFinal.isEmpty() && QFile::exists(op.parkedFinal) &&
			!QFile::exists(op.dst))
		{
			if (restoreReplacedOriginal(op, &why))
			{
				m_sink.itemDone(name, op.src, true, {}, false);
				out.ok = true;
				return out;
			}
			m_sink.itemDone(name, op.src, false,
							QStringLiteral("'%1': %2.").arg(name, why), false);
			return out;
		}
		m_sink.itemDone(name, op.src, true,
						QStringLiteral("'%1' was already undone.").arg(name), true);
		out.ok = out.skipped = true;
		return out;
	}

	// The landed copy must still be the file this run wrote — a file
	// that replaced it since is not ours to trash.
	if (verifyLanded(op.dst, op) != FileIdentity::Verdict::Match)
	{
		m_sink.itemDone(name, op.src, false,
						QStringLiteral("The file now at %1 isn't the copy this operation made, "
									   "so it was left alone.")
							.arg(op.dst),
						false);
		return out;
	}

	// Write-ahead, then act: the copy goes to the TRASH (undo of a copy
	// is a delete in disguise, and deletes never hard-unlink).
	JournalOpGuard lop(&journal, op.dst, QString(), op.landedId.size, QString(), op.landedId);
	const TrashRouter::Landing landing = router.trash(op.dst);
	if (!landing.ok)
	{
		lop.failed(QStringLiteral("trash refused"));
		m_sink.itemDone(name, op.src, false, QStringLiteral("'%1': %2").arg(name, landing.error),
						false);
		return out;
	}

	QString why;
	if (!restoreReplacedOriginal(op, &why))
	{
		OpJournal::DoneInfo info;
		info.finalPath = landing.finalPath;
		lop.done(info);
		m_sink.itemDone(name, op.src, false,
						QStringLiteral("The copy was moved to the trash, but %1.").arg(why),
						false);
		return out;
	}

	OpJournal::DoneInfo info;
	info.finalPath = landing.finalPath;
	lop.done(info);
	m_sink.itemDone(name, op.src, true, {}, false);
	out.ok = true;
	return out;
}

// MARK: - Move undo

OpUndo::ItemOutcome OpUndo::undoMoveOp(const OpJournal::Entry &op, OpJournal &journal,
									   TrashRouter &router, const QString &name, int index,
									   int total)
{
	ItemOutcome out;

	// Already undone? The file is back at its original address.
	if (QFile::exists(op.src))
	{
		if (op.srcId.confidence == FileIdentity::Confidence::Low ||
			FileIdentity::verifyRelocated(op.src, op.srcId) == FileIdentity::Verdict::Match)
		{
			m_sink.itemDone(name, op.src, true,
							QStringLiteral("'%1' was already undone.").arg(name), true);
			out.ok = out.skipped = true;
			return out;
		}
		m_sink.itemDone(name, op.src, false,
						QStringLiteral("A different file now sits at the original location %1, "
									   "so '%2' was left where it is.")
							.arg(op.src, name),
						false);
		return out;
	}

	if (!QFile::exists(op.dst))
	{
		m_sink.itemDone(name, op.src, false,
						QStringLiteral("The moved file is no longer at %1 — it may have been "
									   "moved again. Rescan to find it.")
							.arg(op.dst),
						false);
		return out;
	}

	if (verifyLanded(op.dst, op) != FileIdentity::Verdict::Match)
	{
		m_sink.itemDone(name, op.src, false,
						QStringLiteral("The file now at %1 isn't the one this operation moved, "
									   "so it was left alone.")
							.arg(op.dst),
						false);
		return out;
	}

	// Write-ahead for the journey home. `originalKind: move` in the begin
	// line means a crash between here and done is recovered with the
	// Move machinery over this exact src/dst pair.
	JournalOpGuard lop(&journal, op.dst, op.src, op.srcId.size, QString(),
					   op.landedId.confidence != FileIdentity::Confidence::Low ? op.landedId : op.srcId);

	QDir().mkpath(QFileInfo(op.src).absolutePath());

	// Same-volume: pure rename home. The same gate as the forward run —
	// QFile::rename would silently copy-and-unlink across volumes. Test
	// seam MEDIAMUSTER_FORCE_MOVE_COPY forces the copy-back leg, exactly
	// as it forces the forward copy leg.
	const bool forceCopyLeg = qEnvironmentVariableIsSet("MEDIAMUSTER_FORCE_MOVE_COPY");
	bool home = false;
	if (!forceCopyLeg && OpRunner::sameVolumeForRename(op.dst, op.src) &&
		QFile::rename(op.dst, op.src))
	{
		home = true;
	}
	else
	{
		// Cross-volume: copy back, verify, THEN trash the far copy — at
		// every instant at least one complete verified copy exists.
		//
		// Before a single byte moves, the far copy is checked against the
		// checksum the forward run recorded. The copy-back re-verifies
		// its own bytes too; this check proves the SOURCE of the copy-
		// back is the verified bytes the move landed, not silent
		// corruption that has sat there since.
		if (!op.hash.isEmpty())
		{
			const OpCopier::HashOutcome now = m_copier.hashFile(op.dst, m_cancel);
			if (now.status == OpCopier::HashOutcome::Status::Cancelled)
			{
				lop.failed(QStringLiteral("cancelled"));
				return out;
			}
			if (now.status != OpCopier::HashOutcome::Status::Succeeded ||
				QStringLiteral("%1").arg(now.digest, 16, 16, QLatin1Char('0')) != op.hash)
			{
				lop.failed(QStringLiteral("far copy failed checksum"));
				m_sink.itemDone(name, op.src, false,
								QStringLiteral("The file at %1 no longer matches the checksum "
											   "recorded when it was moved, so it was left "
											   "alone. Nothing changed.")
									.arg(op.dst),
								false);
				return out;
			}
		}

		ParkedFile homePark(op.src, Conventions::kMoveReplaceTag);
		homePark.park(); // slot is empty (checked above); arms the partial discard

		qint64 lastPct = -1;
		const auto onBytes = [&](qint64 copied, qint64 totalBytes)
		{
			const double pct = totalBytes > 0 ? (100.0 * copied / totalBytes) : 100.0;
			if (qint64(pct) != lastPct)
			{
				lastPct = qint64(pct);
				m_sink.progress(name, index, total, pct);
			}
		};

		const OpCopier::Result res =
			m_copier.copy(op.dst, op.src, homePark, m_cancel,
						  NativeFile::Durability::Platter, onBytes, {});
		if (res.outcome == OpCopier::Outcome::Cancelled)
		{
			lop.failed(QStringLiteral("cancelled"));
			return out;
		}
		if (res.outcome != OpCopier::Outcome::Succeeded)
		{
			lop.failed(QStringLiteral("copy-back failed"));
			m_sink.itemDone(name, op.src, false, res.error, false);
			return out;
		}
		// The directory-entry barrier before anything irreversible, same
		// as the forward move (review finding 4).
		if (!NativeFile::syncDirectory(QFileInfo(op.src).absolutePath()) &&
			!res.durabilityDegraded)
		{
			homePark.restore(); // discard the copy we just made (ours)
			lop.failed(QStringLiteral("home directory sync failed"));
			m_sink.itemDone(name, op.src, false,
							QStringLiteral("The drive couldn't confirm the restored file was "
										   "recorded in its folder, so this undo was rolled "
										   "back. Nothing changed."),
							false);
			return out;
		}
		homePark.commit();

		// The file is verifiably home and durable; the far copy goes to
		// the TRASH on its own volume — never a hard delete.
		const TrashRouter::Landing landing = router.trash(op.dst);
		if (!landing.ok)
		{
			lop.failed(QStringLiteral("far copy trash refused"));
			m_sink.itemDone(name, op.src, false,
							QStringLiteral("'%1' is back at its original location, but the far "
										   "copy couldn't be moved to the trash — the file now "
										   "exists in both places. %2")
								.arg(name, landing.error),
							false);
			return out;
		}
		home = true;
	}

	if (!home)
	{
		lop.failed(QStringLiteral("rename home failed"));
		m_sink.itemDone(name, op.src, false,
						QStringLiteral("'%1' couldn't be moved back to %2.").arg(name, op.src),
						false);
		return out;
	}

	// Home. Restore anything the forward move had replaced.
	QString why;
	const bool restored = restoreReplacedOriginal(op, &why);

	OpJournal::DoneInfo info;
	info.landedId = FileIdentity::capture(op.src, /*readContent=*/false);
	info.landedId.contentUmid = op.srcId.contentUmid;
	lop.done(info);

	if (!restored)
	{
		m_sink.itemDone(name, op.src, false,
						QStringLiteral("'%1' is back at its original location, but %2.")
							.arg(name, why),
						false);
		return out;
	}
	m_sink.itemDone(name, op.src, true, {}, false);
	out.ok = true;
	return out;
}

// MARK: - Delete undo

OpUndo::ItemOutcome OpUndo::undoDeleteOp(const OpJournal::Entry &op, OpJournal &journal,
										 const QString &name)
{
	ItemOutcome out;

	if (QFile::exists(op.src))
	{
		if (op.srcId.confidence == FileIdentity::Confidence::Low ||
			FileIdentity::verifyRelocated(op.src, op.srcId) == FileIdentity::Verdict::Match)
		{
			m_sink.itemDone(name, op.src, true,
							QStringLiteral("'%1' was already restored.").arg(name), true);
			out.ok = out.skipped = true;
			return out;
		}
		m_sink.itemDone(name, op.src, false,
						QStringLiteral("A different file now sits at %1, so '%2' was left in "
									   "the trash.")
							.arg(op.src, name),
						false);
		return out;
	}

	if (op.finalPath.isEmpty() || !QFile::exists(op.finalPath))
	{
		m_sink.itemDone(name, op.src, false,
						QStringLiteral("'%1' is no longer in the trash (it may have been "
									   "emptied), so it couldn't be restored.")
							.arg(name),
						false);
		return out;
	}

	// A trashed file moved by RENAME keeps its identity; compare as
	// relocated (the address changed, the file didn't).
	if (op.srcId.confidence != FileIdentity::Confidence::Low &&
		FileIdentity::verifyRelocated(op.finalPath, op.srcId) != FileIdentity::Verdict::Match)
	{
		m_sink.itemDone(name, op.src, false,
						QStringLiteral("The file in the trash no longer matches '%1', so it "
									   "was left where it is.")
							.arg(name),
						false);
		return out;
	}

	JournalOpGuard lop(&journal, op.finalPath, op.src, op.srcId.size, QString(), op.srcId);
	QDir().mkpath(QFileInfo(op.src).absolutePath());
	if (!QFile::rename(op.finalPath, op.src))
	{
		lop.failed(QStringLiteral("restore rename failed"));
		m_sink.itemDone(name, op.src, false,
						QStringLiteral("'%1' couldn't be moved back out of the trash.").arg(name),
						false);
		return out;
	}

	OpJournal::DoneInfo info;
	info.landedId = FileIdentity::capture(op.src, /*readContent=*/false);
	info.landedId.contentUmid = op.srcId.contentUmid;
	lop.done(info);
	m_sink.itemDone(name, op.src, true, {}, false);
	out.ok = true;
	return out;
}

// MARK: - Rename undo

OpUndo::ItemOutcome OpUndo::undoRenameOp(const OpJournal::Entry &op, OpJournal &journal,
										 const QString &name, QSet<QString> &touchedFolders)
{
	ItemOutcome out;

	if (QFile::exists(op.src))
	{
		if (op.srcId.confidence == FileIdentity::Confidence::Low ||
			FileIdentity::verifyRelocated(op.src, op.srcId) == FileIdentity::Verdict::Match)
		{
			m_sink.itemDone(name, op.src, true,
							QStringLiteral("'%1' was already undone.").arg(name), true);
			out.ok = out.skipped = true;
			return out;
		}
		m_sink.itemDone(name, op.src, false,
						QStringLiteral("A different file now sits at %1, so '%2' was left "
									   "where it is.")
							.arg(op.src, name),
						false);
		return out;
	}

	if (!QFile::exists(op.dst) || verifyLanded(op.dst, op) != FileIdentity::Verdict::Match)
	{
		m_sink.itemDone(name, op.src, false,
						QStringLiteral("The file this rename moved isn't at %1 any more, so "
									   "nothing was changed. Rescan to find it.")
							.arg(op.dst),
						false);
		return out;
	}

	JournalOpGuard lop(&journal, op.dst, op.src, op.srcId.size, QString(), op.srcId);
	if (!QFile::rename(op.dst, op.src))
	{
		lop.failed(QStringLiteral("rename back failed"));
		m_sink.itemDone(name, op.src, false,
						QStringLiteral("'%1' couldn't be renamed back.").arg(name), false);
		return out;
	}

	// Reset the folders' Avid databases exactly as the forward run did
	// (one implementation; the honest-absence rationale lives at its
	// definition in oprunner.cpp).
	for (const QString &folder : {QFileInfo(op.src).absolutePath(),
								  QFileInfo(op.dst).absolutePath()})
	{
		if (touchedFolders.contains(folder))
			continue;
		touchedFolders.insert(folder);
		resetAvidDatabases(folder, m_sink);
	}

	OpJournal::DoneInfo info;
	info.landedId = FileIdentity::capture(op.src, /*readContent=*/false);
	info.landedId.contentUmid = op.srcId.contentUmid;
	lop.done(info);
	m_sink.itemDone(name, op.src, true, {}, false);
	out.ok = true;
	return out;
}
