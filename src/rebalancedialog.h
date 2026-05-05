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
class QStandardItemModel;
class QTableView;
class Rebalancer;

// Preview + execute UI for the rebalancer. Caller supplies label→mxfRoot and
// mxfRoot→files maps; the dialog recomputes the plan on open and on drive change.
class RebalanceDialog : public QDialog
{
	Q_OBJECT
public:
	RebalanceDialog(const QHash<QString, QString> &mxfRootsByLabel,
					const QHash<QString, QVector<MediaFile>> &filesByMxfRoot,
					const QString &initialLabel,
					QWidget *parent = nullptr);

	// True if the user started an execute; caller uses this to trigger a re-scan.
	bool didExecute() const { return m_didExecute; }

	// Drive label of the most recently rebalanced drive — tells caller which to re-scan.
	QString executedLabel() const { return m_executedLabel; }

private slots:
	void onDriveChanged(int idx);
	void onExecuteClicked();
	void onCancelClicked();
	void onProgress(int current, int total, const QString &detail);
	void onLog(int level, const QString &msg);
	void onFinished(int succeeded, int failed, bool canceled);
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
	bool m_didExecute = false;
	QString m_executedLabel;

	QComboBox *m_drivePicker = nullptr;
	QLabel *m_summaryLabel = nullptr;
	QStandardItemModel *m_tableModel = nullptr;
	QTableView *m_tableView = nullptr;
	QProgressBar *m_progressBar = nullptr;
	QLabel *m_progressLabel = nullptr;
	QPushButton *m_btnExecute = nullptr;
	QPushButton *m_btnCancel = nullptr;
};