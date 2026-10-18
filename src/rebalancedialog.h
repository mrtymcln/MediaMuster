#pragma once

#include "mediafile.h"
#include "rebalanceplanner.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>
#include <functional>

class FolderCard;
class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QShowEvent;
class Rebalancer;

// MARK: - RebalanceDialog

/// Previews folder changes, then displays asynchronous Rebalance progress and results.
/// During execution, keep the volume fixed and treat Cancel as a cooperative request.
class RebalanceDialog : public QDialog
{
	Q_OBJECT
	friend class TestOperationUi;

public:
	RebalanceDialog(const QHash<QString, QString> &mxfRootPathsByLabel,
					const QHash<QString, QVector<MediaFile>> &filesByMxfRootPath,
					const QString &initialLabel, QWidget *parent = nullptr);

	// MARK: - Result accessors

	/// A run started; the caller must rescan even after cancellation or failure.
	bool didRebalance() const { return m_didRebalance; }

	/// Identifies the volume to rescan after a run.
	QString rebalancedLabel() const { return m_rebalancedLabel; }

	/// MainWindow resolves any previous interrupted job immediately before
	/// this plan starts. False leaves this plan unstarted.
	std::function<bool()> beforeRebalance;

signals:

	/// Forward worker messages to the main-window Console.
	void logMessage(QtMsgType level, const QString &message);

protected:
	void showEvent(QShowEvent *event) override;

	/// Escape requests Cancel during a run and keeps the dialog open until completion.
	/// When idle, close normally.
	void reject() override;

private slots:
	void onVolumeChanged(int idx);
	void onRebalanceClicked();
	void onCancelClicked();
	void onPlanReady();
	void onProgress(int current, int total, const QString &detail);
	void onOperationResult(const OpResult &result);
	void onFinished(int succeeded, int failed, bool cancelled);
	void onAborted(const QString &reason);

private:
	void setupUi();
	void recomputePlan();
	void renderPlan();
	void setBusy(bool busy);
	void finishDisplay(int succeeded,
					   const QHash<NumberedMxfFolder, RebalancePlanner::FolderCount> &counts);

	/// Use past-tense captions for the post-run summary.
	void buildSummaryLine(int files, int foldersAffected, int newFolders, bool past);

	/// Source and destination folders needing preview or final recounts.
	QSet<NumberedMxfFolder> affectedFolders() const;

	QHash<QString, QString> m_mxfRootPathsByLabel;
	QHash<QString, QVector<MediaFile>> m_filesByMxfRootPath;

	RebalancePlan m_currentPlan;

	/// Watch only the latest background plan when the selected volume changes.
	QFutureWatcher<RebalancePlan> m_planWatcher;

	Rebalancer *m_rebalancer = nullptr;
	bool m_running = false;
	bool m_didRebalance = false;
	QString m_rebalancedLabel;

	// MARK: - Widgets

	QComboBox *m_volumePicker = nullptr;

	/// Shows the plan summary, planning progress or an empty-plan message.
	QLabel *m_statsLine = nullptr;

	/// Rebuild cards for a new plan; update affected cards as moves complete.
	QScrollArea *m_cardScroll = nullptr;
	QWidget *m_cardContainer = nullptr;
	QGridLayout *m_cardGrid = nullptr;
	QHash<NumberedMxfFolder, FolderCard *> m_cards;

	QProgressBar *m_progressBar = nullptr;
	QLabel *m_progressLabel = nullptr;
	QPushButton *m_rebalanceButton = nullptr;
	QPushButton *m_cancelButton = nullptr;

	// MARK: - Live rebalance tracking

	/// Running per-folder counts. Initialised from FolderState.count
	/// at rebalance start; decremented/incremented as ops complete.
	QHash<NumberedMxfFolder, int> m_runningCount;

	/// Remove each planned source after its confirmed move, so a result
	/// cannot be counted twice and engine group order need not match the plan.
	QHash<QString, NumberedMxfFolder> m_pendingSources;
	QSet<NumberedMxfFolder> m_changedFolders;
	int m_confirmedMoves = 0;

	/// Seed the counts and pending sources from the current plan.
	void primeLiveState();
	void applyMove(const NumberedMxfFolder &from, const NumberedMxfFolder &to);
};
