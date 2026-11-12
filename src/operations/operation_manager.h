#pragma once

#include "support/background_job.h"
#include "media/media_file_record.h"
#include "operation_request.h"
#include "operation_runner.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QVector>
#include <QWaitCondition>

#include <optional>

// Qt facade for the shared engine. Requests run on one owned worker; results
// reach the GUI through queued signals. The runner also holds the journal lock
// so another facade or recovery pass cannot change files concurrently.
class OperationManager : public QObject, private OperationSink
{
	Q_OBJECT
	friend class TestOperationUi;

public:
	explicit OperationManager(QObject *parent = nullptr);
	~OperationManager() override;

	// MARK: - Job entry points

	/// Dispatch Copy, Move, Trash or Rebalance, preserving the selected items.
	void execute(OperationRequest request);

	void setUndoEnabled(bool enabled) { m_undoEnabled = enabled; }
	bool isRunning() const { return m_running; }

	void cancel();

	// The UI explicitly registers its lifetime so a headless manager never
	// waits for a decision that nobody can supply. Replies are request-scoped:
	// an old dialog cannot authorize a later operation.
	void setTrashFallbackHandlerAvailable(bool available);
	bool isTrashFallbackPending(quint64 requestId) const;
	void respondTrashFallback(quint64 requestId, bool accepted);

	/// Convert selected rows and frozen scan checks into independent journalled requests.
	static QVector<OperationItem> itemsFromMediaFiles(const QVector<MediaFileRecord> &files,
													  const QHash<QString, ConflictPolicy> &policies);

signals:
	void operationResult(const OperationResult &result);

	// MARK: - Progress signals (consumed via QueuedConnection)

	/// pct is 0-100 for the file currently copying. A same-volume Move
	/// (pure rename) and Delete have no per-byte progress and emit 0.
	void operationProgress(const QString &fileName, int current, int total, double pct);

	/// Exactly once per run, including cancel.
	void operationFinished(int succeeded, int failed);

	void operationLog(QtMsgType level, const QString &message);

	/// Aggregated MediaMuster Trash locations reported by this run.
	void mediaMusterTrashUsed(const QString &trashFolderPath, int fileCount);
	void trashFallbackRequested(quint64 requestId, const QVector<OperationTrashFallbackItem> &items);
	void trashFallbackFinished(quint64 requestId);

private:
	// MARK: - OpSink (the runner's reporting channel)

	void progress(const QString &name, int current, int total, double pct) override;
	void log(QtMsgType level, const QString &message) override;
	void trashUsed(const QString &folder, int count) override;
	bool confirmTrashFallback(const QVector<OperationTrashFallbackItem> &items) override;

	void result(const OperationResult &value) override;
	void startRun(OperationRequest request);
	bool m_undoEnabled = false;
	bool m_running = false;
	mutable QMutex m_trashFallbackMutex;
	QWaitCondition m_trashFallbackChanged;
	bool m_trashFallbackHandlerAvailable = false;
	quint64 m_nextTrashFallbackRequest = 0;
	quint64 m_pendingTrashFallbackRequest = 0;
	bool m_trashFallbackAnswered = false;
	bool m_trashFallbackAccepted = false;

	/// Keep last: its destructor joins the worker before other members are destroyed.
	BackgroundJob m_job;
};
