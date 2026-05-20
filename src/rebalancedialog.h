#pragma once

#include "mediafile.h"
#include "rebalanceplan.h"

#include <QDialog>
#include <QHash>
#include <QString>
#include <QVector>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QShowEvent;
class QStandardItemModel;
class QTableView;
class Rebalancer;

// MARK: - RebalanceDialog

/// UI for the `Rebalancer`: volume picker, before/after table,
/// warnings. Rebalance runs async; dialog stays open through
/// progress and final summary.
///
/// Two state phases:
///
///   - **Planning** — the volume picker computes a fresh plan via
///     `Rebalancer::computePlan` and the table reflects what *would*
///     happen. No disk changes yet.
///
///   - **Running** — Rebalance is in flight. The picker is disabled,
///     the progress bar tracks completed moves, and Cancel turns
///     into a cooperative cancel for the worker.
///
/// On close, callers can read `didRebalance()` to know whether any
/// work was actually performed (so the mainwindow can trigger a
/// rescan of the affected volume).
class RebalanceDialog : public QDialog
{
	Q_OBJECT
public:
	RebalanceDialog(const QHash<QString, QString> &mxfRootsByLabel,
	                const QHash<QString, QVector<MediaFile>> &filesByMxfRoot,
	                const QString &initialLabel,
	                QWidget *parent = nullptr);

	// MARK: - Result accessors

	/// True if the user confirmed and the rebalancer ran (even if
	/// some moves failed). False on dismiss without acting.
	bool didRebalance() const
	{
		return m_didRebalance;
	}

	/// Label of the volume that was rebalanced — the caller uses
	/// this to know which volume's scan needs refreshing.
	QString rebalancedLabel() const
	{
		return m_rebalancedLabel;
	}

signals:

	/// Forwarded from `Rebalancer::log` so the mainwindow can
	/// mirror our log lines into the console.
	void logMessage(int level, const QString &message);

protected:
	void showEvent(QShowEvent *event) override;

private slots:
	void onVolumeChanged(int idx);
	void onRebalanceClicked();
	void onCancelClicked();
	void onProgress(int current, int total, const QString &detail);
	void onFinished(int succeeded, int failed, bool cancelled);
	void onAborted(const QString &reason);

private:
	void setupUi();
	void recomputePlan();
	void renderPlan();
	void setBusy(bool busy);

	QHash<QString, QString> m_mxfRootsByLabel;
	QHash<QString, QVector<MediaFile>> m_filesByMxfRoot;

	RebalancePlan m_currentPlan;

	Rebalancer *m_rebalancer = nullptr;
	bool m_running = false;
	bool m_didRebalance = false;
	QString m_rebalancedLabel;

	// MARK: - Widgets

	QComboBox *m_volumePicker = nullptr;
	QLabel *m_summaryLabel = nullptr;
	QStandardItemModel *m_tableModel = nullptr;
	QTableView *m_tableView = nullptr;
	QProgressBar *m_progressBar = nullptr;
	QLabel *m_progressLabel = nullptr;
	QPushButton *m_btnRebalance = nullptr;
	QPushButton *m_btnCancel = nullptr;
};