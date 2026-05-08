#include "rebalancedialog.h"
#include "formatutil.h"
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
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>

RebalanceDialog::RebalanceDialog(
	const QHash<QString, QString> &mxfRootsByLabel,
	const QHash<QString, QVector<MediaFile>> &filesByMxfRoot,
	const QString &initialLabel,
	QWidget *parent)
	: QDialog(parent),
	  m_mxfRootsByLabel(mxfRootsByLabel),
	  m_filesByMxfRoot(filesByMxfRoot)
{
	setWindowTitle(tr("Rebalance"));
	setWindowFlags(windowFlags() | Qt::Tool);
	setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
	setMinimumSize(720, 540);
	resize(860, 640);

	m_rebalancer = new Rebalancer(this);
	connect(m_rebalancer, &Rebalancer::progress,
			this, &RebalanceDialog::onProgress);
	connect(m_rebalancer, &Rebalancer::log,
			this, &RebalanceDialog::logMessage);
	connect(m_rebalancer, &Rebalancer::finished,
			this, &RebalanceDialog::onFinished);
	connect(m_rebalancer, &Rebalancer::aborted,
			this, &RebalanceDialog::onAborted);

	setupUi();

	QStringList labels = m_mxfRootsByLabel.keys();
	std::sort(labels.begin(), labels.end());
	{
		const QSignalBlocker blocker(m_drivePicker);
		for (const QString &lbl : labels)
			m_drivePicker->addItem(lbl);
		int idx = labels.indexOf(initialLabel);
		if (idx < 0)
			idx = 0;
		m_drivePicker->setCurrentIndex(idx);
	}

	recomputePlan();
}

void RebalanceDialog::showEvent(QShowEvent *event)
{
	QDialog::showEvent(event);
	raise();
	activateWindow();
}

void RebalanceDialog::setupUi()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(16, 16, 16, 16);
	root->setSpacing(12);

	auto *intro = new QLabel;
	intro->setWordWrap(true);
	intro->setTextFormat(Qt::RichText);
	intro->setText(tr(
		"<p style='margin-top:0'>Avid recommends keeping each MediaFiles "
		"folder under 5,000 files. Rebalance the files on the selected "
		"volume for better performance.</p>"
		"<p>A clip's relatives are kept together: first placed into an "
		"existing folder with free space, and only into a new folder "
		"when all have reached capacity.</p>"
		"<p>In a Nexis environment, each host writes into its own folder "
		"set — Rebalance will keep those sets separate.</p>"
		"<p style='margin-bottom:0'>Avid will update its media database "
		"to reflect the new locations on next launch.</p>"));
	root->addWidget(intro);

	auto *driveRow = new QHBoxLayout;
	driveRow->setSpacing(8);
	driveRow->addWidget(new QLabel(tr("Volume:")));
	m_drivePicker = new QComboBox;
	m_drivePicker->setMinimumWidth(220);
	driveRow->addWidget(m_drivePicker);
	driveRow->addStretch();
	root->addLayout(driveRow);

	m_summaryLabel = new QLabel;
	m_summaryLabel->setWordWrap(true);
	m_summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_summaryLabel->setTextFormat(Qt::RichText);
	root->addWidget(m_summaryLabel);

	m_tableModel = new QStandardItemModel(this);
	m_tableModel->setHorizontalHeaderLabels(
		QStringList() << tr("Folder") << tr("Current") << tr("Projected")
					  << tr("Out") << tr("In") << tr("Delta"));
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
	m_btnRebalance = new QPushButton(tr("Rebalance"));
	m_btnRebalance->setDefault(true);
	footer->addWidget(m_btnCancel);
	footer->addWidget(m_btnRebalance);
	root->addLayout(footer);

	connect(m_drivePicker, &QComboBox::currentIndexChanged,
			this, &RebalanceDialog::onDriveChanged);
	connect(m_btnRebalance, &QPushButton::clicked,
			this, &RebalanceDialog::onRebalanceClicked);
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
				 .arg(Format::bytes(m_currentPlan.totalBytes()));
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
	const QString dash = QStringLiteral("—");
	for (const FolderState &fs : sorted)
	{
		const QString name = fs.isNew ? (fs.name + QStringLiteral(" *")) : fs.name;
		const int projected = fs.count + fs.filesIn - fs.filesOut;
		const qint64 deltaBytes = fs.bytesIn - fs.bytesOut;

		QList<QStandardItem *> row{
			new QStandardItem(name),
			new QStandardItem(QString::number(fs.count)),
			new QStandardItem(fs.inScope ? QString::number(projected) : dash),
			new QStandardItem(fs.inScope ? QString::number(fs.filesOut) : dash),
			new QStandardItem(fs.inScope ? QString::number(fs.filesIn) : dash),
			new QStandardItem(fs.inScope ? Format::bytesSigned(deltaBytes) : dash),
		};

		for (QStandardItem *cell : row)
		{
			cell->setEditable(false);
			if (!fs.inScope)
				cell->setForeground(dimmed);
		}
		for (int i = 1; i < row.size(); ++i)
			row[i]->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
		m_tableModel->appendRow(row);
	}
	m_tableView->setSortingEnabled(true);

	const bool hasWork = m_currentPlan.totalFiles() > 0;
	m_btnRebalance->setEnabled(hasWork && !m_running);
	m_btnRebalance->setToolTip(
		hasWork
			? QString()
			: tr("Nothing to do — every folder is already under 5,000 files."));
}

void RebalanceDialog::onRebalanceClicked()
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
	m_didRebalance = true;
	m_rebalancedLabel = m_currentPlan.driveLabel;
	setBusy(true);

	m_progressBar->setRange(0, m_currentPlan.totalFiles());
	m_progressBar->setValue(0);
	m_progressBar->setVisible(true);
	m_progressLabel->setVisible(true);
	m_progressLabel->setText(tr("Starting…"));

	m_btnRebalance->setText(tr("Working…"));
	m_btnRebalance->setEnabled(false);
	m_btnCancel->setText(tr("Cancel Operation"));

	m_rebalancer->executeAsync(m_currentPlan);
}

void RebalanceDialog::onCancelClicked()
{
	if (m_running)
	{
		m_rebalancer->cancel();
		m_btnCancel->setEnabled(false);
		m_btnCancel->setText(tr("Cancelling…"));
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

void RebalanceDialog::onFinished(int succeeded, int failed, bool cancelled)
{
	m_running = false;
	setBusy(false);

	m_progressLabel->setText(
		cancelled ? tr("Cancelled — %1 moved, %2 failed")
					   .arg(succeeded)
					   .arg(failed)
				 : tr("Done — %1 moved, %2 failed")
					   .arg(succeeded)
					   .arg(failed));

	m_btnRebalance->setText(tr("Close"));
	m_btnRebalance->setEnabled(true);
	disconnect(m_btnRebalance, &QPushButton::clicked,
			   this, &RebalanceDialog::onRebalanceClicked);
	connect(m_btnRebalance, &QPushButton::clicked,
			this, &QDialog::accept);
	m_btnCancel->setVisible(false);

	const QString body =
		cancelled
			? tr("Rebalance was cancelled.\n\n%1 file(s) moved, %2 failed.")
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

	m_btnRebalance->setText(tr("Rebalance"));
	m_btnRebalance->setEnabled(m_currentPlan.totalFiles() > 0);
	m_btnCancel->setEnabled(true);
	m_btnCancel->setText(tr("Cancel"));

	QMessageBox::warning(this, tr("Rebalance Aborted"), reason);
}

void RebalanceDialog::setBusy(bool busy)
{
	m_drivePicker->setEnabled(!busy);
	m_tableView->setEnabled(!busy);
}