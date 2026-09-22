#include "operationrecovery.h"
#include "oprunner.h"
#include <QDir>
#include <QHash>
#include <QSet>

namespace
{
	using CompletedUndoEntries = QHash<int, QVector<OpJournal::Entry>>;

	CompletedUndoEntries completedUndoEntries(
		OpJournal::Record inverse, const QVector<VolumeIdentity> &mounted)
	{
		QString error;
		if (!OpJournal::resolve(inverse, error, mounted))
			return {}; // Unresolved evidence cannot settle the forward job.
		CompletedUndoEntries completed;
		for (const auto &entry : inverse.entries)
			if (entry.complete())
				completed[entry.undoEntryId].append(entry);
		return completed;
	}

	bool settleOriginalFromUndo(
		OpJournal &journal, OpJournal::Entry &entry, const CompletedUndoEntries &completed)
	{
		if (!entry.needsOriginalRestoration())
			return true;
		const auto candidates = completed.constFind(entry.id);
		if (candidates == completed.cend() || OpFile::occupied(entry.retirement))
			return true;

		// Undo keeps ownership of the forward job. Its completed entries can
		// establish where the original ended up without replaying forward work.
		const auto original = OpFile::inspect(entry.item.src);
		for (const auto &done : candidates.value())
		{
			if (entry.source.unchanged(original))
			{
				entry.sourceRemoved = false;
				entry.step = OpJournal::Step::SourceRestored;
			}
			else if (done.undoAction == "restoreMove" && done.dst == entry.item.src &&
					 done.landed.unchanged(original))
			{
				entry.sourceRemoved = true; // Undo replaced the original object with its completed copy.
				entry.step = OpJournal::Step::SourceRemoved;
			}
			else
				continue;
			entry.error.clear();
			return journal.save(entry);
		}
		return true;
	}
} // namespace

std::optional<OperationRecovery::Resumable> OperationRecovery::resumableFrom(const OpJournal::Record &rec)
{
	if (rec.corrupt || rec.dismissed || !rec.undoPath.isEmpty())
		return {};
	Resumable out;
	out.journalPath = rec.path;
	out.kind = rec.request.kind;
	out.dest = rec.request.destRoot;
	out.preserve = rec.request.preserve;
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
	QHash<QString, OpJournal::Record> inverses;
	for (const auto &record : records)
		if (!record.corrupt && record.request.kind == OpKind::Undo)
			inverses.insert(record.request.undoOf, record);
	for (auto rec : records)
	{
		if (rec.corrupt)
		{
			++out.opsFlagged;
			out.notes.append("Invalid journal retained for inspection: " + rec.path);
			continue;
		}
		const bool forwardActive = !rec.dismissed && rec.undoPath.isEmpty() && !inverses.contains(rec.path);
		bool incomplete = false;
		bool artifacts = false;
		bool needsRestoration = false;
		for (const auto &e : rec.entries)
		{
			if (forwardActive && !e.complete())
				incomplete = true;
			needsRestoration = needsRestoration || e.needsOriginalRestoration();
			artifacts = artifacts || !e.artifacts.isEmpty() || !e.cleanup.isEmpty();
		}
		if (!incomplete && !artifacts && !needsRestoration)
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
		const auto inverse = inverses.constFind(rec.path);
		const auto completedUndo = needsRestoration && inverse != inverses.cend()
									   ? completedUndoEntries(inverse.value(), mounted)
									   : CompletedUndoEntries{};
		for (auto e : rec.entries)
		{
			if (!settleOriginalFromUndo(journal, e, completedUndo))
			{
				++out.opsFlagged;
				out.notes.append(journal.error() + " Journal: " + rec.path);
				break;
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
			if (!journal.healthy())
				break;
		}
		if (auto r = resumableFrom(journal.record()))
			out.resumable.append(*r);
		reportArtifacts(journal.record());
	}
	out.undoCandidate = OpJournal::latestUndoable(directory);
	out.restorable = restorable(directory);
	return out;
}
