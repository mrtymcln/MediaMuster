#include "managemediadialog.h"
#include "formatutil.h"
#include "mediamanager.h"

#include <QButtonGroup>
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
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QStorageInfo>
#include <QTreeWidget>
#include <QVBoxLayout>

// MARK: - Row repaint helper

// 'Keep Both' renders the .Copy.NN destination; 'Replace'/'Skip'
// keep the original path, painted red.
static void applyConflictPolicyToRow(QTreeWidgetItem *item, const QString &baseDest,
                                     ManageMediaDialog::ConflictPolicy policy)
{
	if (policy == ManageMediaDialog::ConflictPolicy::KeepBoth)
	{
		const QString renamed = MediaManager::generateRenamePath(baseDest);
		if (!renamed.isEmpty())
		{
			item->setText(1, renamed);
			item->setForeground(1, QBrush());
			item->setToolTip(1, QString());
			return;
		}
	}
	item->setText(1, baseDest);
	item->setForeground(1, Qt::red);
	item->setToolTip(1, policy == ManageMediaDialog::ConflictPolicy::KeepBoth
	                        ? ManageMediaDialog::tr("No unique name available (999 .Copy.NN slots already used)")
	                        : ManageMediaDialog::tr("File already exists at destination"));
}

// MARK: - Construction

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
	onOperationChanged();
}

// MARK: - UI layout

void ManageMediaDialog::setupUi()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(16, 16, 16, 16);
	root->setSpacing(12);

	// MARK: Operation group

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
		d->setStyleSheet(QStringLiteral("QLabel { color: palette(placeholder-text); }"));
		lay->addWidget(d, 1);
		return row;
	};

	opLayout->addWidget(makeOpRow(m_radioCopy, tr("Copy"),
	                              tr("Copy the selected files to a new location. Originals are untouched.")));
	opLayout->addWidget(makeOpRow(m_radioMove, tr("Move"),
	                              tr("Move the selected files. Originals are removed after a successful copy.")));
	opLayout->addWidget(makeOpRow(m_radioDelete, tr("Delete"),
	                              tr("Delete the selected files. Deleted files can be recovered from the trash.")));

	m_radioCopy->setChecked(true);

	m_opGroup = new QButtonGroup(this);
	m_opGroup->addButton(m_radioCopy, +Operation::Copy);
	m_opGroup->addButton(m_radioMove, +Operation::Move);
	m_opGroup->addButton(m_radioDelete, +Operation::Delete);

	root->addWidget(opGroup);

	// MARK: Destination group

	m_destWidget = new QGroupBox(tr("Destination"));
	auto *destOuter = new QVBoxLayout(static_cast<QGroupBox *>(m_destWidget));
	destOuter->setSpacing(4);

	auto *destRow = new QHBoxLayout;
	m_destPath = new QLineEdit;
	m_destPath->setPlaceholderText(tr("Choose a destination folder..."));
	m_destPath->setReadOnly(true);
	m_btnChoose = new QPushButton(tr("Choose..."));
	destRow->addWidget(m_destPath, 1);
	destRow->addWidget(m_btnChoose);
	destOuter->addLayout(destRow);

	m_spaceWarning = new QLabel;
	m_spaceWarning->setStyleSheet(QStringLiteral("QLabel { color: red; }"));
	m_spaceWarning->setVisible(false);
	destOuter->addWidget(m_spaceWarning);

	m_chkPreserve = new QCheckBox(
	    tr("Preserve Avid folder structure (Avid MediaFiles/MXF/<N>/...)"));
	m_chkPreserve->setChecked(true);
	destOuter->addWidget(m_chkPreserve);

	root->addWidget(m_destWidget);

	// MARK: Conflicts group

	m_conflictGroup = new QGroupBox(tr("Conflicts"));
	auto *conflictLayout = new QHBoxLayout(m_conflictGroup);

	auto *conflictLabel = new QLabel(
	    tr("Some files already exist at the destination. Apply to all:"));
	conflictLabel->setWordWrap(true);
	conflictLayout->addWidget(conflictLabel, 1);

	m_conflictGlobalCombo = new QComboBox;
	m_conflictGlobalCombo->addItem(tr("Keep Both"), +ConflictPolicy::KeepBoth);
	m_conflictGlobalCombo->addItem(tr("Skip"), +ConflictPolicy::Skip);
	m_conflictGlobalCombo->addItem(tr("Replace"), +ConflictPolicy::Replace);
	m_conflictGlobalCombo->addItem(tr("— Mixed —"), -1);

	// 'Mixed' is informational — set programmatically when per-file combos diverge.
	if (auto *model = qobject_cast<QStandardItemModel *>(m_conflictGlobalCombo->model()))
		model->item(3)->setFlags(Qt::NoItemFlags);
	m_conflictGlobalCombo->setCurrentIndex(0);
	m_conflictGlobalCombo->setMinimumWidth(180);
	conflictLayout->addWidget(m_conflictGlobalCombo);

	m_conflictGroup->setVisible(false);
	root->addWidget(m_conflictGroup);

	// MARK: Preview group

	auto *previewGroup = new QGroupBox(tr("Preview"));
	auto *previewLayout = new QVBoxLayout(previewGroup);

	m_previewTree = new QTreeWidget;
	m_previewTree->setRootIsDecorated(false);
	m_previewTree->setAlternatingRowColors(true);
	m_previewTree->setMinimumHeight(140);
	m_previewTree->setSortingEnabled(true);
	m_previewTree->header()->setSectionResizeMode(QHeaderView::Stretch);
	m_previewTree->setTextElideMode(Qt::ElideLeft);
	previewLayout->addWidget(m_previewTree);

	m_summaryLabel = new QLabel;
	previewLayout->addWidget(m_summaryLabel);

	root->addWidget(previewGroup, 1);

	// MARK: Footer

	auto *footer = new QHBoxLayout;
	footer->addStretch(1);
	m_btnCancel = new QPushButton(tr("Cancel"));
	m_btnExecute = new QPushButton(tr("Copy"));
	m_btnExecute->setDefault(true);
	m_btnExecute->setEnabled(false);
	footer->addWidget(m_btnCancel);
	footer->addWidget(m_btnExecute);
	root->addLayout(footer);

	// MARK: Wire signals

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
	        &QComboBox::currentIndexChanged, this,
	        &ManageMediaDialog::onGlobalConflictPolicyChanged);
}

// MARK: - Window lifecycle

void ManageMediaDialog::showEvent(QShowEvent *event)
{
	QDialog::showEvent(event);
	raise();
	activateWindow();
}

// MARK: - Destination chooser

void ManageMediaDialog::onChooseDestination()
{
	const QString dir = QFileDialog::getExistingDirectory(
	    this, tr("Choose destination folder"));
	if (!dir.isEmpty())
		m_destPath->setText(dir);
}

// MARK: - Operation switch

void ManageMediaDialog::onOperationChanged()
{
	const bool isDel = (operation() == Operation::Delete);
	m_destWidget->setVisible(!isDel);

	switch (operation())
	{
	case Operation::Copy:
		m_btnExecute->setText(tr("Copy"));
		break;
	case Operation::Move:
		m_btnExecute->setText(tr("Move"));
		break;
	case Operation::Delete:
		m_btnExecute->setText(tr("Delete"));
		break;
	}

	updatePreview();
}

// MARK: - Global conflict policy cascade

void ManageMediaDialog::onGlobalConflictPolicyChanged(int index)
{
	// 'Mixed' is set only programmatically — never push it down to
	// per-file combos when the user lands on it.
	if (m_conflictGlobalCombo->itemData(index).toInt() == -1)
		return;

	const ConflictPolicy policy = static_cast<ConflictPolicy>(
	    m_conflictGlobalCombo->itemData(index).toInt());

	// Walk conflict rows directly — only they own a combo at
	// column 2. The signal blocker stops each per-file change from
	// re-firing syncGlobalFromPerFile, which would otherwise bounce
	// us back to 'Mixed' mid-iteration. Update the destination
	// preview ourselves instead.
	for (int i = 0; i < m_previewTree->topLevelItemCount(); ++i)
	{
		QTreeWidgetItem *item = m_previewTree->topLevelItem(i);
		auto *combo = qobject_cast<QComboBox *>(m_previewTree->itemWidget(item, 2));
		if (!combo)
			continue;
		const QSignalBlocker blocker(combo);
		combo->setCurrentIndex(index);
		applyConflictPolicyToRow(item, item->data(1, Qt::UserRole).toString(), policy);
	}
}

// MARK: - Preview rebuild

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
		m_previewTree->setHeaderLabels({tr("Source")});
		m_previewTree->setColumnCount(1);
		for (const MediaFile &mf : m_files)
			m_previewTree->addTopLevelItem(new QTreeWidgetItem({mf.filePath}));
		m_conflictGroup->setVisible(false);
	}
	else
	{
		m_previewTree->setHeaderLabels({tr("Source"), tr("Destination")});
		m_previewTree->setColumnCount(2);

		if (dest.isEmpty())
		{
			for (const MediaFile &mf : m_files)
			{
				auto *item = new QTreeWidgetItem(
				    {mf.filePath, tr("(choose destination)")});
				item->setForeground(1, Qt::gray);
				m_previewTree->addTopLevelItem(item);
			}
		}
		else
		{
			// New per-file combos: default to 'Keep Both' if global is
			// 'Mixed', otherwise inherit global.
			const int globalIdx = (m_conflictGlobalCombo->currentData().toInt() == -1)
			                          ? 0
			                          : m_conflictGlobalCombo->currentIndex();

			struct RowInfo
			{
				QTreeWidgetItem *item;
				QString sourcePath;
				QString baseDest;
				bool conflict;
			};
			QVector<RowInfo> rows;
			rows.reserve(m_files.size());

			for (const MediaFile &mf : m_files)
			{
				const QString dp = MediaManager::buildDestPath(mf, dest, preserve);
				const bool conflict = QFileInfo::exists(dp);
				auto *item = new QTreeWidgetItem({mf.filePath, dp});

				// Stash baseDest on the item so the global-cascade handler
				// can recompute the preview without a per-row closure.
				item->setData(1, Qt::UserRole, dp);

				if (conflict)
				{
					item->setForeground(1, Qt::red);
					item->setToolTip(1, tr("File already exists at destination"));
					++conflictCount;
				}
				m_previewTree->addTopLevelItem(item);
				rows.append({item, mf.filePath, dp, conflict});
			}

			if (conflictCount > 0)
			{
				m_previewTree->setHeaderLabels(
				    {tr("Source"), tr("Destination"), tr("If exists")});
				m_previewTree->setColumnCount(3);

				for (const RowInfo &r : rows)
				{
					if (!r.conflict)
						continue;
					auto *combo = new QComboBox;
					combo->addItem(tr("Keep Both"), +ConflictPolicy::KeepBoth);
					combo->addItem(tr("Skip"), +ConflictPolicy::Skip);
					combo->addItem(tr("Replace"), +ConflictPolicy::Replace);
					combo->setCurrentIndex(globalIdx);
					m_previewTree->setItemWidget(r.item, 2, combo);
					m_perFileConflictCombos.insert(r.sourcePath, combo);

					// Reflect initial policy in the destination column.
					applyConflictPolicyToRow(r.item, r.baseDest,
					                         static_cast<ConflictPolicy>(combo->currentData().toInt()));

					connect(combo, &QComboBox::currentIndexChanged,
					        this, [this, item = r.item, baseDest = r.baseDest, combo](int)
					        {
                                applyConflictPolicyToRow(item, baseDest,
                                    static_cast<ConflictPolicy>(combo->currentData().toInt()));
                                syncGlobalFromPerFile(); });
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

	syncGlobalFromPerFile();
	m_previewTree->setSortingEnabled(true);
	updateSummary();
}

// MARK: - Summary line + space check

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
		opVerb = tr("Delete");
		break;
	}

	m_summaryLabel->setText(
	    tr("%1 %2 file%3 (%4)")
	        .arg(opVerb)
	        .arg(count)
	        .arg(count == 1 ? "" : "s")
	        .arg(Format::bytes(totalBytes)));

	bool canExecute = !m_files.isEmpty();
	const QString dest = m_destPath->text();

	if (operation() != Operation::Delete && dest.isEmpty())
		canExecute = false;

	// Block Copy/Move if the destination volume can't fit it all.
	if (operation() != Operation::Delete && !dest.isEmpty())
	{
		const QStorageInfo storage(dest);
		if (storage.isValid() && totalBytes > storage.bytesAvailable())
		{
			m_spaceWarning->setText(
			    tr("Insufficient space: %1 needed, %2 free on destination volume")
			        .arg(Format::bytes(totalBytes))
			        .arg(Format::bytes(storage.bytesAvailable())));
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

// MARK: - Reverse sync (per-file to global)

void ManageMediaDialog::syncGlobalFromPerFile()
{
	if (m_perFileConflictCombos.isEmpty())
		return;

	// All per-file combos agree: mirror to global.
	// Any disagreement: flip global to 'Mixed'.
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

	const QSignalBlocker blocker(m_conflictGlobalCombo);
	m_conflictGlobalCombo->setCurrentIndex(
	    (allSame && commonIdx >= 0) ? commonIdx
	                                : m_conflictGlobalCombo->findData(-1));
}

// MARK: - Result accessors

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