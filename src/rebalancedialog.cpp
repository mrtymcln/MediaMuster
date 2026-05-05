#include "rebalancedialog.h"
#include "rebalancer.h"

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
	QString fmtBytes(qint64 b)
	{
		if (b >= static_cast<qint64>(1024) * 1024 * 1024 * 1024)
			return QString("%1 TB")
				.arg(b / (1024.0 * 1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
		if (b >= static_cast<qint64>(1024) * 1024 * 1024)
			return QString("%1 GB").arg(b / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
		if (b >= 1024 * 1024)
			return QString("%1 MB").arg(b / (1024.0 * 1024.0), 0, 'f', 1);
		if (b >= 1024)
			return QString("%1 KB").arg(b / 1024.0, 0, 'f', 1);
		return QString("%1 B").arg(b);
	}

	QString fmtBytesSigned(qint64 b)
	{
		if (b == 0)
			return QStringLiteral("—");
		const QString sign = (b > 0 ? QStringLiteral("+") : QStringLiteral("−"));
		return sign + fmtBytes(qAbs(b));
	}
} // namespace

RebalanceDialog::RebalanceDialog(
	const QHash<QString, QString> &mxfRootsByLabel,
	const QHash<QString, QVector<MediaFile>> &filesByMxfRoot,
	const QString &initialLabel,
	QWidget *parent)
	: QDialog(parent),
	  m_mxfRootsByLabel(mxfRootsByLabel),
	  m_filesByMxfRoot(filesByMxfRoot)
{
	setWindowTitle(tr("Rebalance MXF Folders"));
	setMinimumSize(720, 540);
	resize(860, 640);

	m_rebalancer = new Rebalancer(this);
	connect(m_rebalancer, &Rebalancer::progress,
			this, &RebalanceDialog::onProgress);
	connect(m_rebalancer, &Rebalancer::log,
			this, &RebalanceDialog::onLog);
	connect(m_rebalancer, &Rebalancer::finished,
			this, &RebalanceDialog::onFinished);
	connect(m_rebalancer, &Rebalancer::aborted,
			this, &RebalanceDialog::onAborted);

	setupUi();

	QStringList labels = m_mxfRootsByLabel.keys();
	std::sort(labels.begin(), labels.end());
	for (const QString &lbl : labels)
		m_drivePicker->addItem(lbl);
	int idx = labels.indexOf(initialLabel);
	if (idx < 0)
		idx = 0;
	m_drivePicker->setCurrentIndex(idx);

	recomputePlan();
}

void RebalanceDialog::setupUi()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(16, 16, 16, 16);
	root->setSpacing(12);

	auto *driveRow = new QHBoxLayout;
	driveRow->setSpacing(8);
	driveRow->addWidget(new QLabel(tr("Drive:")));
	m_drivePicker = new QComboBox;
	m_drivePicker->setMinimumWidth(220);
	driveRow->addWidget(m_drivePicker);
	driveRow->addStretch();
	root->addLayout(driveRow);

	auto *intro = new QLabel(tr(
		"Avid Media Composer recommends keeping each MediaFiles folder "
		"under 5,000 files. Rebalance redistributes MXF files within "
		"this drive only, creating new folders as needed. Avid will "
		"rebuild its media database on next project open."));
	intro->setWordWrap(true);
	intro->setStyleSheet("color:#666;");
	root->addWidget(intro);

	m_summaryLabel = new QLabel;
	m_summaryLabel->setWordWrap(true);
	m_summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_summaryLabel->setTextFormat(Qt::RichText);
	root->addWidget(m_summaryLabel);

	m_tableModel = new QStandardItemModel(this);
	m_tableModel->setHorizontalHeaderLabels(
		QStringList() << tr("Folder") << tr("Current") << tr("Projected")
					  << tr("Out") << tr("In") << tr("ΔBytes"));
	m_tableView = new QTableView;
	m_tableView->setModel(m_tableModel);
	m_tableView->setSelectionMode(QAbstractItemView::NoSelection);
	m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_tableView->setAlternatingRowColors(true);
	m_tableView->setSortingEnabled(true);
	m_tableView->verticalHeader()->setVisible(false);
	m_tableView->horizontalHeader()->setStretchLastSection(true);
	m_tableView->horizontalHeader()->setSectionResizeMode(
		0, QHeaderView::ResizeToContents);
	root->addWidget(m_tableView, 1);

	auto *progRow = new QHBoxLayout;
	m_progressBar = new QProgressBar;
	m_progressBar->setVisible(false);
	m_progressLabel = new QLabel;
	m_progressLabel->setVisible(false);
	m_progressLabel->setMinimumWidth(220);
	progRow->addWidget(m_progressBar, 1);
	progRow->addWidget(m_progressLabel);
	root->addLayout(progRow);

	auto *footer = new QHBoxLayout;
	footer->addStretch();
	m_btnCancel = new QPushButton(tr("Cancel"));
	m_btnExecute = new QPushButton(tr("Execute Rebalance"));
	m_btnExecute->setDefault(true);
	footer->addWidget(m_btnCancel);
	footer->addWidget(m_btnExecute);
	root->addLayout(footer);

	connect(m_drivePicker,
			QOverload<int>::of(&QComboBox::currentIndexChanged),
			this, &RebalanceDialog::onDriveChanged);
	connect(m_btnExecute, &QPushButton::clicked,
			this, &RebalanceDialog::onExecuteClicked);
	connect(m_btnCancel, &QPushButton::clicked,
			this, &RebalanceDialog::onCancelClicked);
}

void RebalanceDialog::onDriveChanged(int)
{
	if (m_running)
		return;
	recomputePlan();
}

void RebalanceDialog::recomputePlan()
{
	if (m_drivePicker->count() == 0)
		return;

	const QString label = m_drivePicker->currentText();
	const QString mxfRoot = m_mxfRootsByLabel.value(label);
	const QVector<MediaFile> files = m_filesByMxfRoot.value(mxfRoot);

	QApplication::setOverrideCursor(Qt::WaitCursor);
	m_currentPlan = Rebalancer::computePlan(mxfRoot, label, files);
	QApplication::restoreOverrideCursor();

	renderPlan();
}

void RebalanceDialog::renderPlan()
{
	QStringList newFolderNames;
	for (const FolderId &fid : m_currentPlan.newFolders)
		newFolderNames << fid.display();

	QSet<FolderId> affected;
	for (const MoveOp &op : m_currentPlan.ops)
	{
		affected.insert(op.dest);
		const QString srcDirName = QFileInfo(
									   QFileInfo(op.srcPath).absolutePath())
									   .fileName();
		auto fid = Rebalancer::parseFolderName(srcDirName);
		if (fid)
			affected.insert(*fid);
	}

	QStringList lines;
	lines << tr("<b>Total files to move:</b> %1")
				 .arg(m_currentPlan.totalFiles());
	lines << tr("<b>Total bytes:</b> %1")
				 .arg(fmtBytes(m_currentPlan.totalBytes()));
	lines << tr("<b>New folders:</b> %1%2")
				 .arg(newFolderNames.size())
				 .arg(newFolderNames.isEmpty()
						  ? QString()
						  : QString("&nbsp;&nbsp;(%1)")
								.arg(newFolderNames.join(", ")));
	lines << tr("<b>Folders affected:</b> %1").arg(affected.size());
	if (!m_currentPlan.warnings.isEmpty())
	{
		lines << tr("<b style='color:#c44'>Warnings (%1):</b>")
					 .arg(m_currentPlan.warnings.size());
		for (const QString &w : m_currentPlan.warnings)
			lines << QStringLiteral("&nbsp;&nbsp;• ") + w.toHtmlEscaped();
	}
	m_summaryLabel->setText(lines.join("<br>"));

	m_tableView->setSortingEnabled(false);
	m_tableModel->removeRows(0, m_tableModel->rowCount());

	QVector<FolderState> sorted = m_currentPlan.folders;
	std::sort(sorted.begin(), sorted.end(),
			  [](const FolderState &a, const FolderState &b)
			  {
				  if (a.inScope != b.inScope)
					  return a.inScope > b.inScope;
				  if (!a.inScope)
					  return a.name < b.name;
				  return a.id < b.id;
			  });

	const QBrush dimmed(QColor(150, 150, 150));
	for (const FolderState &fs : sorted)
	{
		const QString name = fs.isNew
								 ? (fs.name + QStringLiteral(" *"))
								 : fs.name;
		const int projected = fs.count + fs.filesIn - fs.filesOut;
		const qint64 deltaBytes = fs.bytesIn - fs.bytesOut;
		const QString dash = QStringLiteral("—");

		QList<QStandardItem *> row;
		row << new QStandardItem(name);
		row << new QStandardItem(QString::number(fs.count));
		row << new QStandardItem(
			fs.inScope ? QString::number(projected) : dash);
		row << new QStandardItem(
			fs.inScope ? QString::number(fs.filesOut) : dash);
		row << new QStandardItem(
			fs.inScope ? QString::number(fs.filesIn) : dash);
		row << new QStandardItem(
			fs.inScope ? fmtBytesSigned(deltaBytes) : dash);

		for (QStandardItem *cell : row)
		{
			cell->setEditable(false);
			if (!fs.inScope)
				cell->setForeground(dimmed);
		}
		// Right-align numeric columns.
		for (int i = 1; i < row.size(); ++i)
			row[i]->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
		m_tableModel->appendRow(row);
	}
	m_tableView->setSortingEnabled(true);

	const bool hasWork = m_currentPlan.totalFiles() > 0;
	m_btnExecute->setEnabled(hasWork && !m_running);
	m_btnExecute->setToolTip(
		hasWork
			? QString()
			: tr("Nothing to do — every folder is already under 5,000 files."));
}

void RebalanceDialog::onExecuteClicked()
{
	if (m_running)
		return;
	if (m_currentPlan.totalFiles() == 0)
		return;

	const auto reply = QMessageBox::question(
		this, tr("Confirm Rebalance"),
		tr("This will move %1 file(s) and create %2 new folder(s) on '%3'.\n\n"
		   "Quit Avid Media Composer first — it must not have these files "
		   "open. Avid will rebuild its media database on next project open.\n\n"
		   "Continue?")
			.arg(m_currentPlan.totalFiles())
			.arg(m_currentPlan.newFolders.size())
			.arg(m_currentPlan.driveLabel),
		QMessageBox::Yes | QMessageBox::Cancel,
		QMessageBox::Cancel);
	if (reply != QMessageBox::Yes)
		return;

	m_running = true;
	m_didExecute = true;
	m_executedLabel = m_currentPlan.driveLabel;
	setBusy(true);

	m_progressBar->setRange(0, m_currentPlan.totalFiles());
	m_progressBar->setValue(0);
	m_progressBar->setVisible(true);
	m_progressLabel->setVisible(true);
	m_progressLabel->setText(tr("Starting…"));

	m_btnExecute->setText(tr("Working…"));
	m_btnExecute->setEnabled(false);
	m_btnCancel->setText(tr("Cancel Operation"));

	m_rebalancer->executeAsync(m_currentPlan);
}

void RebalanceDialog::onCancelClicked()
{
	if (m_running)
	{
		m_rebalancer->cancel();
		m_btnCancel->setEnabled(false);
		m_btnCancel->setText(tr("Canceling…"));
	}
	else
	{
		reject();
	}
}

void RebalanceDialog::onProgress(int current, int total,
								 const QString &detail)
{
	if (total > 0)
		m_progressBar->setRange(0, total);
	m_progressBar->setValue(current);
	m_progressLabel->setText(
		tr("%1 / %2  %3").arg(current).arg(total).arg(detail));
}

void RebalanceDialog::onLog(int /*level*/, const QString & /*msg*/)
{
	// Future log view hook.
}

void RebalanceDialog::onFinished(int succeeded, int failed, bool canceled)
{
	m_running = false;
	setBusy(false);

	m_progressLabel->setText(
		canceled ? tr("Canceled — %1 moved, %2 failed")
					   .arg(succeeded)
					   .arg(failed)
				 : tr("Done — %1 moved, %2 failed")
					   .arg(succeeded)
					   .arg(failed));

	m_btnExecute->setText(tr("Close"));
	m_btnExecute->setEnabled(true);
	disconnect(m_btnExecute, &QPushButton::clicked,
			   this, &RebalanceDialog::onExecuteClicked);
	connect(m_btnExecute, &QPushButton::clicked,
			this, &QDialog::accept);
	m_btnCancel->setVisible(false);

	const QString body =
		canceled
			? tr("Rebalance was canceled.\n\n%1 file(s) moved, %2 failed.")
				  .arg(succeeded)
				  .arg(failed)
			: (failed == 0
				   ? tr("Rebalance completed successfully — %1 file(s) moved.\n\n"
						"Open the project in Avid Media Composer to rebuild "
						"its media database.")
						 .arg(succeeded)
				   : tr("Rebalance finished with %1 succeeded, %2 failed.\n\n"
						"See the main console for details.")
						 .arg(succeeded)
						 .arg(failed));
	QMessageBox::information(this, tr("Rebalance Finished"), body);
}

void RebalanceDialog::onAborted(const QString &reason)
{
	m_running = false;
	setBusy(false);

	m_progressBar->setVisible(false);
	m_progressLabel->setVisible(false);

	m_btnExecute->setText(tr("Execute Rebalance"));
	m_btnExecute->setEnabled(m_currentPlan.totalFiles() > 0);
	m_btnCancel->setEnabled(true);
	m_btnCancel->setText(tr("Cancel"));

	QMessageBox::warning(this, tr("Rebalance Aborted"), reason);
}

void RebalanceDialog::setBusy(bool busy)
{
	m_drivePicker->setEnabled(!busy);
	m_tableView->setEnabled(!busy);
}