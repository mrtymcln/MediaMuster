#include "binfilterdialog.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
    QString opLabel(BinFilterDialog::Operation op)
    {
        switch (op)
        {
        case BinFilterDialog::Operation::Intersect:
            return QStringLiteral("Intersect");
        case BinFilterDialog::Operation::Subtract:
            return QStringLiteral("Subtract");
        case BinFilterDialog::Operation::Add:
            return QStringLiteral("Add");
        }
        return {};
    }

    // Shared QSS for the plus/minus toolbar strips. Only the object name differs.
    QString segBarStyleSheet(const QString &objectName)
    {
        return QStringLiteral(
                   "#%1 {"
                   "  background: palette(window);"
                   "  border-top: 1px solid palette(mid);"
                   "}"
                   "#%1 QToolButton {"
                   "  border: none; padding: 2px 10px;"
                   "  min-width: 24px; min-height: 20px;"
                   "  color: palette(text);"
                   "  font-weight: bold;"
                   "}"
                   "#%1 QToolButton:hover { background: palette(midlight); }"
                   "#%1 QToolButton:pressed { background: palette(mid); }"
                   "#%1 QToolButton:disabled { color: palette(mid); }")
            .arg(objectName);
    }
    QWidget *makeOperationRow(const QString &label,
                              const QString &help,
                              QPushButton *&buttonOut)
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
}

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

void BinFilterDialog::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto *intro = new QLabel(tr(
        "Filter your media by Avid Bin files. Add an .AVB below, "
        "then apply an operation to narrow or widen your results."));
    intro->setWordWrap(true);
    root->addWidget(intro);

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
    btnPlus->setText(QStringLiteral("➕"));
    btnPlus->setToolTip(tr("Add…"));
    btnPlus->setCursor(Qt::PointingHandCursor);
    btnPlus->setAutoRaise(true);
    auto *btnMinus = new QToolButton;
    btnMinus->setText(QStringLiteral("➖"));
    btnMinus->setToolTip(tr("Remove selected"));
    btnMinus->setCursor(Qt::PointingHandCursor);
    btnMinus->setAutoRaise(true);
    // Minus is only enabled when a row is selected.
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
    m_binListSummary->setStyleSheet(QStringLiteral(
        "QLabel { color: palette(placeholder-text); padding-right: 8px; }"));
    segLayout->addWidget(m_binListSummary);

    framedLayout->addWidget(segBar);
    binsLayout->addWidget(binsFrame);

    connect(btnPlus, &QToolButton::clicked, this,
            &BinFilterDialog::onAddBinsClicked);
    connect(btnMinus, &QToolButton::clicked, this,
            &BinFilterDialog::onRemoveSelectedBinsClicked);

    // Enable Minus button only when at least one row is selected.
    connect(m_binList, &QListWidget::itemSelectionChanged, this,
            [this, btnMinus]()
            {
                btnMinus->setEnabled(!m_binList->selectedItems().isEmpty());
            });

    root->addWidget(binsGroup);

    auto *opsGroup = new QGroupBox(tr("Apply Operation"));
    auto *opsLayout = new QVBoxLayout(opsGroup);
    opsLayout->setSpacing(8);

    opsLayout->addWidget(makeOperationRow(
        tr("Intersect"),
        tr("Keep only media that is referenced in the ticked bins. "
           "Everything else is filtered out."),
        m_btnIntersect));

    opsLayout->addWidget(makeOperationRow(
        tr("Subtract"),
        tr("Hide everything that is referenced in the ticked bins. Useful "
           "for protecting media when archiving or deleting."),
        m_btnSubtract));

    opsLayout->addWidget(makeOperationRow(
        tr("Add"),
        tr("Show everything that is referenced in the ticked bins. "
           "Even if previously hidden."),
        m_btnAdd));

    root->addWidget(opsGroup);

    auto *chainGroup = new QGroupBox(tr("Current Filter Chain"));
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
    btnChainRemove->setText(QStringLiteral("➖"));
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

    m_chainSummary = new QLabel(tr("No operations applied"));
    m_chainSummary->setStyleSheet(QStringLiteral(
        "QLabel { color: palette(placeholder-text); padding-right: 8px; }"));
    chainSegLayout->addWidget(m_chainSummary);

    chainFrameLayout->addWidget(chainSegBar);
    chainLayout->addWidget(chainFrame);

    connect(m_chainList, &QListWidget::itemSelectionChanged, this,
            [this, btnChainRemove]()
            {
                btnChainRemove->setEnabled(
                    !m_chainList->selectedItems().isEmpty());
            });
    connect(btnChainRemove, &QToolButton::clicked, this,
            [this]()
            {
                auto sel = m_chainList->selectedItems();
                if (sel.isEmpty())
                    return;
                int idx = sel.first()->data(Qt::UserRole).toInt();
                onRemoveStep(idx);
            });

    root->addWidget(chainGroup);

    auto *footer = new QHBoxLayout;
    footer->addStretch(1);
    m_btnDone = new QPushButton(tr("Done"));
    m_btnDone->setDefault(true);
    footer->addWidget(m_btnDone);
    root->addLayout(footer);

    // Add / Remove are wired above to the segmented toolbar buttons.
    connect(m_btnIntersect, &QPushButton::clicked, this,
            &BinFilterDialog::onIntersectClicked);
    connect(m_btnSubtract, &QPushButton::clicked, this,
            &BinFilterDialog::onSubtractClicked);
    connect(m_btnAdd, &QPushButton::clicked, this,
            &BinFilterDialog::onAddClicked);
    connect(m_btnDone, &QPushButton::clicked, this, &QDialog::hide);

    // Update the "N loaded, M ticked" summary whenever the tick state changes.
    connect(m_binList, &QListWidget::itemChanged, this, [this](QListWidgetItem *)
            {
                const int loaded = m_bins.size();
                const int ticked = selectedBinsCount();
                m_binListSummary->setText(
                    tr("%1 loaded, %2 ticked").arg(loaded).arg(ticked)); });
}

void BinFilterDialog::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void BinFilterDialog::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void BinFilterDialog::dropEvent(QDropEvent *event)
{
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

void BinFilterDialog::addBinFromFile(const QString &avbFilePath)
{
    for (const AvbBin &b : m_bins)
    {
        if (b.filePath == avbFilePath)
            return;
    }

    AvbBin bin = AvbParser::parse(avbFilePath);
    if (!bin.valid)
    {
        // Parser already logged a qWarning; surface it in the UI too.
        auto *item = new QListWidgetItem(
            tr("⚠ %1  (not a valid Avid bin)")
                .arg(QFileInfo(avbFilePath).fileName()),
            m_binList);
        item->setData(Qt::UserRole, -1);
        item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
        item->setForeground(Qt::red);
        return;
    }
    m_bins.append(bin);
    appendBinItem(m_bins.size() - 1);
}

void BinFilterDialog::appendBinItem(int idx)
{
    const AvbBin &bin = m_bins[idx];
    auto *item = new QListWidgetItem(
        tr("%1  —  %2 refs").arg(bin.displayName).arg(bin.mobIds.size()),
        m_binList);
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
    // "Selected" = list-row highlight (the row(s) the user clicked on),
    // not the tickboxes — those control which bins participate in operations.
    const QList<QListWidgetItem *> selected = m_binList->selectedItems();
    if (selected.isEmpty())
        return;

    // Descending order so each removeAt doesn't shift earlier indices.
    QList<int> indicesDesc;
    indicesDesc.reserve(selected.size());
    for (QListWidgetItem *it : selected)
        indicesDesc.append(it->data(Qt::UserRole).toInt());
    std::sort(indicesDesc.begin(), indicesDesc.end(), std::greater<>{});
    for (int idx : indicesDesc)
    {
        if (idx >= 0 && idx < m_bins.size())
            m_bins.removeAt(idx);
    }

    m_binList->clear();
    for (int i = 0; i < m_bins.size(); ++i)
        appendBinItem(i);

    // Chain steps capture the MOB IDs on apply, so removing a bin doesn't break them.
    recomputeAndEmit();
}

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

void BinFilterDialog::onIntersectClicked() { applyOperation(Operation::Intersect); }
void BinFilterDialog::onSubtractClicked() { applyOperation(Operation::Subtract); }
void BinFilterDialog::onAddClicked() { applyOperation(Operation::Add); }

void BinFilterDialog::applyOperation(Operation op)
{
    QSet<QString> mobs = selectedBinsMobs();
    if (mobs.isEmpty())
    {
        m_chainSummary->setText(
            tr("Tick at least one Bin above to apply %1.").arg(opLabel(op)));
        return;
    }
    ChainStep step;
    step.op = op;
    step.binDisplayNames = selectedBinsDisplayNames();
    step.mobIds = std::move(mobs);
    m_chain.append(std::move(step));
    rebuildChainList();
    recomputeAndEmit();
}

void BinFilterDialog::onRemoveStep(int index)
{
    if (index < 0 || index >= m_chain.size())
        return;
    m_chain.removeAt(index);
    rebuildChainList();
    recomputeAndEmit();
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
                : QStringList(step.binDisplayNames.cbegin(),
                              step.binDisplayNames.cend())
                      .join(QStringLiteral(", "));

        auto *item = new QListWidgetItem(
            tr("%1.  %2:  %3    (%4 refs)")
                .arg(i + 1)
                .arg(opLabel(step.op), binNames)
                .arg(step.mobIds.size()));
        item->setData(Qt::UserRole, i);
        m_chainList->addItem(item);
    }
}

void BinFilterDialog::recomputeAndEmit()
{
    if (m_chain.isEmpty())
    {
        m_chainSummary->setText(tr("No filter active — all files shown."));
        emit filterChainChanged(false, {});
        return;
    }

    // The starting "universe" depends on the first op:
    //   Intersect/Add — accepted = first.mobIds (intersecting {} narrows to {}, so set rather than ∩).
    //   Subtract      — accepted = (union of every loaded bin) \ first.mobIds.
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

    m_chainSummary->setText(
        tr("Filter active — %1 MOBs accepted across %2 step%3.")
            .arg(accepted.size())
            .arg(m_chain.size())
            .arg(m_chain.size() == 1 ? "" : "s"));

    emit filterChainChanged(true, accepted);
}