#include "opmanager.h"

// MARK: - Construction

OpManager::OpManager(QObject *parent) : QObject(parent)
{
	qRegisterMetaType<OpResult>();
	qRegisterMetaType<QVector<OpTrashFallbackItem>>();
}

OpManager::~OpManager()
{
	// Release a waiting decision before joining: teardown must not depend on
	// the GUI event loop delivering a queued reply.
	cancel();
	m_job.shutdown();
}

void OpManager::cancel()
{
	m_job.cancel();
	quint64 requestId = 0;
	{
		QMutexLocker lock(&m_trashFallbackMutex);
		if (m_pendingTrashFallbackRequest && !m_trashFallbackAnswered)
		{
			requestId = m_pendingTrashFallbackRequest;
			m_trashFallbackAnswered = true;
			m_trashFallbackAccepted = false;
			m_trashFallbackChanged.wakeAll();
		}
	}
	if (requestId)
		emit trashFallbackFinished(requestId);
}

void OpManager::setTrashFallbackHandlerAvailable(bool available)
{
	quint64 requestId = 0;
	{
		QMutexLocker lock(&m_trashFallbackMutex);
		m_trashFallbackHandlerAvailable = available;
		if (!available && m_pendingTrashFallbackRequest && !m_trashFallbackAnswered)
		{
			requestId = m_pendingTrashFallbackRequest;
			m_trashFallbackAnswered = true;
			m_trashFallbackAccepted = false;
			m_trashFallbackChanged.wakeAll();
		}
	}
	if (requestId)
		emit trashFallbackFinished(requestId);
}

bool OpManager::isTrashFallbackPending(quint64 requestId) const
{
	QMutexLocker lock(&m_trashFallbackMutex);
	return requestId && m_pendingTrashFallbackRequest == requestId &&
		   !m_trashFallbackAnswered && !m_job.isCancelled();
}

void OpManager::respondTrashFallback(quint64 requestId, bool accepted)
{
	{
		QMutexLocker lock(&m_trashFallbackMutex);
		if (!requestId || m_pendingTrashFallbackRequest != requestId || m_trashFallbackAnswered)
			return;
		m_trashFallbackAnswered = true;
		m_trashFallbackAccepted = accepted && !m_job.isCancelled() && m_trashFallbackHandlerAvailable;
		m_trashFallbackChanged.wakeAll();
	}
	emit trashFallbackFinished(requestId);
}

bool OpManager::confirmTrashFallback(const QVector<OpTrashFallbackItem> &items)
{
	QMutexLocker lock(&m_trashFallbackMutex);
	if (items.isEmpty() || !m_trashFallbackHandlerAvailable || m_job.isCancelled() ||
		m_pendingTrashFallbackRequest)
		return false;
	const quint64 requestId = ++m_nextTrashFallbackRequest;
	m_pendingTrashFallbackRequest = requestId;
	m_trashFallbackAnswered = false;
	m_trashFallbackAccepted = false;
	lock.unlock();
	emit trashFallbackRequested(requestId, items);
	lock.relock();
	while (!m_trashFallbackAnswered && !m_job.isCancelled() && m_trashFallbackHandlerAvailable)
		m_trashFallbackChanged.wait(&m_trashFallbackMutex);
	const bool accepted = m_trashFallbackAnswered && m_trashFallbackAccepted && !m_job.isCancelled();
	m_pendingTrashFallbackRequest = 0;
	return accepted;
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
		it.folder = mf.mediaFolderName;
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

// MARK: - Job entry points

void OpManager::execute(OpRequest request)
{
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
			emit operationLog(QtWarningMsg, QStringLiteral("Enable undo in the Debug menu first."));
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
			const OpRunner::Totals totals = runner.run(request);
			// Join before announcing completion: the next recovery scan must
			// see a released journal lock and a worker that has fully exited.
			QMetaObject::invokeMethod(this, [this, totals]
									  {
				m_job.shutdown();
				m_running = false;
				emit operationFinished(totals.succeeded,
									   totals.failed + totals.needsAttention + totals.retained); }, Qt::QueuedConnection);
		});
}
