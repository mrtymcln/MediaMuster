#include "operationrecovery.h"
#include "oprunner.h"
#include <QDir>
#include <QHash>
#include <QSet>

std::optional<OperationRecovery::Resumable> OperationRecovery::resumableFrom(const OpJournal::Record &rec)
{
	if (rec.corrupt || rec.dismissed || !rec.undoPath.isEmpty())
		return {};
	Resumable out;
	out.journalPath = rec.path;
	out.kind = rec.request.kind;
	out.dest = rec.request.destRoot;
	out.preserve = rec.request.preserve;
	out.verifyCopies = rec.request.verifyCopies;
	out.copiesComplete = rec.copiesComplete;
	out.started = rec.started;
	for (const auto &e : rec.entries)
	{
		if (!e.item.maintenance)
		{
			++out.total;
			if (e.complete())
				++out.finished;
		}
		if (!e.complete())
			out.remaining.append(e.item);
	}
	return out.remaining.isEmpty() ? std::nullopt : std::optional<Resumable>(out);
}
QVector<OperationRecovery::Resumable> OperationRecovery::pending(const QString &directory)
{
	QVector<Resumable> out;
	for (const auto &rec : OpJournal::interrupted(directory))
	{
		if (auto r = resumableFrom(rec))
			out.append(*r);
	}
	return out;
}
QVector<OperationRecovery::Restorable> OperationRecovery::restorable(const QString &directory)
{
	QVector<Restorable> out;
	const auto records = OpJournal::scan(directory);
	QSet<QString> claimed;
	for (const auto &record : records)
		if (!record.corrupt && record.request.kind == OpKind::Undo)
			claimed.insert(record.request.undoOf);
	for (const auto &record : records)
	{
		if (record.corrupt || !record.undoPath.isEmpty() || claimed.contains(record.path))
			continue;
		Restorable job;
		job.journalPath = record.path;
		for (const auto &entry : record.entries)
			if (entry.needsOriginalRestoration())
			{
				job.originals.append(entry.item.src);
				job.retainedPaths.append(entry.retirement);
			}
		if (!job.originals.isEmpty())
			out.append(job);
	}
	return out;
}
OperationRecovery::Summary OperationRecovery::run(const QString &directory, const QVector<VolumeIdentity> &mounted)
{
	Summary out;
	auto reportArtifacts = [&](const OpJournal::Record &record)
	{
		for (const auto &entry : record.entries)
			for (const auto &path : entry.artifacts)
				if (OpFile::occupied(path))
				{
					++out.opsFlagged;
					out.notes.append("Isolated temporary file retained: " + path);
				}
	};
	QString error;
	auto lock = OpJournal::acquire(directory, error);
	if (!lock)
	{
		out.notes.append(error);
		return out;
	}
	const auto records = OpJournal::scan(directory);
	QSet<QString> claimed;
	QHash<QString, OpJournal::Record> inverses;
	for (const auto &record : records)
		if (!record.corrupt && record.request.kind == OpKind::Undo)
		{
			claimed.insert(record.request.undoOf);
			inverses.insert(record.request.undoOf, record);
		}
	for (auto rec : records)
	{
		if (rec.corrupt)
		{
			++out.opsFlagged;
			out.notes.append("Invalid journal retained for inspection: " + rec.path);
			continue;
		}
		const bool forwardActive = !rec.dismissed && rec.undoPath.isEmpty() && !claimed.contains(rec.path);
		bool incomplete = false, artifacts = false;
		for (const auto &e : rec.entries)
		{
			if (forwardActive && !e.complete())
				incomplete = true;
			artifacts = artifacts || !e.artifacts.isEmpty() || !e.cleanup.isEmpty() ||
				e.needsOriginalRestoration();
		}
		if (!incomplete && !artifacts)
			continue;
		if (!OpJournal::resolve(rec, error, mounted))
		{
			++out.opsFlagged;
			out.notes.append(error + " Journal: " + rec.path);
			if (auto r = resumableFrom(rec))
				out.resumable.append(*r);
			continue;
		}
		OpJournal journal;
		if (!journal.resume(rec, error))
		{
			++out.opsFlagged;
			out.notes.append(error);
			if (auto r = resumableFrom(rec))
				out.resumable.append(*r);
			continue;
		}
		for (auto e : rec.entries)
		{
			if (e.needsOriginalRestoration() && inverses.contains(rec.path) &&
				!OpFile::occupied(e.retirement))
			{
				// A completed inverse owns this forward job permanently. Its
				// evidence can settle an empty retirement folder without replaying
				// any forward move or relying on the now-discarded destination.
				auto inverse = inverses.value(rec.path);
				QString inverseError;
				if (OpJournal::resolve(inverse, inverseError, mounted))
					for (const auto &done : inverse.entries)
						if (done.complete() && done.undoEntryId == e.id)
						{
							const auto original = OpFile::inspect(e.item.src);
							if (e.source.unchanged(original))
							{
								e.sourceRemoved = false;
								e.step = OpJournal::Step::SourceRestored;
							}
							else if (done.undoAction == "restoreMove" && done.dst == e.item.src &&
									 done.landed.unchanged(original))
							{
								e.sourceRemoved = true; // The original object was replaced by Undo's verified copy.
								e.step = OpJournal::Step::SourceRemoved;
							}
							else
								continue;
							e.error.clear();
							journal.save(e);
							break;
						}
			}
			// Dismissal ends forward work, but cannot erase cleanup or a
			// restoring rename whose completion append was interrupted.
			if ((!e.complete() && e.step != OpJournal::Step::Planned && forwardActive) ||
				e.step == OpJournal::Step::RestoringSource)
			{
				if (!OpRunner::reconcile(journal, e, error))
				{
					++out.opsFlagged;
					out.notes.append(error + " Journal: " + rec.path);
				}
			}
			if (!journal.healthy())
				break;
			QString cleanupError;
			if (!OpRunner::cleanup(journal, e, cleanupError))
			{
				++out.opsFlagged;
				out.notes.append(cleanupError + " Journal: " + rec.path);
			}
		}
		if (auto r = resumableFrom(journal.record()))
			out.resumable.append(*r);
		reportArtifacts(journal.record());
	}
	out.undoCandidate = OpJournal::latestUndoable(directory);
	out.restorable = restorable(directory);
	return out;
}
