#include "rebalanceplanner.h"
#include "rebalancedialog.h"
#include "opjournal.h"
#include "conventions.h"
#include "formatutil.h"
#include "layoututil.h"
#include "rebalancer.h"

#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPixmap>
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
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>

// MARK: - Local helpers

namespace
{

	// Fixed number of columns in the folder-card grid.
	constexpr int kCardColumns = 3;

	// MARK: - Card painting helpers

	QColor capColor(int fileCount)
	{
		if (fileCount >= Conventions::kFolderCritical)
			return QColor(0xff, 0x3b, 0x30);
		if (fileCount >= Conventions::kFolderWarn)
			return QColor(0xff, 0x95, 0x00);
		return QColor(0x34, 0xc7, 0x59);
	}

	QBrush makeStripeBrush(const QColor &color)
	{
		constexpr int kTile = 8;
		QPixmap tile(kTile, kTile);
		tile.fill(Qt::transparent);

		QPainter p(&tile);
		p.setRenderHint(QPainter::Antialiasing);
		QPen pen(color, 3.0);
		pen.setCapStyle(Qt::FlatCap);
		p.setPen(pen);
		p.drawLine(0, kTile, kTile, 0);
		p.end();

		return QBrush(tile);
	}

	QBrush makeAquaBrush(const QColor &base, const QRect &rect)
	{
		QLinearGradient grad(rect.topLeft(), rect.bottomLeft());
		grad.setColorAt(0.00, base.lighter(140));
		grad.setColorAt(0.45, base);
		grad.setColorAt(1.00, base.darker(115));
		return QBrush(grad);
	}

	void paintAquaGloss(QPainter &p, const QRect &rect, int radius)
	{
		QRect top = rect;
		top.setHeight(rect.height() / 2);
		QLinearGradient grad(top.topLeft(), top.bottomLeft());
		grad.setColorAt(0.0, QColor(255, 255, 255, 110));
		grad.setColorAt(1.0, QColor(255, 255, 255, 0));
		p.setBrush(grad);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(top, radius, radius);
	}

	// Cards have a fixed height, but fluid width.
	constexpr int kCardMinWidth = 200;
	constexpr int kCardHeight = 100;
	constexpr int kPad = 12;
	constexpr int kBarHeight = 14;
	constexpr int kBarRadius = 4;

} // namespace

// MARK: - FolderCard
//
// One folder's name, capacity bar and before/after counts.

class FolderCard : public QFrame
{
public:
	explicit FolderCard(QWidget *parent = nullptr);
	void setFolder(const FolderState &fs);
	void setCurrentCount(int current);
	void markFinished(int count, bool exists);
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	QString countCaption() const;
	QString m_mediaFolderName;
	bool m_inScope = true;
	bool m_isNew = false;
	bool m_finished = false;
	bool m_exists = true;
	int m_currentCount = 0;
	int m_projectedCount = 0;
	int m_initialCount = 0;
};

FolderCard::FolderCard(QWidget *parent)
	: QFrame(parent)
{
	setObjectName(QStringLiteral("folderCard"));
	setFrameShape(QFrame::StyledPanel);
	setStyleSheet(QStringLiteral("QFrame#folderCard { border: 1px solid palette(mid); "
								 "border-radius: 6px; background: palette(base); }"));
	setFixedHeight(kCardHeight);
	setMinimumWidth(kCardMinWidth);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void FolderCard::setFolder(const FolderState &fs)
{
	m_mediaFolderName = fs.mediaFolderName;
	setAccessibleName(m_mediaFolderName);
	m_inScope = fs.inScope;
	m_isNew = fs.isNew;
	m_finished = false;
	m_exists = !fs.isNew;
	m_currentCount = fs.count;
	m_initialCount = fs.count;
	m_projectedCount = fs.count + fs.filesIn - fs.filesOut;
	setAccessibleDescription(countCaption());
	update();
}

void FolderCard::setCurrentCount(int current)
{
	if (current == m_currentCount)
		return;
	m_currentCount = current;
	setAccessibleDescription(countCaption());
	update();
}

void FolderCard::markFinished(int count, bool exists)
{
	m_currentCount = count;
	m_projectedCount = count;
	m_exists = exists;
	m_finished = true;
	setAccessibleDescription(countCaption());
	update();
}

QString FolderCard::countCaption() const
{
	if (m_currentCount < 0)
		return tr("Unavailable");
	if (m_finished && !m_exists)
		return m_isNew ? tr("Not created") : tr("Missing");
	if (!m_inScope || m_currentCount == m_projectedCount)
		return Format::count(m_currentCount);
	return QStringLiteral("%1 → %2").arg(Format::count(m_currentCount),
										 Format::count(m_projectedCount));
}

QSize FolderCard::sizeHint() const
{
	return {kCardMinWidth, kCardHeight};
}

QSize FolderCard::minimumSizeHint() const
{
	return {kCardMinWidth, kCardHeight};
}

void FolderCard::paintEvent(QPaintEvent *event)
{
	QFrame::paintEvent(event);

	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::TextAntialiasing);

	const QRect content = rect().adjusted(kPad, kPad - 2, -kPad, -kPad + 2);
	int y = content.top();

	// MARK: Folder name

	QFont nameFont = font();
	nameFont.setBold(true);
	nameFont.setPointSize(nameFont.pointSize() + 1);
	const QFontMetrics fmName(nameFont);
	p.setFont(nameFont);
	p.setPen(palette().color(QPalette::WindowText));
	// ⚠️ for bloated folders.
	// 🆕 for new folders.
	QString displayName = m_mediaFolderName;
	if (m_inScope && qMax(m_currentCount, m_projectedCount) > Conventions::kFolderMax)
		displayName = QStringLiteral("⚠️ ") + displayName;
	if (m_isNew && (!m_finished || m_exists))
		displayName = QStringLiteral("🆕 ") + displayName;
	p.drawText(QRect(content.left(), y, content.width(), fmName.height()),
			   Qt::AlignLeft | Qt::AlignVCenter,
			   fmName.elidedText(displayName, Qt::ElideRight, content.width()));
	y += fmName.height() + 6;

	// MARK: Capacity bar

	const QRect track(content.left(), y, content.width(), kBarHeight);
	if (m_inScope)
	{
		// `barFrac` maps file counts onto the visual scale.
		const auto barFrac = [](int n)
		{ return qBound(0.0, double(n) / Conventions::kFolderMax, 1.0); };

		// Track shade derived from the theme's text colour: dark on
		// light backgrounds, light on dark backgrounds.
		p.setPen(Qt::NoPen);
		QColor trackColor = palette().color(QPalette::WindowText);
		trackColor.setAlpha(70);
		p.setBrush(trackColor);
		p.drawRoundedRect(track, kBarRadius, kBarRadius);

		const int settledEnd = int(track.width() * barFrac(qMin(m_currentCount, m_projectedCount)));
		const int outerEnd = int(track.width() * barFrac(qMax(m_currentCount, m_projectedCount)));
		const QColor fillColor = capColor(qMax(m_currentCount, m_projectedCount));
		p.save();
		QPainterPath barClip;
		barClip.addRoundedRect(track, kBarRadius, kBarRadius);
		p.setClipPath(barClip);

		if (settledEnd > 0)
		{
			QRect solid(track.x(), track.y(), settledEnd, track.height());
			p.fillRect(solid, makeAquaBrush(fillColor, solid));
			paintAquaGloss(p, solid, 0);
		}

		if (outerEnd > settledEnd)
		{
			QRect ghost(track.x() + settledEnd, track.y(), outerEnd - settledEnd, track.height());
			p.fillRect(ghost, makeStripeBrush(fillColor));
		}

		p.restore();
	}
	y += kBarHeight + 8;

	// MARK: Count caption

	QFont countFont = font();
	const QFontMetrics fmCount(countFont);
	p.setFont(countFont);
	p.setPen(palette().color(QPalette::WindowText));

	p.drawText(QRect(content.left(), y, content.width(), fmCount.height()),
			   Qt::AlignLeft | Qt::AlignVCenter, countCaption());
	y += fmCount.height() + 2;

	// MARK: Delta caption

	QFont deltaFont = font();
	deltaFont.setPointSize(qMax(8, deltaFont.pointSize() - 1));
	const QFontMetrics fmDelta(deltaFont);
	p.setFont(deltaFont);
	QColor secondaryText = palette().color(QPalette::WindowText);
	secondaryText.setAlpha(160);
	p.setPen(secondaryText);

	QString deltaText;
	if (m_finished)
	{
		// Suppress the delta caption after rebalancing.
	}
	else if (!m_inScope)
	{
		deltaText = tr("out of scope");
	}
	else
	{
		const int df = m_projectedCount - m_initialCount;
		if (df == 0)
			deltaText = tr("no change");
		else
			// %n drives the plural; the leading sign is a symbol, not translatable.
			deltaText = df > 0 ? tr("+%n file(s)", nullptr, df) : tr("−%n file(s)", nullptr, -df);
	}
	if (!deltaText.isEmpty())
	{
		p.drawText(QRect(content.left(), y, content.width(), fmDelta.height()),
				   Qt::AlignLeft | Qt::AlignVCenter,
				   fmDelta.elidedText(deltaText, Qt::ElideRight, content.width()));
	}
}

// MARK: - Construction

RebalanceDialog::RebalanceDialog(const QHash<QString, QString> &mxfRootPathsByLabel,
								 const QHash<QString, QVector<MediaFile>> &filesByMxfRootPath,
								 const QString &initialLabel, QWidget *parent)
	: QDialog(parent),
	  m_mxfRootPathsByLabel(mxfRootPathsByLabel),
	  m_filesByMxfRootPath(filesByMxfRootPath)
{
	setWindowTitle(tr("Rebalance"));
	setWindowFlags(windowFlags() | Qt::Tool);
	setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
	setMinimumSize(720, 540);
	resize(860, 640);

	m_rebalancer = new Rebalancer(this);
	connect(m_rebalancer, &Rebalancer::progress, this, &RebalanceDialog::onProgress);
	connect(m_rebalancer, &Rebalancer::operationResult, this, &RebalanceDialog::onOperationResult);
	connect(m_rebalancer, &Rebalancer::log, this, &RebalanceDialog::logMessage);
	connect(m_rebalancer, &Rebalancer::finished, this, &RebalanceDialog::onFinished);
	connect(m_rebalancer, &Rebalancer::aborted, this, &RebalanceDialog::onAborted);

	connect(&m_planWatcher, &QFutureWatcher<RebalancePlan>::finished, this,
			&RebalanceDialog::onPlanReady);

	setupUi();

	// Populate the picker with the available volumes, sorted
	// alphabetically, then select the volume the caller asked for.
	QStringList labels = m_mxfRootPathsByLabel.keys();
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
	root->setContentsMargins(20, 20, 20, 20);
	root->setSpacing(12);

	auto *intro = new QLabel;
	intro->setWordWrap(true);
	intro->setTextFormat(Qt::RichText);
	// Consistent paragraph margins + 140% line-height for breathing
	// room. QLabel only honours inline CSS, not stylesheet rules.
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
	if (m_volumePicker->count() == 0)
		return;

	const QString label = m_volumePicker->currentText();
	const QString mxfRootPath = m_mxfRootPathsByLabel.value(label);
	const QVector<MediaFile> files = m_filesByMxfRootPath.value(mxfRootPath);

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
		[mxfRootPath, label, files]
		{ return RebalancePlanner::computePlan(mxfRootPath, label, files); }));
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
	for (const NumberedMxfFolder &fid : m_currentPlan.newFolders)
		newFolderNames << fid.display();

	// MARK: Compute affected-folder set

	const QSet<NumberedMxfFolder> affected = affectedFolders();

	// MARK: Summary line

	if (m_currentPlan.moveCount() == 0)
	{
		m_statsLine->setText(tr("The Force is balanced..."));
		// Standard palette text, matching the "N files moving..." summary and
		// the "Computing plan..." state on the same shared label.
		m_statsLine->setStyleSheet(QString());
	}
	else
	{
		buildSummaryLine(m_currentPlan.moveCount(), affected.size(), newFolderNames.size(),
						 /*past=*/false);
	}

	// MARK: Rebuild the card grid

	LayoutUtil::clearLayout(m_cardGrid);
	m_cards.clear();

	// Eligible folders first, ordered by prefix and number; other folders by name.
	QVector<FolderState> sorted = m_currentPlan.folders;
	std::sort(sorted.begin(), sorted.end(),
			  [](const FolderState &a, const FolderState &b)
			  {
				  if (a.inScope != b.inScope)
					  return a.inScope > b.inScope;
				  if (!a.inScope)
					  return a.mediaFolderName < b.mediaFolderName;
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
	const bool hasWork = m_currentPlan.moveCount() > 0;
	m_btnRebalance->setEnabled(hasWork && !m_running);
}

// MARK: - Execute

void RebalanceDialog::onRebalanceClicked()
{
	if (m_running)
		return;
	if (m_currentPlan.moveCount() == 0)
		return;

	// Verb-labelled action button ("Rebalance") so the choice is legible
	// without re-reading the body; Cancel stays default for the big move.
	QMessageBox confirm(this);
	confirm.setIcon(QMessageBox::Question);
	confirm.setWindowTitle(tr("Confirm Rebalance"));
	confirm.setText(tr("This will move %1 file(s) and create %2 new folder(s) on '%3'.\n\n"
					   "Quit Avid Media Composer first — it must not have these files "
					   "open. Avid will rebuild its media database on next project open.")
						.arg(Format::count(m_currentPlan.moveCount()),
							 Format::count(m_currentPlan.newFolders.size()),
							 m_currentPlan.volumeLabel));
	auto *goBtn = confirm.addButton(tr("Rebalance"), QMessageBox::AcceptRole);
	confirm.addButton(QMessageBox::Cancel);
	confirm.setDefaultButton(QMessageBox::Cancel);
	confirm.exec();
	if (confirm.clickedButton() != goBtn)
		return;
	if (beforeRebalance && !beforeRebalance())
		return;

	m_running = true;
	// Set the flag now, not in onFinished; a mid-run cancel still
	// mutates volume state, so the caller always needs a rescan.
	m_didRebalance = true;
	m_rebalancedLabel = m_currentPlan.volumeLabel;
	setBusy(true);

	m_progressBar->setRange(0, m_currentPlan.moveCount());
	m_progressBar->setValue(0);
	m_progressBar->setVisible(true);
	m_progressLabel->setVisible(true);
	m_progressLabel->setText(tr("Starting..."));

	m_btnRebalance->setText(tr("Rebalancing..."));
	m_btnRebalance->setEnabled(false);
	m_btnCancel->setText(tr("Cancel"));

	primeLiveState();

	m_rebalancer->executeAsync(m_currentPlan);
}

void RebalanceDialog::onCancelClicked()
{
	if (m_running)
	{
		// Cooperative cancel: the worker checks the flag between
		// relatives groups. Disable Cancel after one click.
		m_rebalancer->cancel();
		m_btnCancel->setEnabled(false);
		m_btnCancel->setText(tr("Cancelling..."));
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

// MARK: - Worker signal handlers

void RebalanceDialog::onProgress(int current, int total, const QString &detail)
{
	if (total > 0)
		m_progressBar->setRange(0, total);
	m_progressBar->setValue(current);
	m_progressLabel->setText(
		QStringLiteral("%1 / %2  %3").arg(Format::count(current), Format::count(total), detail));
}

void RebalanceDialog::onOperationResult(const OpResult &result)
{
	if (!result.sourceRemoved)
		return;
	const auto source = m_pendingSources.constFind(result.source);
	const auto destination = RebalancePlanner::srcFolderOf(result.destination);
	if (source == m_pendingSources.cend() || !destination)
		return;

	const NumberedMxfFolder from = source.value();
	m_pendingSources.remove(result.source);
	applyMove(from, *destination);
}

void RebalanceDialog::applyMove(const NumberedMxfFolder &from, const NumberedMxfFolder &to)
{
	--m_runningCount[from];
	++m_runningCount[to];
	++m_confirmedMoves;
	m_changedFolders.insert(from);
	m_changedFolders.insert(to);
	for (const auto &folder : {from, to})
		if (auto *card = m_cards.value(folder))
			card->setCurrentCount(m_runningCount.value(folder));
}

void RebalanceDialog::onFinished(int succeeded, int failed, bool cancelled)
{
	m_running = false;
	m_btnCancel->setVisible(false);
	m_btnRebalance->setEnabled(false);
	m_statsLine->setText(tr("Updating folder counts…"));
	m_progressLabel->setText(cancelled ? tr("Cancelled — %1 moved, %2 failed")
											 .arg(Format::count(succeeded), Format::count(failed))
									   : tr("Done — %1 moved, %2 failed")
											 .arg(Format::count(succeeded), Format::count(failed)));
	m_progressBar->setValue(m_confirmedMoves);

	// OpManager has joined the operation worker before finished. Count only
	// directory entries here: a move can land before a later journal/sync
	// error, so even confirmed-result deltas are not a final disk snapshot.
	using Counts = QHash<NumberedMxfFolder, RebalancePlanner::FolderCount>;
	auto *watcher = new QFutureWatcher<Counts>(this);
	connect(watcher, &QFutureWatcher<Counts>::finished, this,
			[this, watcher, succeeded]
			{
				finishDisplay(succeeded, watcher->result());
				watcher->deleteLater();
			});
	const QString mxfRootPath = m_currentPlan.mxfRootPath;
	const QSet<NumberedMxfFolder> folders = affectedFolders();
	// Value captures let the read-only task finish safely if the dialog closes.
	watcher->setFuture(QtConcurrent::run([mxfRootPath, folders]
										 { return RebalancePlanner::countFolders(mxfRootPath, folders); }));
}

void RebalanceDialog::finishDisplay(
	int succeeded, const QHash<NumberedMxfFolder, RebalancePlanner::FolderCount> &counts)
{
	setBusy(false);

	QSet<NumberedMxfFolder> changed = m_changedFolders;
	int newFolders = 0;
	bool affectedUnknown = false;
	bool newFoldersUnknown = false;
	for (const auto &folder : m_currentPlan.folders)
	{
		if (!folder.inScope)
			continue;
		const auto actual = counts.value(folder.id, {m_runningCount.value(folder.id), !folder.isNew});
		m_runningCount[folder.id] = actual.count;
		if (auto *card = m_cards.value(folder.id))
			card->markFinished(actual.count, actual.exists);
		if (actual.count < 0)
		{
			affectedUnknown = true;
			newFoldersUnknown |= folder.isNew;
			continue;
		}
		if (actual.count != folder.count || (folder.isNew && actual.exists))
			changed.insert(folder.id);
		if (folder.isNew && actual.exists)
			++newFolders;
	}
	buildSummaryLine(succeeded, affectedUnknown ? -1 : changed.size(),
					 newFoldersUnknown ? -1 : newFolders, /*past=*/true);

	m_btnRebalance->setText(tr("Close"));
	m_btnRebalance->setEnabled(true);
	disconnect(m_btnRebalance, &QPushButton::clicked, this, &RebalanceDialog::onRebalanceClicked);
	connect(m_btnRebalance, &QPushButton::clicked, this, &QDialog::accept);
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
	m_btnRebalance->setEnabled(m_currentPlan.moveCount() > 0);
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
	m_confirmedMoves = 0;
	m_runningCount.clear();
	m_pendingSources.clear();
	m_changedFolders.clear();
	for (const auto &folder : m_currentPlan.folders)
		if (folder.inScope)
			m_runningCount.insert(folder.id, folder.count);
	QHash<QString, QString> canonicalParents;
	for (const auto &op : m_currentPlan.ops)
		if (const auto folder = RebalancePlanner::srcFolderOf(op.srcPath))
		{
			// Match the engine's canonical source paths, including volume
			// aliases. Resolve once per folder, not once per media file.
			const QFileInfo source(op.srcPath);
			const QString parent = source.absolutePath();
			if (!canonicalParents.contains(parent))
				canonicalParents.insert(parent, QFileInfo(OpJournal::canonicalPath(op.srcPath)).absolutePath());
			const QString sourcePath = QDir(canonicalParents.value(parent)).filePath(source.fileName());
			m_pendingSources.insert(sourcePath, *folder);
		}
}

QSet<NumberedMxfFolder> RebalanceDialog::affectedFolders() const
{
	QSet<NumberedMxfFolder> affected;
	for (const RebalanceMove &op : m_currentPlan.ops)
	{
		affected.insert(op.dest);
		if (const auto src = RebalancePlanner::srcFolderOf(op.srcPath))
			affected.insert(*src);
	}
	return affected;
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
			.arg(Format::count(files), filesCaption, sep,
				 foldersAffected < 0 ? tr("Unknown") : Format::count(foldersAffected),
				 affectedCaption, sep,
				 newFolders < 0 ? tr("Unknown") : Format::count(newFolders), newCaption);
	m_statsLine->setText(text);
	m_statsLine->setStyleSheet({});
}
