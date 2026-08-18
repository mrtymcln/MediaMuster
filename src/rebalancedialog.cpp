#include "rebalancedialog.h"
#include "foldercard.h"
#include "formatutil.h"
#include "layoututil.h"
#include "rebalancer.h"

#include <QComboBox>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QStyleFactory>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>

// MARK: - Local helpers

namespace
{

	// Total wall-clock duration for the simulated demo run. Same value
	// regardless of file count, so massive demos and tiny ones both
	// feel responsive.
	constexpr int kDemoDurationMs = 5000;
	constexpr int kDemoTickMs = 33; // ~30 Hz, matches real worker cadence

	// Card grid columns. Fixed for the first iteration; a flow layout could
	// reflow on dialog resize, but the dialog is narrow-ish anyway.
	constexpr int kCardColumns = 3;

} // namespace

// MARK: - Construction

RebalanceDialog::RebalanceDialog(const QHash<QString, QString> &mxfRootsByLabel,
								 const QHash<QString, QVector<MediaFile>> &filesByMxfRoot,
								 const QString &initialLabel, QWidget *parent)
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
	connect(m_rebalancer, &Rebalancer::progress, this, &RebalanceDialog::onProgress);
	connect(m_rebalancer, &Rebalancer::log, this, &RebalanceDialog::logMessage);
	connect(m_rebalancer, &Rebalancer::finished, this, &RebalanceDialog::onFinished);
	connect(m_rebalancer, &Rebalancer::aborted, this, &RebalanceDialog::onAborted);

	connect(&m_planWatcher, &QFutureWatcher<RebalancePlan>::finished, this,
			&RebalanceDialog::onPlanReady);

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

// MARK: - Demo-mode construction

RebalanceDialog::RebalanceDialog(const RebalancePlan &precomputedPlan, QWidget *parent)
	: QDialog(parent),
	  m_currentPlan(precomputedPlan),
	  m_demoMode(true)
{
	setWindowTitle(tr("Rebalance (Demo)"));
	setWindowFlags(windowFlags() | Qt::Tool);
	setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
	setMinimumSize(720, 540);
	resize(860, 640);

	// No Rebalancer connection needed; execution is simulated.
	m_demoTimer = new QTimer(this);
	m_demoTimer->setInterval(kDemoTickMs);
	connect(m_demoTimer, &QTimer::timeout, this, &RebalanceDialog::onDemoTick);

	setupUi();

	// Single picker item labelled with the demo scenario.
	{
		const QSignalBlocker blocker(m_volumePicker);
		m_volumePicker->addItem(precomputedPlan.volumeLabel);
		m_volumePicker->setCurrentIndex(0);
		m_volumePicker->setEnabled(false);
	}

	// Render straight away; no compute step.
	renderPlan();
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
	// Consistent paragraph margins + 140% line-height for breathing
	// room. Qt's QLabel only honours inline CSS, not stylesheet rules.
	const QString paraStyle = QStringLiteral("margin: 0 0 10px 0; line-height: 140%;");
	const QString lastParaStyle = QStringLiteral("margin: 0; line-height: 140%;");
	intro->setText(tr("<p style='%1'>Avid performance can degrade once a "
					  "MediaFiles folder holds more than 5,000 files.</p>"
					  "<p style='%2'>Rebalance keeps each clip's relatives together "
					  "— and in a Nexis environment, each workstation's folders "
					  "stay separate.</p>")
					   .arg(paraStyle, lastParaStyle));
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

	// MARK: Summary line

	m_statsLine = new QLabel;
	m_statsLine->setAlignment(Qt::AlignCenter);
	m_statsLine->setTextFormat(Qt::RichText);
	{
		QFont f = m_statsLine->font();
		f.setPointSize(f.pointSize() + 2);
		m_statsLine->setFont(f);
	}
	root->addWidget(m_statsLine);

	// MARK: Folder card grid

	m_cardContainer = new QWidget;
	m_cardGrid = new QGridLayout(m_cardContainer);
	m_cardGrid->setContentsMargins(4, 4, 4, 4);
	m_cardGrid->setSpacing(8);
	m_cardGrid->setAlignment(Qt::AlignTop);
	// Equal column stretches so the 3 cards in a row spread evenly
	// across the grid width.
	for (int c = 0; c < kCardColumns; ++c)
		m_cardGrid->setColumnStretch(c, 1);

	m_cardScroll = new QScrollArea;
	m_cardScroll->setWidget(m_cardContainer);
	m_cardScroll->setWidgetResizable(true);
	m_cardScroll->setFrameShape(QFrame::NoFrame);
	// Always-visible vanilla scrollbar with arrow buttons. The
	// macOS native style hides scrollbars on auto-fade and skips
	// arrows entirely; swapping just the scrollbar to Fusion gives
	// us a chunky always-on bar with top/bottom arrow buttons that
	// behaves the same on every platform. Parent the QStyle to the
	// dialog so it cleans up with us.
	m_cardScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
	if (auto *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
	{
		fusion->setParent(this);
		m_cardScroll->verticalScrollBar()->setStyle(fusion);
	}
	root->addWidget(m_cardScroll, 1);

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

	connect(m_volumePicker, &QComboBox::currentIndexChanged, this,
			&RebalanceDialog::onVolumeChanged);
	connect(m_btnRebalance, &QPushButton::clicked, this, &RebalanceDialog::onRebalanceClicked);
	connect(m_btnCancel, &QPushButton::clicked, this, &RebalanceDialog::onCancelClicked);
}

// MARK: - Planning

void RebalanceDialog::onVolumeChanged(int)
{
	// Switching volumes mid-run would orphan the rebalance;
	// double-checked here as well as via the disabled picker.
	if (m_running)
		return;
	recomputePlan();
}

void RebalanceDialog::recomputePlan()
{
	if (m_demoMode || m_volumePicker->count() == 0)
		return;

	const QString label = m_volumePicker->currentText();
	const QString mxfRoot = m_mxfRootsByLabel.value(label);
	const QVector<MediaFile> files = m_filesByMxfRoot.value(mxfRoot);

	// Lock the card grid + Rebalance button until the new plan lands.
	// Volume picker stays live so the user can flip volumes; a
	// fresh setFuture() silently displaces any in-flight compute.
	m_cardScroll->setEnabled(false);
	m_btnRebalance->setEnabled(false);
	m_statsLine->setText(tr("Computing plan..."));
	// Standard palette text, matching the summary and balanced states.
	m_statsLine->setStyleSheet(QString());

	// Capture by value so the pool task is independent of dialog
	// lifetime. computePlan stat-walks every subfolder, which can
	// take seconds on a slow network volume, so it runs off
	// the main thread.
	m_planWatcher.setFuture(QtConcurrent::run(
		[mxfRoot, label, files]
		{ return Rebalancer::computePlan(mxfRoot, label, files); }));
}

void RebalanceDialog::onPlanReady()
{
	m_currentPlan = m_planWatcher.result();
	m_cardScroll->setEnabled(true);
	renderPlan();
}

// MARK: - Rendering

void RebalanceDialog::renderPlan()
{
	QStringList newFolderNames;
	for (const FolderId &fid : m_currentPlan.newFolders)
		newFolderNames << fid.display();

	// MARK: Compute affected-folder set

	const QSet<FolderId> affected = affectedFolders();

	// MARK: Summary line

	if (m_currentPlan.totalFiles() == 0)
	{
		m_statsLine->setText(tr("The Force is balanced..."));
		// Standard palette text, matching the "N files moving..." summary and
		// the "Computing plan..." state on the same shared label.
		m_statsLine->setStyleSheet(QString());
	}
	else
	{
		buildSummaryLine(m_currentPlan.totalFiles(), affected.size(), newFolderNames.size(),
						 /*past=*/false);
	}

	// MARK: Rebuild the card grid

	LayoutUtil::clearLayout(m_cardGrid);
	m_cards.clear();

	// In-scope first (sorted by FolderId), out-of-scope after
	// (sorted by name).
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

	int row = 0;
	int col = 0;
	for (const FolderState &fs : sorted)
	{
		auto *card = new FolderCard(m_cardContainer);
		card->setFolder(fs);
		m_cardGrid->addWidget(card, row, col);
		if (fs.inScope)
			m_cards.insert(fs.id, card);

		if (++col >= kCardColumns)
		{
			col = 0;
			++row;
		}
	}

	// Disable Rebalance when the plan is a no-op. The stats line already
	// reads "The Force is balanced..." in that state, so no tooltip is needed.
	const bool hasWork = m_currentPlan.totalFiles() > 0;
	m_btnRebalance->setEnabled(hasWork && !m_running);
}

// MARK: - Execute

void RebalanceDialog::onRebalanceClicked()
{
	if (m_running)
		return;
	if (m_currentPlan.totalFiles() == 0)
		return;

	if (!m_demoMode)
	{
		// Verb-labelled action button ("Rebalance") so the choice is legible
		// without re-reading the body; Cancel stays default for the big move.
		QMessageBox confirm(this);
		confirm.setIcon(QMessageBox::Question);
		confirm.setWindowTitle(tr("Confirm Rebalance"));
		confirm.setText(tr("This will move %1 file(s) and create %2 new folder(s) on '%3'.\n\n"
						   "Quit Avid Media Composer first — it must not have these files "
						   "open. Avid will rebuild its media database on next project open.")
							.arg(Format::count(m_currentPlan.totalFiles()),
								 Format::count(m_currentPlan.newFolders.size()),
								 m_currentPlan.volumeLabel));
		auto *goBtn = confirm.addButton(tr("Rebalance"), QMessageBox::AcceptRole);
		confirm.addButton(QMessageBox::Cancel);
		confirm.setDefaultButton(QMessageBox::Cancel);
		confirm.exec();
		if (confirm.clickedButton() != goBtn)
			return;

	}

	m_running = true;
	// Set the flag now, not in onFinished; a mid-run cancel still
	// mutates volume state, so the caller always needs a rescan.
	m_didRebalance = true;
	m_rebalancedLabel = m_currentPlan.volumeLabel;
	setBusy(true);

	m_progressBar->setRange(0, m_currentPlan.totalFiles());
	m_progressBar->setValue(0);
	m_progressBar->setVisible(true);
	m_progressLabel->setVisible(true);
	m_progressLabel->setText(tr("Starting..."));

	m_btnRebalance->setText(tr("Rebalancing..."));
	m_btnRebalance->setEnabled(false);
	m_btnCancel->setText(tr("Cancel"));

	primeLiveState();

	if (m_demoMode)
	{
		m_demoElapsed.start();
		m_demoTimer->start();
	}
	else
	{
		m_rebalancer->executeAsync(m_currentPlan);
	}
}

void RebalanceDialog::onCancelClicked()
{
	if (m_running)
	{
		if (m_demoMode)
		{
			// Demo cancel: stop the timer, fire finished with the
			// progress reached so far.
			m_demoTimer->stop();
			const int done = m_progressBar->value();
			onFinished(done, 0, /*cancelled=*/true);
		}
		else
		{
			// Cooperative cancel: the worker checks the flag between
			// relatives groups. Disable Cancel after one click.
			m_rebalancer->cancel();
			m_btnCancel->setEnabled(false);
			m_btnCancel->setText(tr("Cancelling..."));
		}
	}
	else
	{
		reject();
	}
}

void RebalanceDialog::reject()
{
	// Esc during a run takes the Cancel button's path: cooperative
	// cancel, dialog stays up. Letting QDialog::reject tear the dialog
	// down mid-run would silently cancel the rebalance and stall the UI
	// while ~Rebalancer joins its worker.
	if (m_running)
	{
		onCancelClicked();
		return;
	}
	QDialog::reject();
}

void RebalanceDialog::onDemoTick()
{
	const qint64 elapsed = m_demoElapsed.elapsed();
	const int total = m_currentPlan.totalFiles();
	const double frac = qMin(1.0, double(elapsed) / kDemoDurationMs);
	const int current = int(frac * total);

	onProgress(current, total, tr("(simulated)"));

	if (frac >= 1.0)
	{
		m_demoTimer->stop();
		onFinished(total, 0, /*cancelled=*/false);
	}
}

// MARK: - Worker signal handlers

void RebalanceDialog::onProgress(int current, int total, const QString &detail)
{
	if (total > 0)
		m_progressBar->setRange(0, total);
	m_progressBar->setValue(current);
	m_progressLabel->setText(
		QStringLiteral("%1 / %2  %3").arg(Format::count(current), Format::count(total), detail));

	// Push the running counts forward to match the ops that have
	// completed since the last tick, and refresh the affected cards.
	applyOpsUpTo(current);
}

void RebalanceDialog::onFinished(int succeeded, int failed, bool cancelled)
{
	m_running = false;
	setBusy(false);

	m_progressLabel->setText(cancelled ? tr("Cancelled — %1 moved, %2 failed")
											 .arg(Format::count(succeeded), Format::count(failed))
									   : tr("Done — %1 moved, %2 failed")
											 .arg(Format::count(succeeded), Format::count(failed)));

	// Repaint the cards in past tense with the actual succeeded
	// count.
	const QSet<FolderId> affected = affectedFolders();
	buildSummaryLine(succeeded, affected.size(), m_currentPlan.newFolders.size(), /*past=*/true);

	// Snap every card to its final projected state and drop the
	// in-flight delta label so the dialog reads as 'done'.
	for (auto it = m_cards.constBegin(); it != m_cards.constEnd(); ++it)
		it.value()->markFinished();

	// Repurpose the Rebalance button as Close. Must disconnect the
	// old handler so a click doesn't try to start another plan.
	m_btnRebalance->setText(tr("Close"));
	m_btnRebalance->setEnabled(true);
	disconnect(m_btnRebalance, &QPushButton::clicked, this, &RebalanceDialog::onRebalanceClicked);
	connect(m_btnRebalance, &QPushButton::clicked, this, &QDialog::accept);
	m_btnCancel->setVisible(false);

	// No completion popup: the cards snap to their final counts, the summary
	// line flips to past tense, and the progress label shows "Done — N moved,
	// N failed", so the outcome is fully reported inline. (The reminder that
	// Avid rebuilds its database on next launch lives in the intro, which stays
	// visible.) A modal acknowledgement would only add a click.
}

void RebalanceDialog::onAborted(const QString &reason)
{
	// Pre-flight refused; no files moved. Reset UI to planning
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
	m_cardScroll->setEnabled(!busy);
}

// MARK: - Live rebalance tracking

void RebalanceDialog::primeLiveState()
{
	m_lastProcessedOp = 0;
	m_runningCount.clear();
	m_srcFolderByOp.clear();
	m_srcFolderByOp.reserve(m_currentPlan.ops.size());

	// Seed running counts from each folder's starting state.
	for (const auto &fs : m_currentPlan.folders)
	{
		if (fs.inScope)
			m_runningCount.insert(fs.id, fs.count);
	}

	// Pre-parse each op's source FolderId once. The hot path (live
	// updates) reuses this without re-parsing srcPath.
	for (const auto &op : m_currentPlan.ops)
		m_srcFolderByOp.append(Rebalancer::srcFolderOf(op.srcPath).value_or(FolderId{}));
}

QSet<FolderId> RebalanceDialog::affectedFolders() const
{
	QSet<FolderId> affected;
	for (const MoveOp &op : m_currentPlan.ops)
	{
		affected.insert(op.dest);
		if (const auto src = Rebalancer::srcFolderOf(op.srcPath))
			affected.insert(*src);
	}
	return affected;
}

void RebalanceDialog::applyOpsUpTo(int upTo)
{
	if (upTo <= m_lastProcessedOp)
		return;
	upTo = qMin(upTo, int(m_currentPlan.ops.size()));

	// Walk just the new ops, accumulate per-folder deltas, then push
	// final counts to the cards in one pass. Avoids touching the same
	// card N times when many ops on the same folder arrive together.
	QSet<FolderId> touched;
	for (int i = m_lastProcessedOp; i < upTo; ++i)
	{
		const FolderId &src = m_srcFolderByOp[i];
		const FolderId &dest = m_currentPlan.ops[i].dest;
		// n == 0 is the unparsed-source sentinel from primeLiveState's
		// value_or(FolderId{}); real Avid folders are numbered from 1, so a
		// parsed source always has n >= 1. Skip the sentinel so an unparseable
		// source can't decrement a stray running-count entry.
		if (src.n > 0)
		{
			--m_runningCount[src];
			touched.insert(src);
		}
		++m_runningCount[dest];
		touched.insert(dest);
	}
	m_lastProcessedOp = upTo;

	for (const FolderId &fid : touched)
	{
		if (auto *card = m_cards.value(fid))
			card->setCurrentCount(m_runningCount.value(fid));
	}
}

// MARK: - Summary line builder

void RebalanceDialog::buildSummaryLine(int files, int foldersAffected, int newFolders, bool past)
{
	const QString filesCaption =
		past ? (files == 1 ? tr("file moved") : tr("files moved"))
			 : (files == 1 ? tr("file moving") : tr("files moving"));
	const QString affectedCaption =
		foldersAffected == 1 ? tr("folder affected") : tr("folders affected");
	const QString newCaption =
		newFolders == 1 ? tr("new folder") : tr("new folders");

	const QString sep = QStringLiteral("&nbsp;&nbsp;|&nbsp;&nbsp;");
	const QString text =
		QStringLiteral("<b>%1</b> %2%3<b>%4</b> %5%6<b>%7</b> %8")
			.arg(Format::count(files), filesCaption, sep, Format::count(foldersAffected),
				 affectedCaption, sep, Format::count(newFolders), newCaption);
	m_statsLine->setText(text);
	m_statsLine->setStyleSheet({});
}