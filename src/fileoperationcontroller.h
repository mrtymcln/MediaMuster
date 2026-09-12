#pragma once

#include "operationrecovery.h"
#include "opmanager.h"

#include <QObject>
#include <QSet>

class QAction;
class ProgressDialog;
class QWidget;

// Owns the UI lifecycle of one file job: recovery, dispatch, progress and Undo.
// MainWindow supplies scan/dialog activity and applies model changes from signals.
class FileOperationController : public QObject
{
	Q_OBJECT
	friend class TestOperationUi;

public:
	enum class Activity
	{
		Idle,
		Scanning,
		Recovering,
		FileOperation,
		RebalanceDialog
	};
	Q_ENUM(Activity)
	explicit FileOperationController(QWidget *window);
	Activity activity() const { return m_activity; }
	bool isIdle() const { return m_activity == Activity::Idle; }
	void setActivity(Activity activity);
	OpManager *manager() const { return m_fileOps; }
	QAction *resumeAction() const { return m_resumeAct; }
	QAction *undoAction() const { return m_undoAct; }
	QAction *verifyCopiesAction() const { return m_verifyCopiesAct; }
	QAction *enableUndoAction() const { return m_enableUndoAct; }
	void setUndoSeparator(QAction *separator);

	void runStartupRecovery();
	void refreshHistory();
	bool dispatchRequest(OpRequest request);
	bool resolvePreviousJob();
	void offerResume();
	void undoLastOperation();
	// Temporarily releases only the modal-dialog activity for the authoritative
	// previous-job gate. A resumed job keeps its activity when the dialog closes.
	bool resolveBeforeRebalance();
	void endRebalanceDialog();

signals:
	void activityChanged(FileOperationController::Activity activity);
	void logMessage(QtMsgType level, const QString &module, const QString &message);
	void sourcesRemoved(const QSet<QString> &paths);
	void mediaMusterTrashUsed(const QString &folder, int fileCount);

private:
	void onRecoveryDone(const OperationRecovery::Summary &summary);
	void updateUndoAction();
	void updateResumeAction();
	bool confirmCrashProtection();
	bool resumeOperation(const OperationRecovery::Resumable &job);
	void applyOperationHistory(const OperationRecovery::Summary &history);
	void readOperationHistoryForGate();
	ProgressDialog *progressDialog();
	QWidget *m_window;
	OpManager *m_fileOps;
	ProgressDialog *m_progressDialog = nullptr;
	Activity m_activity = Activity::Idle;
	bool m_pruneSourceRowsAfterOperation = false;
	QSet<QString> m_removedSourcePaths;
	QVector<OperationRecovery::Resumable> m_resumable;
	QAction *m_resumeAct;
	QAction *m_undoAct;
	QAction *m_undoSeparator = nullptr;
	QAction *m_verifyCopiesAct;
	QAction *m_enableUndoAct;
	bool m_operationGateActive = false;
	bool m_historyLoading = false;
	quint64 m_historyGeneration = 0;
	struct UndoCandidate
	{
		QString path;
		QString label;
	};
	UndoCandidate m_undoCandidate;
};
