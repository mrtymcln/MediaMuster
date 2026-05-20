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

// MARK: - Construction

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

	// Populate the picker with the available volumes, sorted
	// alphabetically, then select the volume the caller asked for.
	QStringList labels = m_mxfRootsByLabel.keys();
	std::sort(labels.begin(), labels.end());
	{
		const QSignalBlocker blocker(m_volumePicker);
		for (const QString &lbl : labels)
			m_volumePicker->addItem(lbl);
		int idx = labels.indexOf(initialLabel);
		if (idx < 0)
			idx = 0;
		m_volumePicker->setCurrentIndex(idx);
	}

	recomputePlan();
}

// MARK: - Window lifecycle

void RebalanceDialog::showEvent(QShowEvent *event)
{
	QDialog::showEvent(event);
	raise();
	activateWindow();
}

// MARK: - UI layout

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

	// MARK: Volume picker row

	auto *volumeRow = new QHBoxLayout;
	volumeRow->setSpacing(8);
	volumeRow->addWidget(new QLabel(tr("Volume:")));
	m_volumePicker = new QComboBox;
	m_volumePicker->setMinimumWidth(220);
	volumeRow->addWidget(m_volumePicker);
	volumeRow->addStretch();
	root->addLayout(volumeRow);

	// MARK: Summary block (HTML)

	m_summaryLabel = new QLabel;
	m_summaryLabel->setWordWrap(true);
	m_summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_summaryLabel->setTextFormat(Qt::RichText);
	root->addWidget(m_summaryLabel);

	// MARK: Before/after table

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

	// MARK: Progress row

	auto *progRow = new QHBoxLayout;
	m_progressBar = new QProgressBar;
	m_progressBar->setVisible(false);
	m_progressLabel = new QLabel;
	m_progressLabel->setVisible(false);
	m_progressLabel->setMinimumWidth(220);
	progRow->addWidget(m_progressBar, 1);
	progRow->addWidget(m_progressLabel);
	root->addLayout(progRow);

	// MARK: Footer

	auto *footer = new QHBoxLayout;
	footer->addStretch();
	m_btnCancel = new QPushButton(tr("Cancel"));
	m_btnRebalance = new QPushButton(tr("Rebalance"));
	m_btnRebalance->setDefault(true);
	footer->addWidget(m_btnCancel);
	footer->addWidget(m_btnRebalance);
	root->addLayout(footer);

	connect(m_volumePicker, &QComboBox::currentIndexChanged,
	        this, &RebalanceDialog::onVolumeChanged);
	connect(m_btnRebalance, &QPushButton::clicked,
	        this, &RebalanceDialog::onRebalanceClicked);
	connect(m_btnCancel, &QPushButton::clicked,
	        this, &RebalanceDialog::onCancelClicked);
}

// MARK: - Planning

void RebalanceDialog::onVolumeChanged(int)
{
	// Switching volumes mid-run would orphan the rebalance —
	// double-checked here as well as via the disabled picker.
	if (m_running)
		return;
	recomputePlan();
}

void RebalanceDialog::recomputePlan()
{
	if (m_volumePicker->count() == 0)
		return;

	const QString label = m_volumePicker->currentText();
	const QString mxfRoot = m_mxfRootsByLabel.value(label);
	const QVector<MediaFile> files = m_filesByMxfRoot.value(mxfRoot);

	QApplication::setOverrideCursor(Qt::WaitCursor);
	m_currentPlan = Rebalancer::computePlan(mxfRoot, label, files);
	QApplication::restoreOverrideCursor();

	renderPlan();
}

// MARK: - Rendering

void RebalanceDialog::renderPlan()
{
	QStringList newFolderNames;
	for (const FolderId &fid : m_currentPlan.newFolders)
		newFolderNames << fid.display();

	// MARK: Compute affected-folder set

	// "Affected" = any folder that's a source or destination of
	// a move. Surfaced in the summary line.
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

	// MARK: Summary label

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

	// MARK: Rebuild the table

	m_tableView->setSortingEnabled(false);
	m_tableModel->removeRows(0, m_tableModel->rowCount());

	// In-scope first (sorted by FolderId), out-of-scope after
	// (sorted by name). Out-of-scope rows are dimmed below.
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
		// New folders get a trailing `*` so they're spottable inline.
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

	// Disable Rebalance when the plan is a no-op; tooltip explains.
	const bool hasWork = m_currentPlan.totalFiles() > 0;
	m_btnRebalance->setEnabled(hasWork && !m_running);
	m_btnRebalance->setToolTip(
	    hasWork
	        ? QString()
	        : tr("Nothing to do — every folder is already under 5,000 files."));
}

// MARK: - Execute

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
	        .arg(m_currentPlan.volumeLabel),
	    QMessageBox::Yes | QMessageBox::Cancel,
	    QMessageBox::Cancel);
	if (reply != QMessageBox::Yes)
		return;

	m_running = true;
	// Set the flag now, not in onFinished — a mid-run cancel still
	// mutates volume state, so the caller always needs a rescan.
	m_didRebalance = true;
	m_rebalancedLabel = m_currentPlan.volumeLabel;
	setBusy(true);

	m_progressBar->setRange(0, m_currentPlan.totalFiles());
	m_progressBar->setValue(0);
	m_progressBar->setVisible(true);
	m_progressLabel->setVisible(true);
	m_progressLabel->setText(tr("Starting..."));

	m_btnRebalance->setText(tr("Working..."));
	m_btnRebalance->setEnabled(false);
	m_btnCancel->setText(tr("Cancel Operation"));

	m_rebalancer->executeAsync(m_currentPlan);
}

void RebalanceDialog::onCancelClicked()
{
	if (m_running)
	{
		// Cooperative cancel — the worker checks the flag between
		// composition groups. Disable Cancel after one click.
		m_rebalancer->cancel();
		m_btnCancel->setEnabled(false);
		m_btnCancel->setText(tr("Cancelling..."));
	}
	else
	{
		reject();
	}
}

// MARK: - Worker signal handlers

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

	// Repurpose the Rebalance button as Close. Must disconnect the
	// old handler so a click doesn't try to start another plan.
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
	                    "Launch Avid Media Composer to rebuild "
	                    "the media database.")
	                     .arg(succeeded)
	               : tr("Rebalance finished with %1 succeeded, %2 failed.\n\n"
	                    "See the console for details.")
	                     .arg(succeeded)
	                     .arg(failed));
	QMessageBox::information(this, tr("Rebalance Finished"), body);
}

void RebalanceDialog::onAborted(const QString &reason)
{
	// Pre-flight refused — no files moved. Reset UI to planning
	// state so the editor can fix the block and retry.
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

// MARK: - Busy-state toggle

void RebalanceDialog::setBusy(bool busy)
{
	m_volumePicker->setEnabled(!busy);
	m_tableView->setEnabled(!busy);
}