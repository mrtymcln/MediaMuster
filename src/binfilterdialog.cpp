#include "binfilterdialog.h"
#include "dragdroputil.h"
#include "formatutil.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFrame>
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
#include <QVBoxLayout>

// MARK: - File-local helpers

namespace
{
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
	resize(720, 640);
	setupUi();
}

BinFilterDialog::~BinFilterDialog() = default;

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
	const int loaded = m_bins.size();
	const int ready = selectedBinsCount();
	m_binListSummary->setText(
		tr("%1 loaded, %2 ticked").arg(Format::count(loaded), Format::count(ready)));

	// Disabled op buttons hard-couple the two halves of the dialog: users can't
	// click an op without first ticking a bin, so the 'tickbox = arms, button =
	// fires' model becomes self-evident — which is also why the disabled state
	// needs no explanatory tooltip.
	const bool canApply = ready > 0;
	m_btnIntersect->setEnabled(canApply);
	m_btnAdd->setEnabled(canApply);
	// Subtract additionally needs an existing base to act on. As the very first
	// step it would fall back to the loaded-bin union and hide media that's in
	// no loaded bin — a surprise. Requiring a prior Intersect/Add step keeps it
	// predictable, so it stays disabled until the chain has at least one step.
	m_btnSubtract->setEnabled(canApply && !m_chain.isEmpty());

	// Empty-chain placeholder. Single message; the intro text and
	// the bin-list summary already guide users into loading + ticking
	// bins; this slot just describes the next concrete action.
	if (m_chain.isEmpty())
		m_chainSummary->setText(tr("Tick a bin, then choose an operation above."));
}

// MARK: - Drag and drop

// Shared between dragEnterEvent, dragMoveEvent, and dropEvent so
// all three honour the same drop-accept rules.
static bool dragHasAvb(const QMimeData *mime)
{
	return DragDropUtil::hasAnyLocalUrl(
		mime, [](const QString &path)
		{ return path.endsWith(QStringLiteral(".avb"), Qt::CaseInsensitive); });
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
	if (dragHasAvb(event->mimeData()))
	{
		event->acceptProposedAction();
		setDropHighlight(true);
	}
	else
	{
		event->ignore();
	}
}

void BinFilterDialog::dragMoveEvent(QDragMoveEvent *event)
{
	if (dragHasAvb(event->mimeData()))
		event->acceptProposedAction();
	else
		event->ignore();
}

void BinFilterDialog::dragLeaveEvent(QDragLeaveEvent *event)
{
	setDropHighlight(false);
	QDialog::dragLeaveEvent(event);
}

void BinFilterDialog::dropEvent(QDropEvent *event)
{
	setDropHighlight(false);
	if (!event->mimeData()->hasUrls())
		return;
	for (const QUrl &url : event->mimeData()->urls())
	{
		if (!url.isLocalFile())
			continue;
		const QString path = url.toLocalFile();
		if (path.endsWith(QStringLiteral(".avb"), Qt::CaseInsensitive))
			addBinFromFile(path);
	}
	event->acceptProposedAction();
}

// MARK: - Bin loading

void BinFilterDialog::addBinFromFile(const QString &avbFilePath)
{
	// De-dupe by path to avoid two list entries with the same MOBs.
	for (const AvbBin &b : m_bins)
	{
		if (b.filePath == avbFilePath)
			return;
	}

	AvbBin bin = AvbParser::parse(avbFilePath);
	if (!bin.valid)
	{
		// Both entry points filter to .avb — the picker by file filter, the drag
		// by extension — so a non-bin normally can't reach here and no error row
		// is shown. A file with a genuinely renamed .avb extension could still
		// slip past the extension check; drop it silently (the parser logged why
		// to the lcAvb category) rather than list it as if it were a real bin.
		return;
	}
	m_bins.append(bin);
	appendBinItem(m_bins.size() - 1);

	// Convenience: an empty chain + a fresh add means the user hasn't
	// built any filter yet. Auto-apply Intersect across whatever's
	// ticked so the most common case (filter to just-loaded bins)
	// works without a button press. singleShot(0) coalesces drop
	// bursts and file-picker batches: the first scheduled call applies
	// the Intersect; subsequent calls see a non-empty chain and skip.
	if (m_chain.isEmpty())
		QTimer::singleShot(0, this, &BinFilterDialog::maybeAutoIntersect);
}

void BinFilterDialog::maybeAutoIntersect()
{
	if (!m_chain.isEmpty())
		return;
	if (selectedBinsCount() == 0)
		return;
	applyOperation(Operation::Intersect);
}

void BinFilterDialog::appendBinItem(int idx)
{
	const AvbBin &bin = m_bins[idx];

	// Just the bin's display name; the underlying MOB-ID count is
	// an Avid internal that doesn't map cleanly to clips or files,
	// so we don't surface it.
	auto *item = new QListWidgetItem(bin.displayName, m_binList);
	item->setData(Qt::UserRole, idx);
	item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
	item->setCheckState(Qt::Checked);
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

	// Drop the backing AvbBins. Every row carries its m_bins index in
	// UserRole — an invalid bin produces no row at all, so there are no
	// placeholders and the >= 0 check below is belt-and-braces. Descending
	// so each removeAt doesn't shift indices we still need.
	QList<int> binIndices;
	binIndices.reserve(selected.size());
	for (QListWidgetItem *it : selected)
	{
		const int idx = it->data(Qt::UserRole).toInt();
		if (idx >= 0)
			binIndices.append(idx);
	}
	std::sort(binIndices.begin(), binIndices.end(), std::greater<>{});
	for (int idx : binIndices)
	{
		if (idx < m_bins.size())
			m_bins.removeAt(idx);
	}

	// Delete the clicked rows in place instead of clearing and rebuilding the
	// whole list. The old rebuild re-ran appendBinItem for every survivor,
	// which forces Qt::Checked — silently re-ticking bins the user had
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
			QListWidgetItem *it = m_binList->item(row);
			if (it->data(Qt::UserRole).toInt() >= 0)
				it->setData(Qt::UserRole, compacted++);
		}
	}

	// Deletes don't fire itemChanged, so refresh the summary and op-button
	// state ourselves; otherwise they'd stay stale when the last bin goes.
	refreshBinSelectionUi();

	// Chain steps snapshot MOB IDs at apply time, so removing a
	// loaded bin doesn't invalidate them; re-emit the cached result.
	recomputeAndEmit();
}

// MARK: - Tick helpers

int BinFilterDialog::selectedBinsCount() const
{
	int n = 0;
	for (int i = 0; i < m_binList->count(); ++i)
	{
		if (m_binList->item(i)->checkState() == Qt::Checked)
			++n;
	}
	return n;
}

QSet<QString> BinFilterDialog::selectedBinsMobs() const
{
	QSet<QString> out;
	for (int i = 0; i < m_binList->count(); ++i)
	{
		QListWidgetItem *it = m_binList->item(i);
		if (it->checkState() != Qt::Checked)
			continue;
		const int idx = it->data(Qt::UserRole).toInt();
		if (idx < 0 || idx >= m_bins.size())
			continue;
		out.unite(m_bins[idx].mobIds);
	}
	return out;
}

QVector<QString> BinFilterDialog::selectedBinsDisplayNames() const
{
	QVector<QString> out;
	for (int i = 0; i < m_binList->count(); ++i)
	{
		QListWidgetItem *it = m_binList->item(i);
		if (it->checkState() != Qt::Checked)
			continue;
		const int idx = it->data(Qt::UserRole).toInt();
		if (idx < 0 || idx >= m_bins.size())
			continue;
		out.append(m_bins[idx].displayName);
	}
	return out;
}

QSet<QString> BinFilterDialog::allLoadedMobs() const
{
	QSet<QString> out;
	for (const AvbBin &b : m_bins)
		out.unite(b.mobIds);
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
	QSet<QString> mobs = selectedBinsMobs();
	// The op buttons are disabled when nothing is ticked; this guard
	// is belt-and-braces in case the path ever opens up programmatically.
	if (mobs.isEmpty())
		return;
	ChainStep step;
	step.op = op;
	step.binDisplayNames = selectedBinsDisplayNames();
	step.mobIds = std::move(mobs);
	m_chain.append(std::move(step));
	rebuildChainList();
	recomputeAndEmit();
	refreshBinSelectionUi(); // re-gate Subtract now the chain has a step
}

void BinFilterDialog::clearChain()
{
	if (m_chain.isEmpty())
		return;
	m_chain.clear();
	rebuildChainList();
	recomputeAndEmit();
	refreshBinSelectionUi(); // chain empty again — disable Subtract
}

void BinFilterDialog::onRemoveStep(int index)
{
	if (index < 0 || index >= m_chain.size())
		return;
	m_chain.removeAt(index);
	rebuildChainList();
	recomputeAndEmit();
	refreshBinSelectionUi(); // chain may now be empty — re-gate Subtract
}

void BinFilterDialog::rebuildChainList()
{
	m_chainList->clear();
	for (int i = 0; i < m_chain.size(); ++i)
	{
		const ChainStep &step = m_chain[i];
		const QString binNames =
			step.binDisplayNames.isEmpty()
				? tr("(no bins)")
				: QStringList(step.binDisplayNames.cbegin(), step.binDisplayNames.cend())
					  .join(QStringLiteral(", "));

		auto *item = new QListWidgetItem(
			QStringLiteral("%1.  %2:  %3").arg(i + 1).arg(opLabel(step.op), binNames));
		item->setData(Qt::UserRole, i);
		m_chainList->addItem(item);
	}
}

// MARK: - Resolve chain and emit

void BinFilterDialog::recomputeAndEmit()
{
	if (m_chain.isEmpty())
	{
		m_chainSummary->setText(tr("Tick a bin, then choose an operation above."));
		emit filterChainChanged(false, {}, {});
		return;
	}

	// Starting universe depends on the first op:
	//   Intersect / Add: start from `first.mobIds` directly.
	//   Subtract: start from (loaded-bin union) \ `first.mobIds`.
	//
	// Subtract seeds from the loaded-bin union because
	// 'subtract from accept-everything' isn't expressible as a
	// finite set.
	QSet<QString> accepted;

	const ChainStep &first = m_chain.first();
	switch (first.op)
	{
	case Operation::Intersect:
		accepted = first.mobIds;
		break;
	case Operation::Subtract:
		accepted = allLoadedMobs().subtract(first.mobIds);
		break;
	case Operation::Add:
		accepted = first.mobIds;
		break;
	}

	for (int i = 1; i < m_chain.size(); ++i)
	{
		const ChainStep &s = m_chain[i];
		switch (s.op)
		{
		case Operation::Intersect:
			accepted.intersect(s.mobIds);
			break;
		case Operation::Subtract:
			accepted.subtract(s.mobIds);
			break;
		case Operation::Add:
			accepted.unite(s.mobIds);
			break;
		}
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
	for (const ChainStep &s : m_chain)
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

	emit filterChainChanged(true, accepted, binNames);
}