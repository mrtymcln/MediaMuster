#include "opmanager.h"

#include "opundo.h"

// MARK: - Construction

OpManager::OpManager(QObject *parent)
	: QObject(parent)
{
}

// MARK: - OpSink (re-emit as signals)

void OpManager::progress(const QString &name, int current, int total, double pct)
{
	emit operationProgress(name, current, total, pct);
}

void OpManager::itemDone(const QString &name, const QString &path, bool ok, const QString &error,
						 bool skipped)
{
	emit operationItemDone(name, path, ok, error, skipped);
}

void OpManager::log(QtMsgType level, const QString &message)
{
	emit operationLog(level, message);
}

void OpManager::trashUsed(const QString &folder, int count)
{
	emit mediaMusterTrashUsed(folder, count);
}

// MARK: - Item building

QVector<OpItem> OpManager::itemsFromMediaFiles(const QVector<MediaFile> &files,
											   const QHash<QString, ConflictPolicy> &policies)
{
	QVector<OpItem> out;
	out.reserve(files.size());
	for (const MediaFile &mf : files)
	{
		OpItem it;
		it.src = mf.filePath;
		it.name = mf.fileName;
		it.folder = mf.mxfFolder;
		it.bytes = mf.sizeBytes;
		if (const auto p = policies.constFind(mf.filePath); p != policies.constEnd())
			it.policy = conflictPolicyName(p.value());
		// The scan's Avid identity claims. The runner cross-checks the
		// file on disk against these before touching it, and every
		// journal/undo/recovery message can then name the clip the
		// editor knows rather than a cryptic MXF filename.
		it.mobId = mf.mobId;
		it.masterMobId = mf.masterMobId;
		it.clipName = mf.clipName;
		out.append(it);
	}
	return out;
}

// MARK: - Path helpers

QString OpManager::buildDestPath(const MediaFile &mf, const QString &destRoot, bool preserve)
{
	return OpRunner::buildDestPath(mf.fileName, mf.mxfFolder, destRoot, preserve);
}

// MARK: - Job entry points

void OpManager::execute(OpRequest request)
{
	startRun(std::move(request));
}

void OpManager::executeUndo(const QString &journalPath)
{
	// Same worker discipline as every run: one BackgroundJob, cancel
	// token polled inside; exactly one operationFinished at the end.
	m_job.start(
		[this, journalPath]
		{
			OpUndo undo(*this, m_job.cancelFlag());
			const OpRunner::Totals totals = undo.run(journalPath);
			emit operationFinished(totals.succeeded, totals.failed);
		});
}

// MARK: - The worker

void OpManager::startRun(OpRequest request)
{
	m_job.start(
		[this, request = std::move(request)]
		{
			OpRunner runner(*this, m_job.cancelFlag());
			runner.onRenameFolderTouched = renameFolderTouched;
			const OpRunner::Totals totals = runner.run(request);
			// Exactly once, on every path out of run() — this is what
			// un-busies the UI, so nothing may return without it.
			emit operationFinished(totals.succeeded, totals.failed);
		});
}
