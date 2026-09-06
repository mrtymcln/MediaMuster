#include "binfilterdialog.h"
#include "dragdroputil.h"
#include "formatutil.h"

#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <utility>

// MARK: - File-local helpers

namespace
{
	constexpr int kMaxConcurrentBinLoads = 2;

	QString binExplanation(const AvbBin &bin, bool loading)
	{
		if (loading)
			return BinFilterDialog::tr("Loading…");
		if (bin.mobIds.isEmpty())
			return BinFilterDialog::tr("No media references.");
		return bin.warnings.isEmpty() ? QString() : BinFilterDialog::tr("Loaded with warnings.");
	}

	QString opLabel(BinFilterDialog::Operation op)
	{
		switch (op)
		{
		case BinFilterDialog::Operation::Intersect:
			return BinFilterDialog::tr("Intersect");
		case BinFilterDialog::Operation::Subtract:
			return BinFilterDialog::tr("Subtract");
		case BinFilterDialog::Operation::Add:
			return BinFilterDialog::tr("Add");
		}
		return {};
	}

	// Two strips share styling, so parameterise rule selectors by
	// objectName.
	QString segBarStyleSheet(const QString &objectName)
	{
		return QStringLiteral("#%1 {"
							  "  background: palette(window);"
							  "  border-top: 1px solid palette(mid);"
							  "}"
							  "#%1 QToolButton {"
							  "  border: none; padding: 0px 10px 2px 10px;"
							  "  min-width: 24px; min-height: 20px;"
							  "  color: palette(text);"
							  "  font-weight: bold;"
							  "  font-size: 18px;"
							  "}"
							  "#%1 QToolButton:hover { background: palette(midlight); }"
							  "#%1 QToolButton:pressed { background: palette(mid); }"
							  "#%1 QToolButton:disabled { color: palette(mid); }")
			.arg(objectName);
	}

	// `[ button ] [ help text ]`. Button returned by reference so the
	// caller can wire its clicked signal.
	QWidget *makeOperationRow(const QString &label, const QString &help, QPushButton *&buttonOut)
	{
		auto *row = new QWidget;
		auto *layout = new QHBoxLayout(row);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(12);

		buttonOut = new QPushButton(label);
		buttonOut->setMinimumWidth(150);
		buttonOut->setMinimumHeight(44);
		layout->addWidget(buttonOut);

		auto *helpLabel = new QLabel(help);
		helpLabel->setWordWrap(true);
		layout->addWidget(helpLabel, 1);

		return row;
	}
} // namespace

// MARK: - Construction

BinFilterDialog::BinFilterDialog(QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Filter by Bin"));
	setWindowFlags(windowFlags() | Qt::Tool);
	setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
	setModal(false);
	setAcceptDrops(true);
	m_parsePool.setMaxThreadCount(kMaxConcurrentBinLoads);
	resize(720, 640);
	setupUi();
}

BinFilterDialog::~BinFilterDialog()
{
	for (const auto &cancelled : std::as_const(m_pendingLoads))
		cancelled->store(true, std::memory_order_relaxed);
	// Workers own only their path and cancellation flag. On teardown, join
	// after signalling cancellation so the pool cannot outlive the dialog.
	m_parsePool.waitForDone();
}

// MARK: - UI layout

void BinFilterDialog::setupUi()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(16, 16, 16, 16);
	root->setSpacing(12);

	auto *intro = new QLabel(
		tr("Load your Avid Bin files here and I'll show you only the media they "
		   "reference.\nTick which bins to include, then refine the filter below."));
	// Explicit \n keeps the two sentences on their own lines; word wrap still
	// applies within each line if the dialog is ever narrowed.
	intro->setWordWrap(true);
	root->addWidget(intro);

	// Breathing room so the wrapped intro doesn't crowd the Loaded Bins group
	// heading directly beneath it.
	root->addSpacing(8);

	// MARK: Loaded Bins group

	auto *binsGroup = new QGroupBox(tr("Loaded Bins"));
	auto *binsLayout = new QVBoxLayout(binsGroup);
	auto *binsFrame = new QFrame;
	binsFrame->setFrameShape(QFrame::StyledPanel);
	binsFrame->setFrameShadow(QFrame::Sunken);
	auto *framedLayout = new QVBoxLayout(binsFrame);
	framedLayout->setContentsMargins(0, 0, 0, 0);
	framedLayout->setSpacing(0);
	m_binList = new QListWidget;
	m_binList->setObjectName(QStringLiteral("BinList"));
	m_binList->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_binList->setMinimumHeight(140);
	m_binList->setFrameShape(QFrame::NoFrame);
	framedLayout->addWidget(m_binList, 1);
	auto *segBar = new QWidget;
	segBar->setObjectName("BinFilterSegBar");
	segBar->setStyleSheet(segBarStyleSheet(QStringLiteral("BinFilterSegBar")));
	auto *segLayout = new QHBoxLayout(segBar);
	segLayout->setContentsMargins(0, 0, 0, 0);
	segLayout->setSpacing(0);
	auto *btnPlus = new QToolButton;
	// Text-mode glyphs (not emoji dingbats) so the colour follows
	// palette(text) and flips with dark mode. The segBar QSS bumps
	// the font size to keep them visually chunky.
	btnPlus->setText(QStringLiteral("+"));
	btnPlus->setToolTip(tr("Add..."));
	btnPlus->setCursor(Qt::PointingHandCursor);
	btnPlus->setAutoRaise(true);
	auto *btnMinus = new QToolButton;
	btnMinus->setText(QStringLiteral("−"));
	btnMinus->setToolTip(tr("Remove selected"));
	btnMinus->setCursor(Qt::PointingHandCursor);
	btnMinus->setAutoRaise(true);
	// Disabled until a row is selected.
	btnMinus->setEnabled(false);
	auto *vSep1 = new QFrame;
	vSep1->setFrameShape(QFrame::VLine);
	vSep1->setStyleSheet(QStringLiteral("QFrame { color: palette(mid); }"));
	auto *vSep2 = new QFrame;
	vSep2->setFrameShape(QFrame::VLine);
	vSep2->setStyleSheet(QStringLiteral("QFrame { color: palette(mid); }"));
	segLayout->addWidget(btnPlus);
	segLayout->addWidget(vSep1);
	segLayout->addWidget(btnMinus);
	segLayout->addWidget(vSep2);
	segLayout->addStretch(1);
	m_binListSummary = new QLabel(tr("0 loaded, 0 ticked"));
	m_binListSummary->setObjectName(QStringLiteral("BinListSummary"));
	m_binListSummary->setStyleSheet(
		QStringLiteral("QLabel { color: palette(placeholder-text); padding-right: 8px; }"));
	segLayout->addWidget(m_binListSummary);

	framedLayout->addWidget(segBar);
	binsLayout->addWidget(binsFrame);

	connect(btnPlus, &QToolButton::clicked, this, &BinFilterDialog::onAddBinsClicked);
	connect(btnMinus, &QToolButton::clicked, this, &BinFilterDialog::onRemoveSelectedBinsClicked);

	connect(m_binList, &QListWidget::itemSelectionChanged, this,
			[this, btnMinus]()
			{ btnMinus->setEnabled(!m_binList->selectedItems().isEmpty()); });

	root->addWidget(binsGroup);

	// MARK: Apply Operation group

	auto *opsGroup = new QGroupBox(tr("Filter Operations"));
	auto *opsLayout = new QVBoxLayout(opsGroup);
	opsLayout->setSpacing(8);

	opsLayout->addWidget(makeOperationRow(
		tr("Intersect"),
		tr("Show only the media referenced in the ticked bins."),
		m_btnIntersect));

	opsLayout->addWidget(makeOperationRow(
		tr("Subtract"),
		tr("Hide media that is referenced in the ticked bins. Useful when archiving or deleting."),
		m_btnSubtract));

	opsLayout->addWidget(makeOperationRow(
		tr("Add"),
		tr("Bring back media referenced in the ticked bins, even if an earlier step hid it."),
		m_btnAdd));
	m_btnIntersect->setObjectName(QStringLiteral("BinIntersectButton"));
	m_btnSubtract->setObjectName(QStringLiteral("BinSubtractButton"));
	m_btnAdd->setObjectName(QStringLiteral("BinAddButton"));

	root->addWidget(opsGroup);

	// MARK: Current Steps group

	// Mirrors the Loaded Bins panel: list inside a sunken frame with
	// a bottom segBar carrying the remove control and a summary
	// label. Familiar shape, familiar interactions.
	auto *chainGroup = new QGroupBox(tr("Filter Steps"));
	auto *chainLayout = new QVBoxLayout(chainGroup);

	auto *chainFrame = new QFrame;
	chainFrame->setFrameShape(QFrame::StyledPanel);
	chainFrame->setFrameShadow(QFrame::Sunken);
	auto *chainFrameLayout = new QVBoxLayout(chainFrame);
	chainFrameLayout->setContentsMargins(0, 0, 0, 0);
	chainFrameLayout->setSpacing(0);

	m_chainList = new QListWidget;
	m_chainList->setObjectName(QStringLiteral("BinChainList"));
	m_chainList->setMinimumHeight(96);
	m_chainList->setFrameShape(QFrame::NoFrame);
	chainFrameLayout->addWidget(m_chainList, 1);

	auto *chainSegBar = new QWidget;
	chainSegBar->setObjectName("ChainFilterSegBar");
	chainSegBar->setStyleSheet(segBarStyleSheet(QStringLiteral("ChainFilterSegBar")));
	auto *chainSegLayout = new QHBoxLayout(chainSegBar);
	chainSegLayout->setContentsMargins(0, 0, 0, 0);
	chainSegLayout->setSpacing(0);

	auto *btnChainRemove = new QToolButton;
	btnChainRemove->setText(QStringLiteral("−"));
	btnChainRemove->setToolTip(tr("Remove selected operation"));
	btnChainRemove->setCursor(Qt::PointingHandCursor);
	btnChainRemove->setAutoRaise(true);
	btnChainRemove->setEnabled(false);

	auto *chainSep = new QFrame;
	chainSep->setFrameShape(QFrame::VLine);
	chainSep->setStyleSheet(QStringLiteral("QFrame { color: palette(mid); }"));

	chainSegLayout->addWidget(btnChainRemove);
	chainSegLayout->addWidget(chainSep);
	chainSegLayout->addStretch(1);

	m_chainSummary = new QLabel(tr("Add bin files to get started."));
	m_chainSummary->setStyleSheet(
		QStringLiteral("QLabel { color: palette(placeholder-text); padding-right: 8px; }"));
	chainSegLayout->addWidget(m_chainSummary);

	chainFrameLayout->addWidget(chainSegBar);
	chainLayout->addWidget(chainFrame);

	connect(m_chainList, &QListWidget::itemSelectionChanged, this, [this, btnChainRemove]()
			{ btnChainRemove->setEnabled(!m_chainList->selectedItems().isEmpty()); });
	connect(btnChainRemove, &QToolButton::clicked, this,
			[this]()
			{
				auto sel = m_chainList->selectedItems();
				if (sel.isEmpty())
					return;
				const int idx = sel.first()->data(Qt::UserRole).toInt();
				onRemoveStep(idx);
			});

	root->addWidget(chainGroup);

	// MARK: Footer

	auto *footer = new QHBoxLayout;
	footer->addStretch(1);
	m_btnDone = new QPushButton(tr("Done"));
	m_btnDone->setDefault(true);
	footer->addWidget(m_btnDone);
	root->addLayout(footer);

	connect(m_btnIntersect, &QPushButton::clicked, this, &BinFilterDialog::onIntersectClicked);
	connect(m_btnSubtract, &QPushButton::clicked, this, &BinFilterDialog::onSubtractClicked);
	connect(m_btnAdd, &QPushButton::clicked, this, &BinFilterDialog::onAddClicked);
	connect(m_btnDone, &QPushButton::clicked, this, &QDialog::hide);

	// Tickbox changes drive the summary text and op-button enable state.
	connect(m_binList, &QListWidget::itemChanged, this,
			[this](QListWidgetItem *)
			{ refreshBinSelectionUi(); });

	// Prime initial state (0 loaded, buttons disabled, placeholder text).
	refreshBinSelectionUi();
}

// MARK: - UI state

void BinFilterDialog::refreshBinSelectionUi()
{
	const auto loading = std::count_if(m_bins.cbegin(), m_bins.cend(),
									   [](const LoadedBin &entry)
									   { return entry.loading; });
	const auto loaded = m_bins.size() - loading;
	const int ready = selectedBinsCount();
	QString summary = tr("%1 loaded, %2 ticked").arg(Format::count(loaded), Format::count(ready));
	if (loading > 0)
		summary += tr(", %1 loading").arg(Format::count(loading));
	m_binListSummary->setText(summary);

	// Disabled op buttons hard-couple the two halves of the dialog: users can't
	// click an op without first ticking a bin, so the 'tickbox = arms, button =
	// fires' model becomes self-evident — which is also why the disabled state
	// needs no explanatory tooltip.
	const bool canApply = ready > 0;
	m_btnIntersect->setEnabled(canApply);
	m_btnAdd->setEnabled(canApply);
	// A leading Subtract starts with all media rows, just as its help text says.
	m_btnSubtract->setEnabled(canApply);

	// Empty-chain placeholder. Single message; the intro text and
	// the bin-list summary already guide users into loading + ticking
	// bins; this slot just describes the next concrete action.
	if (m_chain.isEmpty())
		m_chainSummary->setText(tr("Tick a bin, then choose an operation above."));
}

// MARK: - Drag and drop

bool BinFilterDialog::hasAcceptedDragPath(const QMimeData *mime) const
{
	return DragDropUtil::hasAnyLocalUrl(
		mime, [this](const QString &path)
		{ return m_dragAcceptedPaths.contains(path); });
}

// Same blue ring + tint the Volumes list draws (VolumeListWidget). Kept as a
// duplicate string on purpose: two static drop targets, no shared helper.
void BinFilterDialog::setDropHighlight(bool on)
{
	if (on)
		m_binList->setStyleSheet(QStringLiteral(
			"QListWidget { border: 2px solid #4A90E2; "
			"background-color: rgba(74, 144, 226, 0.08); }"));
	else
		m_binList->setStyleSheet(QString());
}

void BinFilterDialog::dragEnterEvent(QDragEnterEvent *event)
{
	m_dragAcceptedPaths.clear();
	QSet<QString> inspectedPaths;
	// Recognize contents once per drag, not on every mouse movement. Full
	// object validation still runs on the background workers after dropping.
	for (const QUrl &url : event->mimeData()->urls())
	{
		if (!url.isLocalFile())
			continue;
		const QString path = url.toLocalFile();
		if (inspectedPaths.contains(path))
			continue;
		inspectedPaths.insert(path);
		const AvbHeaderCheck header = path.endsWith(QStringLiteral(".avb"), Qt::CaseInsensitive)
										  ? AvbParser::inspectHeader(path)
										  : AvbHeaderCheck{false, tr("Choose an Avid bin file with an .avb extension.")};
		if (header.recognized)
			m_dragAcceptedPaths.insert(path);
		else
			// A rejected enter need not receive a drop event, so report it now.
			emit loadError(path, header.error);
	}
	if (hasAcceptedDragPath(event->mimeData()))
	{
		event->acceptProposedAction();
		setDropHighlight(true);
	}
	else
	{
		setDropHighlight(false);
		event->ignore();
	}
}

void BinFilterDialog::dragMoveEvent(QDragMoveEvent *event)
{
	if (hasAcceptedDragPath(event->mimeData()))
		event->acceptProposedAction();
	else
		event->ignore();
}

void BinFilterDialog::dragLeaveEvent(QDragLeaveEvent *event)
{
	setDropHighlight(false);
	m_dragAcceptedPaths.clear();
	QDialog::dragLeaveEvent(event);
}

void BinFilterDialog::dropEvent(QDropEvent *event)
{
	setDropHighlight(false);
	if (!hasAcceptedDragPath(event->mimeData()))
	{
		m_dragAcceptedPaths.clear();
		event->ignore();
		return;
	}
	for (const QUrl &url : event->mimeData()->urls())
	{
		if (!url.isLocalFile())
			continue;
		const QString path = url.toLocalFile();
		if (m_dragAcceptedPaths.contains(path))
			addBinFromFile(path);
	}
	m_dragAcceptedPaths.clear();
	event->acceptProposedAction();
}

// MARK: - Bin loading

void BinFilterDialog::addBinFromFile(const QString &avbFilePath)
{
	const QFileInfo info(avbFilePath);
	const QString canonical = info.canonicalFilePath();
	const QString path = canonical.isEmpty()
							 ? QDir::cleanPath(info.absoluteFilePath())
							 : canonical;
	for (const LoadedBin &entry : std::as_const(m_bins))
	{
		if (entry.bin.filePath == path)
			return;
	}
	if (!avbFilePath.endsWith(QStringLiteral(".avb"), Qt::CaseInsensitive))
	{
		AvbBin rejected;
		rejected.filePath = path;
		rejected.displayName = info.completeBaseName();
		rejected.error = tr("Choose an Avid bin file with an .avb extension.");
		reportLoadFailure(rejected);
		emit binLoaded(rejected);
		return;
	}

	LoadedBin entry;
	entry.bin.filePath = path;
	entry.bin.displayName = info.completeBaseName();
	entry.id = m_nextBinId++;
	const quint64 id = entry.id;
	const int row = m_binList->count();
	m_bins.append(std::move(entry));
	appendBinItem(row);
	refreshBinSelectionUi();

	if (m_chain.isEmpty())
		m_autoIntersectPending = true;
	startBinLoad(id, path);
}

void BinFilterDialog::startBinLoad(quint64 id, const QString &path)
{
	const auto cancelled = std::make_shared<std::atomic_bool>(false);
	m_pendingLoads.insert(id, cancelled);
	auto *const watcher = new QFutureWatcher<AvbBin>(this);
	connect(watcher, &QFutureWatcher<AvbBin>::finished, this,
			[this, watcher, id]()
			{
				m_pendingLoads.remove(id);
				const AvbBin parsed = watcher->result();
				watcher->deleteLater();
				completeBinLoad(id, parsed);
				QTimer::singleShot(0, this, &BinFilterDialog::finishLoadingBatch);
			});
	// The worker owns its path and shares only the cancellation flag. The
	// watcher callback is bound to this dialog's lifetime on the GUI thread.
	watcher->setFuture(QtConcurrent::run(&m_parsePool,
										 [path, cancelled]()
										 { return AvbParser::parse(path, cancelled.get()); }));
}

void BinFilterDialog::completeBinLoad(quint64 id, const AvbBin &bin)
{
	// Rows may have been removed, compacted, or re-added while parsing.
	// Only the original stable ID can receive this result.
	for (int row = 0; row < m_binList->count(); ++row)
	{
		LoadedBin &entry = m_bins[row];
		if (entry.id != id)
			continue;
		if (!bin.valid || !bin.complete)
		{
			removeBinRow(row);
			refreshBinSelectionUi();
			reportLoadFailure(bin);
			emit binLoaded(bin);
			return;
		}
		entry.bin = bin;
		entry.loading = false;
		m_newlyLoadedIds.insert(id);
		updateBinItem(row);
		refreshBinSelectionUi();
		m_metadataUpdatePending = true;
		emit binLoaded(bin);
		return;
	}
}

void BinFilterDialog::removeBinRow(int row)
{
	m_newlyLoadedIds.remove(m_bins[row].id);
	m_bins.removeAt(row);
	// takeItem transfers ownership out of the view; release it at scope end.
	const std::unique_ptr<QListWidgetItem> removed(m_binList->takeItem(row));
	const QSignalBlocker block(m_binList);
	for (int index = row; index < m_binList->count(); ++index)
		m_binList->item(index)->setData(Qt::UserRole, index);
}

void BinFilterDialog::reportLoadFailure(const AvbBin &bin)
{
	QStringList reasons;
	if (bin.valid)
		reasons.append(tr("This bin contains data that MediaMuster does not yet support"));
	if (!bin.error.isEmpty())
		reasons.append(bin.error);
	reasons.append(bin.warnings);
	if (reasons.isEmpty())
		reasons.append(tr("The bin could not be read completely."));
	emit loadError(bin.filePath, reasons.join(QStringLiteral("; ")));
}

bool BinFilterDialog::hasLoadingBins() const
{
	return std::any_of(m_bins.cbegin(), m_bins.cend(),
					   [](const LoadedBin &entry)
					   { return entry.loading; });
}

void BinFilterDialog::finishLoadingBatch()
{
	if (hasLoadingBins())
		return;
	// Both metadata and initial filtering use the settled set of retained
	// bins. This avoids rebuilding all media metadata on every file result.
	if (m_metadataUpdatePending)
		emitBinsChanged();
	// Rejected attempts must not reactivate a previously cleared filter.
	if (!m_newlyLoadedIds.isEmpty())
		maybeAutoIntersect();
	else
		m_autoIntersectPending = false;
	m_newlyLoadedIds.clear();
}

void BinFilterDialog::maybeAutoIntersect()
{
	if (!m_autoIntersectPending || !m_chain.isEmpty())
		return;
	if (hasLoadingBins())
		return;
	m_autoIntersectPending = false;
	if (selectedBinsCount() == 0)
		return;
	applyOperation(Operation::Intersect);
}

void BinFilterDialog::appendBinItem(int idx)
{
	const QSignalBlocker block(m_binList);
	auto *const item = new QListWidgetItem(m_binList);
	item->setData(Qt::UserRole, idx);
	updateBinItem(idx);
}

void BinFilterDialog::updateBinItem(int idx)
{
	const LoadedBin &entry = m_bins[idx];
	const AvbBin &bin = entry.bin;
	QListWidgetItem *const item = m_binList->item(idx);
	const QSignalBlocker block(m_binList);
	const bool usable = !entry.loading && bin.valid && bin.complete;
	item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
				   (usable ? Qt::ItemIsUserCheckable : Qt::NoItemFlags));
	// Loading rows remain selectable so their outstanding reads can be cancelled.
	item->setData(Qt::CheckStateRole, usable ? QVariant(Qt::Checked) : QVariant());
	const QString explanation = binExplanation(bin, entry.loading);
	item->setText(explanation.isEmpty() ? bin.displayName
										: bin.displayName + QStringLiteral("\n") + explanation);
	QStringList details{bin.filePath};
	if (!explanation.isEmpty())
		details.append(explanation);
	if (!bin.error.isEmpty())
		details.append(bin.error);
	details.append(bin.warnings);
	item->setToolTip(details.join(QStringLiteral("\n")));
}

void BinFilterDialog::emitBinsChanged()
{
	m_metadataUpdatePending = false;
	QVector<AvbBin> bins;
	for (const LoadedBin &entry : std::as_const(m_bins))
	{
		if (!entry.loading && entry.bin.valid && entry.bin.complete)
			bins.append(entry.bin);
	}
	emit binsChanged(bins);
}

void BinFilterDialog::onAddBinsClicked()
{
	const QStringList paths = QFileDialog::getOpenFileNames(
		this, tr("Add Avid Bin files"), QString(),
		tr("Avid Bin files (*.avb)"));
	for (const QString &p : paths)
		addBinFromFile(p);
}

void BinFilterDialog::onRemoveSelectedBinsClicked()
{
	// 'Selected' means rows the user clicked, not the tickboxes;
	// tickboxes control which bins participate in operations.
	const QList<QListWidgetItem *> selected = m_binList->selectedItems();
	if (selected.isEmpty())
		return;

	// Every retained row, including pending reads, carries its current
	// m_bins index. Remove descending so later indices stay valid.
	QList<int> binIndices;
	binIndices.reserve(selected.size());
	for (const QListWidgetItem *it : selected)
	{
		const int idx = it->data(Qt::UserRole).toInt();
		if (idx >= 0)
			binIndices.append(idx);
	}
	std::sort(binIndices.begin(), binIndices.end(), std::greater<>{});
	for (int idx : binIndices)
	{
		if (idx < m_bins.size())
		{
			const auto cancelled = m_pendingLoads.value(m_bins[idx].id);
			if (cancelled)
				cancelled->store(true, std::memory_order_relaxed);
			m_newlyLoadedIds.remove(m_bins[idx].id);
			m_bins.removeAt(idx);
		}
	}

	// Delete the clicked rows in place instead of clearing and rebuilding the
	// whole list. The old rebuild re-ran appendBinItem for every survivor,
	// which forces the checked state — silently re-ticking bins the user had
	// deliberately unticked. Deleting in place leaves each survivor's tick
	// state intact.
	qDeleteAll(selected);

	// Survivors are still in m_bins order, so renumber their UserRole to the
	// compacted indices. Block itemChanged so the per-row setData doesn't fan
	// out one UI refresh each.
	{
		const QSignalBlocker block(m_binList);
		int compacted = 0;
		for (int row = 0; row < m_binList->count(); ++row)
		{
			QListWidgetItem *const it = m_binList->item(row);
			if (it->data(Qt::UserRole).toInt() >= 0)
				it->setData(Qt::UserRole, compacted++);
		}
	}

	// Deletes don't fire itemChanged, so refresh the summary and op-button
	// state ourselves; otherwise they'd stay stale when the last bin goes.
	refreshBinSelectionUi();

	// Chain operands are snapshots and remain unchanged, including a
	// leading Subtract. Metadata, however, belongs to the retained bins.
	emitBinsChanged();
	recomputeAndEmit();
	QTimer::singleShot(0, this, &BinFilterDialog::finishLoadingBatch);
}

// MARK: - Tick helpers

int BinFilterDialog::selectedBinsCount() const
{
	int n = 0;
	for (int i = 0; i < m_binList->count(); ++i)
	{
		const QListWidgetItem *item = m_binList->item(i);
		const int idx = item->data(Qt::UserRole).toInt();
		if (item->checkState() == Qt::Checked && idx >= 0 && idx < m_bins.size() &&
			!m_bins[idx].loading && m_bins[idx].bin.valid && m_bins[idx].bin.complete)
			++n;
	}
	return n;
}

QSet<QString> BinFilterDialog::selectedBinsMobs() const
{
	QSet<QString> out;
	for (int i = 0; i < m_binList->count(); ++i)
	{
		const QListWidgetItem *it = m_binList->item(i);
		if (it->checkState() != Qt::Checked)
			continue;
		const int idx = it->data(Qt::UserRole).toInt();
		if (idx < 0 || idx >= m_bins.size())
			continue;
		const LoadedBin &entry = m_bins[idx];
		if (!entry.loading && entry.bin.valid && entry.bin.complete)
			out.unite(entry.bin.mobIds);
	}
	return out;
}

QVector<QString> BinFilterDialog::selectedBinsDisplayNames() const
{
	QVector<QString> out;
	for (int i = 0; i < m_binList->count(); ++i)
	{
		const QListWidgetItem *it = m_binList->item(i);
		if (it->checkState() != Qt::Checked)
			continue;
		const int idx = it->data(Qt::UserRole).toInt();
		if (idx < 0 || idx >= m_bins.size())
			continue;
		const LoadedBin &entry = m_bins[idx];
		if (!entry.loading && entry.bin.valid && entry.bin.complete)
			out.append(entry.bin.displayName);
	}
	return out;
}

// MARK: - Chain operations

void BinFilterDialog::onIntersectClicked()
{
	applyOperation(Operation::Intersect);
}
void BinFilterDialog::onSubtractClicked()
{
	applyOperation(Operation::Subtract);
}
void BinFilterDialog::onAddClicked()
{
	applyOperation(Operation::Add);
}

void BinFilterDialog::applyOperation(Operation op)
{
	// A selected, complete empty bin is a real operand: Intersect must
	// create an active filter matching no rows. Absence of ticks is separate.
	if (selectedBinsCount() == 0)
		return;
	m_autoIntersectPending = false;
	ChainStep step;
	step.op = op;
	step.binDisplayNames = selectedBinsDisplayNames();
	step.mobIds = selectedBinsMobs();
	m_chain.append(std::move(step));
	rebuildChainList();
	recomputeAndEmit();
	refreshBinSelectionUi();
}

void BinFilterDialog::clearChain()
{
	m_autoIntersectPending = false;
	if (m_chain.isEmpty())
		return;
	m_chain.clear();
	rebuildChainList();
	recomputeAndEmit();
	refreshBinSelectionUi();
}

void BinFilterDialog::onRemoveStep(int index)
{
	if (index < 0 || index >= m_chain.size())
		return;
	m_autoIntersectPending = false;
	m_chain.removeAt(index);
	rebuildChainList();
	recomputeAndEmit();
	refreshBinSelectionUi();
}

void BinFilterDialog::rebuildChainList()
{
	m_chainList->clear();
	for (const ChainStep &step : std::as_const(m_chain))
	{
		const int row = m_chainList->count();
		const QString binNames =
			step.binDisplayNames.isEmpty()
				? tr("(no bins)")
				: QStringList(step.binDisplayNames.cbegin(), step.binDisplayNames.cend())
					  .join(QStringLiteral(", "));

		auto *const item = new QListWidgetItem(
			QStringLiteral("%1.  %2:  %3").arg(row + 1).arg(opLabel(step.op), binNames));
		item->setData(Qt::UserRole, row);
		m_chainList->addItem(item);
	}
}

// MARK: - Emit the ordered chain

void BinFilterDialog::recomputeAndEmit()
{
	if (m_chain.isEmpty())
	{
		m_chainSummary->setText(tr("Tick a bin, then choose an operation above."));
		emit filterChainChanged({}, {});
		return;
	}

	// No summary line here: the step list above already spells out the active
	// filter, and the main-window chips and the "filtered from N" status-bar
	// count say it again — a "filter active" label would just be a fourth,
	// vaguer restatement. Keep it blank while steps exist.
	m_chainSummary->clear();

	// Deduped, insertion-ordered bin names for the main-window chip
	// strip. First appearance wins so the chip ordering is stable
	// across re-emits.
	QStringList binNames;
	QSet<QString> seen;
	for (const ChainStep &s : std::as_const(m_chain))
	{
		for (const QString &name : s.binDisplayNames)
		{
			if (!seen.contains(name))
			{
				seen.insert(name);
				binNames.append(name);
			}
		}
	}

	emit filterChainChanged(BinFilter{m_chain}, binNames);
}
