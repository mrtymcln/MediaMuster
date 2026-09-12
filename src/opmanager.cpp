#include "opmanager.h"

// MARK: - Construction

OpManager::OpManager(QObject *parent) : QObject(parent)
{
	qRegisterMetaType<OpResult>();
}

// MARK: - OpSink (re-emit as signals)

void OpManager::progress(const QString &name, int current, int total, double pct)
{
	emit operationProgress(name, current, total, pct);
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
		it.omfEra = mf.omfEra; // OMF-era: travels with the item, and through the journal
		it.bytes = mf.sizeBytes;
		it.modifiedMs = mf.modified.isValid() ? mf.modified.toMSecsSinceEpoch() : -1;
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
	return OpRunner::buildDestPath(mf.fileName, mf.mxfFolder, destRoot, preserve,
								   mf.omfEra); // OMF-era: the scanner's verdict routes preserve
}

// MARK: - Job entry points

void OpManager::execute(OpRequest request)
{
	startRun(std::move(request));
}

void OpManager::executeUndo(const QString &journalPath)
{
	OpRequest request;
	request.kind = OpKind::Undo;
	request.undoJournalPath = journalPath;
	startRun(std::move(request));
}

void OpManager::result(const OpResult &value)
{
	emit operationResult(value);
}

// MARK: - The worker

void OpManager::startRun(OpRequest request)
{
	// A second dispatch must never cancel the important job already running.
	if (m_running)
	{
		emit operationLog(QtWarningMsg, QStringLiteral("Another operation is still running."));
		return;
	}
	if (request.kind == OpKind::Undo && request.resumeJournalPath.isEmpty())
	{
		if (!m_undoEnabled)
		{
			emit operationLog(QtWarningMsg, QStringLiteral("Enable Undo in the Debug menu first."));
			emit operationFinished(0, 1);
			return;
		}
		request.undoEnabled = true;
	}
	m_running = true;
	m_job.start(
		[this, request = std::move(request)]
		{
			OpRunner runner(*this, m_job.cancelFlag());
			runner.onRenameFolderTouched = renameFolderTouched;
			const OpRunner::Totals totals = runner.run(request);
			// Join before announcing completion: the next recovery scan must
			// see a released journal lock and a worker that has fully exited.
			QMetaObject::invokeMethod(this, [this, totals]
			{
				m_job.shutdown();
				m_running = false;
				emit operationFinished(totals.succeeded,
									   totals.failed + totals.needsAttention + totals.retained);
			}, Qt::QueuedConnection);
		});
}
