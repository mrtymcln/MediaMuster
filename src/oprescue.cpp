#include "oprescue.h"
#include "oprunner.h"
#include <QDir>
#include <QSet>

std::optional<OpRescue::Resumable> OpRescue::resumableFrom(const OpJournal::Record &rec)
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
	out.usedMediaMusterTrash = out.kind == OpKind::Delete;
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
QVector<OpRescue::Resumable> OpRescue::pending(const QString &directory)
{
	QVector<Resumable> out;
	for (const auto &rec : OpJournal::interrupted(directory))
	{
		if (auto r = resumableFrom(rec))
			out.append(*r);
	}
	return out;
}
OpRescue::Summary OpRescue::run(const QString &directory, const QVector<VolumeIdentity> &mounted)
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
	for (const auto &path : OpJournal::unreadableRecords(directory))
	{
		out.notes.append("Incompatible beta recovery record retained without execution: " + path);
	}
	const auto records = OpJournal::scan(directory);
	QSet<QString> claimed;
	for (const auto &record : records)
		if (!record.corrupt && record.request.kind == OpKind::Undo)
			claimed.insert(record.request.undoOf);
	for (auto rec : records)
	{
		if (rec.corrupt)
		{
			++out.opsFlagged;
			out.notes.append("Invalid journal retained for inspection: " + rec.path);
			continue;
		}
		if (rec.dismissed || !rec.undoPath.isEmpty() || claimed.contains(rec.path))
			continue;
		bool incomplete = false, artifacts = false;
		for (const auto &e : rec.entries)
		{
			if (!e.complete())
				incomplete = true;
			artifacts = artifacts || !e.artifacts.isEmpty();
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
		if (!incomplete)
		{
			reportArtifacts(rec);
			continue;
		}
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
			if (e.complete() || e.step == OpJournal::Step::Planned)
				continue;
			if (!OpRunner::reconcile(journal, e, error))
			{
				++out.opsFlagged;
				out.notes.append(error + " Journal: " + rec.path);
			}
			else
				++out.journalsRecovered;
		}
		if (auto r = resumableFrom(journal.record()))
			out.resumable.append(*r);
		reportArtifacts(journal.record());
	}
	out.undoCandidate = OpJournal::latestUndoable(directory);
	return out;
}
