#include "managemediadialog.h"

#include <QButtonGroup>
#include <QStandardItemModel>
#include <QStorageInfo>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTreeWidget>
#include <QVBoxLayout>

static QString formatBytes(qint64 bytes)
{
    if (bytes >= static_cast<qint64>(1024) * 1024 * 1024 * 1024)
        return QString("%1 TB").arg(bytes / (1024.0 * 1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
    if (bytes >= static_cast<qint64>(1024) * 1024 * 1024)
        return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
    if (bytes >= 1024 * 1024)
        return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    if (bytes >= 1024)
        return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QString("%1 B").arg(bytes);
}

ManageMediaDialog::ManageMediaDialog(const QVector<MediaFile> &files,
                                     QWidget *parent,
                                     Operation initialOp)
    : QDialog(parent), m_files(files)
{
    setWindowTitle(tr("Manage Media"));
    setWindowFlags(windowFlags() | Qt::Tool);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow, true);
    setMinimumWidth(700);
    resize(780, 620);
    setupUi();

    // Pre-select the requested operation (e.g: when invoked from the
    // context menu's "Copy To…" / "Move To…" / "Delete" items).
    switch (initialOp)
    {
    case Operation::Copy:
        m_radioCopy->setChecked(true);
        break;
    case Operation::Move:
        m_radioMove->setChecked(true);
        break;
    case Operation::Delete:
        m_radioDelete->setChecked(true);
        break;
    }
    onOperationChanged(); // sets visibility, populates preview, updates summary
}

void ManageMediaDialog::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    // ---- operation group ----
    auto *opGroup = new QGroupBox(tr("Operation"));
    auto *opLayout = new QVBoxLayout(opGroup);
    opLayout->setSpacing(8);

    auto makeOpRow = [&](QRadioButton *&radio, const QString &label,
                         const QString &desc) -> QWidget *
    {
        auto *row = new QWidget;
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(12);
        radio = new QRadioButton(label);
        radio->setMinimumWidth(120);
        lay->addWidget(radio);
        auto *d = new QLabel(desc);
        d->setWordWrap(true);
        d->setStyleSheet(QStringLiteral("QLabel { color: palette(mid); }"));
        lay->addWidget(d, 1);
        return row;
    };

    opLayout->addWidget(makeOpRow(m_radioCopy, tr("Copy"),
                                  tr("Copy the selected files to a new location. Originals are untouched.")));
    opLayout->addWidget(makeOpRow(m_radioMove, tr("Move"),
                                  tr("Move the selected files. Originals are removed after a successful copy.")));
    opLayout->addWidget(makeOpRow(m_radioDelete, tr("Delete"),
                                  tr("Send the selected files to the Bin. Files can be recovered from the Bin if needed.")));

    m_radioCopy->setChecked(true);

    m_opGroup = new QButtonGroup(this);
    m_opGroup->addButton(m_radioCopy, +Operation::Copy);
    m_opGroup->addButton(m_radioMove, +Operation::Move);
    m_opGroup->addButton(m_radioDelete, +Operation::Delete);

    root->addWidget(opGroup);

    // ---- destination group (hidden when delete is selected) ----
    m_destWidget = new QGroupBox(tr("Destination"));
    auto *destOuter = new QVBoxLayout(static_cast<QGroupBox *>(m_destWidget));
    destOuter->setSpacing(4);

    auto *destRow = new QHBoxLayout;
    m_destPath = new QLineEdit;
    m_destPath->setPlaceholderText(tr("Choose a destination folder…"));
    m_destPath->setReadOnly(true);
    m_btnChoose = new QPushButton(tr("Choose…"));
    destRow->addWidget(m_destPath, 1);
    destRow->addWidget(m_btnChoose);
    destOuter->addLayout(destRow);

    m_spaceWarning = new QLabel;
    m_spaceWarning->setStyleSheet(QStringLiteral("QLabel { color: red; }"));
    m_spaceWarning->setVisible(false);
    destOuter->addWidget(m_spaceWarning);

    root->addWidget(m_destWidget);

    // ---- options group ----
    m_optGroup = new QGroupBox(tr("Options"));
    auto *optLayout = new QVBoxLayout(m_optGroup);

    m_chkPreserve = new QCheckBox(
        tr("Preserve Avid folder structure (Avid MediaFiles/MXF/<N>/…)"));
    m_chkPreserve->setChecked(true);
    optLayout->addWidget(m_chkPreserve);

    root->addWidget(m_optGroup);

    // ---- conflict resolution (hidden until conflicts are detected) ----
    m_conflictGroup = new QGroupBox(tr("Conflicts"));
    auto *conflictLayout = new QHBoxLayout(m_conflictGroup);

    auto *conflictLabel = new QLabel(
        tr("Some files already exist at the destination. Apply to all:"));
    conflictLabel->setWordWrap(true);
    conflictLayout->addWidget(conflictLabel, 1);

    m_conflictGlobalCombo = new QComboBox;
    m_conflictGlobalCombo->addItem(tr("Overwrite"), +ConflictPolicy::Overwrite);
    m_conflictGlobalCombo->addItem(tr("Skip"), +ConflictPolicy::Skip);
    m_conflictGlobalCombo->addItem(tr("Append '.Copy.01'"), +ConflictPolicy::Rename);
    m_conflictGlobalCombo->addItem(tr("— Mixed —"), -1);
    // Make "Mixed" non-selectable: it is set only programmatically
    if (auto *model = qobject_cast<QStandardItemModel *>(m_conflictGlobalCombo->model()))
        model->item(3)->setFlags(Qt::NoItemFlags);
    m_conflictGlobalCombo->setCurrentIndex(0);
    m_conflictGlobalCombo->setMinimumWidth(180);
    conflictLayout->addWidget(m_conflictGlobalCombo);

    m_conflictGroup->setVisible(false); // shown only when conflicts exist
    root->addWidget(m_conflictGroup);

    // ---- preview group ----
    auto *previewGroup = new QGroupBox(tr("Preview"));
    auto *previewLayout = new QVBoxLayout(previewGroup);

    m_previewTree = new QTreeWidget;
    m_previewTree->setRootIsDecorated(false);
    m_previewTree->setAlternatingRowColors(true);
    m_previewTree->setMinimumHeight(140);
    m_previewTree->setSortingEnabled(true);
    m_previewTree->header()->setSectionResizeMode(QHeaderView::Stretch);
    previewLayout->addWidget(m_previewTree);

    m_summaryLabel = new QLabel;
    previewLayout->addWidget(m_summaryLabel);

    root->addWidget(previewGroup, 1);

    // ---- footer ----
    auto *footer = new QHBoxLayout;
    footer->addStretch(1);
    m_btnCancel = new QPushButton(tr("Cancel"));
    m_btnExecute = new QPushButton(tr("Copy"));
    m_btnExecute->setDefault(true);
    m_btnExecute->setEnabled(false);
    footer->addWidget(m_btnCancel);
    footer->addWidget(m_btnExecute);
    root->addLayout(footer);

    // ---- wiring ----
    connect(m_btnChoose, &QPushButton::clicked, this,
            &ManageMediaDialog::onChooseDestination);
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_btnExecute, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_opGroup, &QButtonGroup::idToggled, this,
            [this](int, bool)
            { onOperationChanged(); });
    connect(m_destPath, &QLineEdit::textChanged, this,
            [this](const QString &)
            { updatePreview(); });
    connect(m_chkPreserve, &QCheckBox::toggled, this,
            [this](bool)
            { updatePreview(); });
    connect(m_conflictGlobalCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ManageMediaDialog::onGlobalConflictPolicyChanged);
}

// Slots

void ManageMediaDialog::onChooseDestination()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Choose destination folder"));
    if (!dir.isEmpty())
        m_destPath->setText(dir);
}

void ManageMediaDialog::onOperationChanged()
{
    const bool isDel = (operation() == Operation::Delete);
    m_destWidget->setVisible(!isDel);
    m_optGroup->setVisible(!isDel);

    switch (operation())
    {
    case Operation::Copy:
        m_btnExecute->setText(tr("Copy"));
        break;
    case Operation::Move:
        m_btnExecute->setText(tr("Move"));
        break;
    case Operation::Delete:
        m_btnExecute->setText(tr("Send to Bin"));
        break;
    }

    updatePreview(); // calls updateSummary() internally
}

void ManageMediaDialog::onGlobalConflictPolicyChanged(int index)
{
    if (m_conflictGlobalCombo->itemData(index).toInt() == -1)
        return; // "Mixed" — informational only, do not push to per-file combos

    for (auto *combo : m_perFileConflictCombos)
    {
        combo->blockSignals(true); // prevent per-row signal storm
        combo->setCurrentIndex(index);
        combo->blockSignals(false);
    }
}

// Preview

QString ManageMediaDialog::buildDestPath(const MediaFile &mf,
                                         const QString &destRoot,
                                         bool preserve) const
{
    const QDir root(destRoot);
    if (preserve)
    {
        return root.filePath(
            QStringLiteral("Avid MediaFiles/MXF/%1/%2").arg(mf.mxfFolder, mf.fileName));
    }
    return root.filePath(mf.fileName);
}

void ManageMediaDialog::updatePreview()
{
    m_previewTree->setSortingEnabled(false);
    m_previewTree->clear();
    m_perFileConflictCombos.clear();

    const Operation op = operation();
    const QString dest = m_destPath->text();
    const bool preserve = m_chkPreserve->isChecked();
    int conflictCount = 0;

    if (op == Operation::Delete)
    {
        m_previewTree->setHeaderLabels({tr("File"), tr("Size"), tr("Action")});
        m_previewTree->setColumnCount(3);
        for (const MediaFile &mf : m_files)
        {
            auto *item = new QTreeWidgetItem(
                {mf.filePath, mf.sizeDisplay(), tr("Send to Bin")});
            m_previewTree->addTopLevelItem(item);
        }
        m_conflictGroup->setVisible(false);
    }
    else
    {
        // Columns: Source(0) | Size(1) | Destination(2) [| If exists(3)]
        // The 4th column is added only if conflicts are detected, to avoid
        // a confusing empty column in the common no-conflict case.
        m_previewTree->setHeaderLabels({tr("Source"), tr("Size"), tr("Destination")});
        m_previewTree->setColumnCount(3);

        if (dest.isEmpty())
        {
            for (const MediaFile &mf : m_files)
            {
                auto *item = new QTreeWidgetItem(
                    {mf.filePath, mf.sizeDisplay(), tr("(choose destination)")});
                item->setForeground(2, Qt::gray);
                m_previewTree->addTopLevelItem(item);
            }
        }
        else
        {
            // When global is "Mixed" (data == -1) new combos default to Overwrite.
            const int globalIdx = (m_conflictGlobalCombo->currentData().toInt() == -1)
                                      ? 0
                                      : m_conflictGlobalCombo->currentIndex();

            struct RowInfo
            {
                QTreeWidgetItem *item;
                QString sourcePath;
                bool conflict;
            };
            QVector<RowInfo> rows;
            rows.reserve(m_files.size());

            for (const MediaFile &mf : m_files)
            {
                const QString dp = buildDestPath(mf, dest, preserve);
                const bool conflict = QFileInfo::exists(dp);
                auto *item = new QTreeWidgetItem({mf.filePath, mf.sizeDisplay(), dp});

                if (conflict)
                {
                    item->setForeground(2, Qt::red);
                    item->setToolTip(2, tr("File already exists at destination"));
                    ++conflictCount;
                }
                m_previewTree->addTopLevelItem(item);
                rows.append({item, mf.filePath, conflict});
            }

            if (conflictCount > 0)
            {
                m_previewTree->setHeaderLabels(
                    {tr("Source"), tr("Size"), tr("Destination"), tr("If exists")});
                m_previewTree->setColumnCount(4);

                for (const RowInfo &r : rows)
                {
                    if (!r.conflict)
                        continue;
                    auto *combo = new QComboBox;
                    combo->addItem(tr("Overwrite"), +ConflictPolicy::Overwrite);
                    combo->addItem(tr("Skip"), +ConflictPolicy::Skip);
                    combo->addItem(tr("Append '.Copy.01'"), +ConflictPolicy::Rename);
                    combo->setCurrentIndex(globalIdx);
                    m_previewTree->setItemWidget(r.item, 3, combo);
                    m_perFileConflictCombos.insert(r.sourcePath, combo);
                    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                            this, [this](int)
                            { syncGlobalFromPerFile(); });
                }
            }
        }

        m_conflictGroup->setVisible(conflictCount > 0);
        if (conflictCount > 0)
        {
            m_conflictGroup->setTitle(
                tr("Conflicts (%1 of %2 files)")
                    .arg(conflictCount)
                    .arg(m_files.size()));
        }
    }

    syncGlobalFromPerFile(); // keep global in sync after tree rebuild
    m_previewTree->setSortingEnabled(true);
    updateSummary();
}

void ManageMediaDialog::updateSummary()
{
    const int count = m_files.size();
    qint64 totalBytes = 0;
    for (const MediaFile &mf : m_files)
        totalBytes += mf.sizeBytes;

    QString opVerb;
    switch (operation())
    {
    case Operation::Copy:
        opVerb = tr("Copy");
        break;
    case Operation::Move:
        opVerb = tr("Move");
        break;
    case Operation::Delete:
        opVerb = tr("Send to Bin");
        break;
    }

    m_summaryLabel->setText(
        tr("%1 %2 file%3 (%4)")
            .arg(opVerb)
            .arg(count)
            .arg(count == 1 ? "" : "s")
            .arg(formatBytes(totalBytes)));

    bool canExecute = !m_files.isEmpty();
    const QString dest = m_destPath->text();

    if (operation() != Operation::Delete && dest.isEmpty())
        canExecute = false;

    if (operation() != Operation::Delete && !dest.isEmpty())
    {
        const QStorageInfo storage(dest);
        if (storage.isValid() && totalBytes > storage.bytesAvailable())
        {
            m_spaceWarning->setText(
                tr("Insufficient space: %1 needed, %2 free on destination volume")
                    .arg(formatBytes(totalBytes))
                    .arg(formatBytes(storage.bytesAvailable())));
            m_spaceWarning->setVisible(true);
            canExecute = false;
        }
        else
        {
            m_spaceWarning->setVisible(false);
        }
    }
    else
    {
        m_spaceWarning->setVisible(false);
    }

    m_btnExecute->setEnabled(canExecute);
}

void ManageMediaDialog::syncGlobalFromPerFile()
{
    if (m_perFileConflictCombos.isEmpty())
        return;

    int commonIdx = -1;
    bool allSame = true;
    for (const auto *combo : m_perFileConflictCombos)
    {
        if (commonIdx == -1)
            commonIdx = combo->currentIndex();
        else if (combo->currentIndex() != commonIdx)
        {
            allSame = false;
            break;
        }
    }

    m_conflictGlobalCombo->blockSignals(true);
    m_conflictGlobalCombo->setCurrentIndex(
        (allSame && commonIdx >= 0) ? commonIdx
                                    : m_conflictGlobalCombo->findData(-1));
    m_conflictGlobalCombo->blockSignals(false);
}

// Accessors (read after exec() returns Accepted)

ManageMediaDialog::Operation ManageMediaDialog::operation() const
{
    return static_cast<Operation>(m_opGroup->checkedId());
}

QString ManageMediaDialog::destination() const
{
    return m_destPath->text();
}

bool ManageMediaDialog::preserveStructure() const
{
    return m_chkPreserve->isChecked();
}

QHash<QString, ManageMediaDialog::ConflictPolicy>
ManageMediaDialog::conflictPolicies() const
{
    QHash<QString, ConflictPolicy> out;
    for (auto it = m_perFileConflictCombos.constBegin();
         it != m_perFileConflictCombos.constEnd(); ++it)
    {
        out.insert(it.key(),
                   static_cast<ConflictPolicy>(it.value()->currentData().toInt()));
    }
    return out;
}