#include "operation_manager.h"
#include "operation_request.h"

// MARK: - Construction

OperationManager::OperationManager(QObject *parent) : QObject(parent)
{
	qRegisterMetaType<OperationResult>();
	qRegisterMetaType<QVector<OperationTrashFallbackItem>>();
}

OperationManager::~OperationManager()
{
	// Release a waiting decision before joining: teardown must not depend on
	// the GUI event loop delivering a queued reply.
	cancel();
	m_job.shutdown();
}

// MARK: - Cancellation and Trash fallback

void OperationManager::cancel()
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

void OperationManager::setTrashFallbackHandlerAvailable(bool available)
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

bool OperationManager::isTrashFallbackPending(quint64 requestId) const
{
	QMutexLocker lock(&m_trashFallbackMutex);
	return requestId && m_pendingTrashFallbackRequest == requestId &&
		   !m_trashFallbackAnswered && !m_job.isCancelled();
}

void OperationManager::respondTrashFallback(quint64 requestId, bool accepted)
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

bool OperationManager::confirmTrashFallback(const QVector<OperationTrashFallbackItem> &items)
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

void OperationManager::progress(const QString &name, int current, int total, double pct)
{
	emit operationProgress(name, current, total, pct);
}

void OperationManager::log(QtMsgType level, const QString &message)
{
	emit operationLog(level, message);
}

void OperationManager::trashUsed(const QString &folder, int count)
{
	emit mediaMusterTrashUsed(folder, count);
}

// MARK: - Item building

QVector<OperationItem> OperationManager::itemsFromMediaFiles(const QVector<MediaFile> &files,
											   const QHash<QString, ConflictPolicy> &policies)
{
	QVector<OperationItem> out;
	out.reserve(files.size());
	for (const MediaFile &mf : files)
	{
		auto it = operationItemFromMediaFile(mf);
		if (const auto p = policies.constFind(mf.mediaFilePath); p != policies.constEnd())
			it.policy = conflictPolicyName(p.value());
		out.append(it);
	}
	return out;
}

// MARK: - Job entry points

void OperationManager::execute(OperationRequest request)
{
	startRun(std::move(request));
}

void OperationManager::result(const OperationResult &value)
{
	emit operationResult(value);
}

// MARK: - The worker

void OperationManager::startRun(OperationRequest request)
{
	// A second dispatch must never cancel the important job already running.
	if (m_running)
	{
		emit operationLog(QtWarningMsg, QStringLiteral("Another operation is still running."));
		return;
	}
	if (request.kind == OperationKind::Undo && request.resumeJournalPath.isEmpty())
	{
		if (!m_undoEnabled)
		{
			emit operationLog(QtWarningMsg, QStringLiteral("Undo is disabled in this build."));
			emit operationFinished(0, 1);
			return;
		}
		request.undoEnabled = true;
	}
	m_running = true;
	m_job.start(
		[this, request = std::move(request)]
		{
			OperationRunner runner(*this, m_job.cancelFlag());
			const OperationRunner::Totals totals = runner.run(request);
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
