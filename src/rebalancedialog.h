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

class RebalanceDialog : public QDialog
{
	Q_OBJECT
public:
	RebalanceDialog(const QHash<QString, QString> &mxfRootsByLabel,
					const QHash<QString, QVector<MediaFile>> &filesByMxfRoot,
					const QString &initialLabel,
					QWidget *parent = nullptr);

	bool didRebalance() const { return m_didRebalance; }

	QString rebalancedLabel() const { return m_rebalancedLabel; }

signals:
	void logMessage(int level, const QString &message);

protected:
	void showEvent(QShowEvent *event) override;

private slots:
	void onDriveChanged(int idx);
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

	QComboBox *m_drivePicker = nullptr;
	QLabel *m_summaryLabel = nullptr;
	QStandardItemModel *m_tableModel = nullptr;
	QTableView *m_tableView = nullptr;
	QProgressBar *m_progressBar = nullptr;
	QLabel *m_progressLabel = nullptr;
	QPushButton *m_btnRebalance = nullptr;
	QPushButton *m_btnCancel = nullptr;
};