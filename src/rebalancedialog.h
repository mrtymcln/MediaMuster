#pragma once

#include "mediafile.h"
#include "rebalanceplanner.h"

#include <QDialog>
#include <QElapsedTimer>
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
class QTimer;
class Rebalancer;

// MARK: - RebalanceDialog

/// UI for the `Rebalancer`: volume picker, a grid of before/after
/// folder cards, warnings. Rebalance runs async; dialog stays open through
/// progress and final summary.
///
/// Two state phases:
///
///   - **Planning**: the volume picker computes a fresh plan via
///     `RebalancePlanner::computePlan` and the table reflects what *would*
///     happen. No disk changes yet.
///
///   - **Running**: Rebalance is in flight. The picker is disabled,
///     the progress bar tracks execution, and Cancel turns
///     into a cooperative cancel for the worker.
///
/// On close, callers can read `didRebalance()` to know whether a run
/// started (so the mainwindow can rescan even after cancellation).
class RebalanceDialog : public QDialog
{
	Q_OBJECT
	friend class TestOperationUi;

public:
	RebalanceDialog(const QHash<QString, QString> &mxfRootsByLabel,
					const QHash<QString, QVector<MediaFile>> &filesByMxfRoot,
					const QString &initialLabel, QWidget *parent = nullptr);

	/// Debug ▸ Rebalance demos: a dialog over a synthetic plan, skipping
	/// the disk scan. Clicking Rebalance runs a fake QTimer-driven
	/// progress sequence instead of touching disk. Shows the visual
	/// states to beta testers without a real Avid project.
	enum class DemoScenario
	{
		Small,	  ///< ~50 files between 4 folders.
		Big,	  ///< ~7,500 files, red/amber bars, 3 new folders.
		ReallyBig ///< 666,666 files across hundreds of folders — a
				  ///< rendering-path stress test; also fun to watch.
	};
	static RebalanceDialog *createDemo(DemoScenario scenario, QWidget *parent = nullptr);

	// MARK: - Result accessors

	/// True if the user confirmed and the rebalancer ran (even if
	/// some moves failed). False on dismiss without acting.
	bool didRebalance() const { return m_didRebalance; }

	/// Label of the volume that was rebalanced; the caller uses
	/// this to know which volume's scan needs refreshing.
	QString rebalancedLabel() const { return m_rebalancedLabel; }

	/// MainWindow resolves any previous interrupted job immediately before
	/// this plan starts. False leaves this plan unstarted.
	std::function<bool()> beforeRebalance;

signals:

	/// Forwarded from `Rebalancer::log` so the mainwindow can
	/// mirror our log lines into the console.
	void logMessage(QtMsgType level, const QString &message);

protected:
	void showEvent(QShowEvent *event) override;

	/// Esc maps here. Mid-run it must behave exactly like the Cancel
	/// button (cooperative cancel, dialog stays up); QDialog::reject
	/// would otherwise tear down a dialog whose worker is still moving
	/// files — a silent cancel plus a UI stall while ~Rebalancer joins
	/// the thread. When idle, closes as normal.
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

	/// Demo-mode tick. Advances fake progress proportional to
	/// elapsed time and fires onFinished when the run is done.
	void onDemoTick();

private:
	/// Demo-mode constructor behind createDemo(): shows `precomputedPlan`
	/// directly, no Rebalancer wired.
	explicit RebalanceDialog(const RebalancePlan &precomputedPlan, QWidget *parent = nullptr);

	void setupUi();
	void recomputePlan();
	void renderPlan();
	void setBusy(bool busy);
	void finishDisplay(int succeeded,
					   const QHash<FolderName, RebalancePlanner::FolderCount> &counts);

	/// Rebuild the inline summary line from explicit counts.
	/// `past=true` shifts captions to 'moved' / 'files moved' for
	/// the post-run summary.
	void buildSummaryLine(int files, int foldersAffected, int newFolders, bool past);

	/// Every folder that's a source or destination of a planned move in
	/// `m_currentPlan` — used for the preview and final folder recount.
	QSet<FolderName> affectedFolders() const;

	QHash<QString, QString> m_mxfRootsByLabel;
	QHash<QString, QVector<MediaFile>> m_filesByMxfRoot;

	RebalancePlan m_currentPlan;

	/// Drives computePlan off the UI thread. setFuture() replaces an
	/// in-flight planning future silently; when the user flips
	/// volumes rapidly, only the latest result ever fires onPlanReady.
	QFutureWatcher<RebalancePlan> m_planWatcher;

	Rebalancer *m_rebalancer = nullptr;
	bool m_running = false;
	bool m_didRebalance = false;
	QString m_rebalancedLabel;

	// MARK: - Demo mode

	/// True when this dialog was constructed with a precomputed
	/// plan. Disables the real disk scan + worker execution.
	bool m_demoMode = false;
	QTimer *m_demoTimer = nullptr;
	QElapsedTimer m_demoElapsed;

	// MARK: - Widgets

	QComboBox *m_volumePicker = nullptr;

	/// Single-line summary above the card grid. Holds three states:
	/// the inline stats ("<b>N</b> files moving | …"), the
	/// "Computing plan…" busy hint, and the "Already balanced"
	/// empty state. State swap = setText + setStyleSheet only.
	QLabel *m_statsLine = nullptr;

	/// Scrollable container for the folder cards. renderPlan() wipes
	/// the grid and rebuilds it; live updates touch only the affected
	/// cards by FolderName lookup.
	QScrollArea *m_cardScroll = nullptr;
	QWidget *m_cardContainer = nullptr;
	QGridLayout *m_cardGrid = nullptr;
	QHash<FolderName, FolderCard *> m_cards;

	QProgressBar *m_progressBar = nullptr;
	QLabel *m_progressLabel = nullptr;
	QPushButton *m_btnRebalance = nullptr;
	QPushButton *m_btnCancel = nullptr;

	// MARK: - Live rebalance tracking

	/// Running per-folder counts. Initialised from FolderState.count
	/// at rebalance start; decremented/incremented as ops complete.
	QHash<FolderName, int> m_runningCount;

	/// Remove each planned source after its confirmed move, so a result
	/// cannot be counted twice and engine group order need not match the plan.
	QHash<QString, FolderName> m_pendingSources;
	QSet<FolderName> m_changedFolders;
	int m_confirmedMoves = 0;
	int m_nextDemoOp = 0;

	/// Seed the counts and pending sources from the current plan.
	void primeLiveState();
	void applyMove(const FolderName &from, const FolderName &to);
};
