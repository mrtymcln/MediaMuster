#pragma once

#include "backgroundjob.h"
#include "mediafile.h"
#include "oprequest.h"
#include "oprunner.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QVector>
#include <QWaitCondition>

#include <functional>
#include <optional>

// Qt facade for the shared engine. Requests run on one owned worker; results
// reach the GUI through queued signals. The runner also holds the journal lock
// so another facade or recovery pass cannot change files concurrently.
class OpManager : public QObject, private OpSink
{
	Q_OBJECT
	friend class TestOperationUi;

public:
	explicit OpManager(QObject *parent = nullptr);
	~OpManager() override;

	// MARK: - Job entry points

	/// Dispatch Copy, Move, Trash or Rebalance, preserving the selected items.
	void execute(OpRequest request);

	void setUndoEnabled(bool enabled) { m_undoEnabled = enabled; }
	bool isRunning() const { return m_running; }

	void cancel();

	// The UI explicitly registers its lifetime so a headless manager never
	// waits for a decision that nobody can supply. Replies are request-scoped:
	// an old dialog cannot authorize a later operation.
	void setTrashFallbackHandlerAvailable(bool available);
	bool isTrashFallbackPending(quint64 requestId) const;
	void respondTrashFallback(quint64 requestId, bool accepted);

	/// The engine's entire read of a MediaFile, in one place: path,
	/// name, folder, size, the per-file conflict policy, and the scan's
	/// Avid identity claims (mob ids + clip name) that the runner
	/// cross-checks and the journal records.
	static QVector<OpItem> itemsFromMediaFiles(const QVector<MediaFile> &files,
											   const QHash<QString, ConflictPolicy> &policies);

	/// Rename runs only: called on the WORKER thread after the first
	/// successful rename touching each folder (the engine's own Avid-
	/// database reset for that folder has already run). The Rebalance
	/// adapter wires its summary counting here; unset means no extra
	/// action. Set before dispatching; not thread-safe to change mid-run.
	std::function<void(const QString &folderPath)> renameFolderTouched;

signals:
	void operationResult(const OpResult &result);

	// MARK: - Progress signals (consumed via QueuedConnection)

	/// pct is 0-100 for the file currently copying. A same-volume Move
	/// (pure rename) and Delete have no per-byte progress and emit 0.
	void operationProgress(const QString &fileName, int current, int total, double pct);

	/// Exactly once per run, including cancel.
	void operationFinished(int succeeded, int failed);

	void operationLog(QtMsgType level, const QString &message);

	/// Aggregated locations used by Delete in this run.
	void mediaMusterTrashUsed(const QString &trashFolderPath, int fileCount);
	void trashFallbackRequested(quint64 requestId, const QVector<OpTrashFallbackItem> &items);
	void trashFallbackFinished(quint64 requestId);

private:
	// MARK: - OpSink (the runner's reporting channel)

	void progress(const QString &name, int current, int total, double pct) override;
	void log(QtMsgType level, const QString &message) override;
	void trashUsed(const QString &folder, int count) override;
	bool confirmTrashFallback(const QVector<OpTrashFallbackItem> &items) override;

	void result(const OpResult &value) override;
	void startRun(OpRequest request);
	bool m_undoEnabled = false;
	bool m_running = false;
	mutable QMutex m_trashFallbackMutex;
	QWaitCondition m_trashFallbackChanged;
	bool m_trashFallbackHandlerAvailable = false;
	quint64 m_nextTrashFallbackRequest = 0;
	quint64 m_pendingTrashFallbackRequest = 0;
	bool m_trashFallbackAnswered = false;
	bool m_trashFallbackAccepted = false;

	/// Must stay the LAST member: BackgroundJob's destructor joins the
	/// worker, and members declared after it would be destroyed first —
	/// out from under a still-running worker.
	BackgroundJob m_job;
};
