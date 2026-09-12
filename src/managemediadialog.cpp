#include "managemediadialog.h"
#include "enumutil.h"
#include "formatutil.h"
#include "opmanager.h"
#include "operationplan.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
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
#include <QtConcurrent>

// MARK: - Row repaint helper

// 'Keep Both' renders the renamed \"name (2)\" destination; 'Skip'
// keep the original path, painted red.
//
// Rename hints always come from the background sweep; repainting does no disk I/O.
static void applyConflictPolicyToRow(QTreeWidgetItem *item, const QString &baseDest,
									 ManageMediaDialog::ConflictPolicy policy,
									 const QString &renamed)
{
	if (policy == ManageMediaDialog::ConflictPolicy::KeepBoth)
	{
		if (!renamed.isNull())
		{
			item->setText(1, renamed);
			item->setForeground(1, QBrush());
			item->setToolTip(1, QString());
			return;
		}
	}
	item->setText(1, baseDest);
	item->setForeground(1, Qt::red);
	item->setToolTip(
		1, policy == ManageMediaDialog::ConflictPolicy::KeepBoth
			   ? ManageMediaDialog::tr("No unique name available (999 duplicate names already used)")
			   : ManageMediaDialog::tr("File already exists at destination"));
}

// MARK: - Construction

ManageMediaDialog::ManageMediaDialog(const QVector<MediaFile> &files, QWidget *parent,
									 Operation initialOp)
	: QDialog(parent),
	  m_files(files)
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

	opLayout->addWidget(
		makeOpRow(m_radioCopy, tr("Copy"),
				  tr("Copy the selected files to a new location. "
					 "Originals are untouched.")));
	opLayout->addWidget(
		makeOpRow(m_radioMove, tr("Move"),
				  tr("Move the selected files to a new location. When copying is needed, "
					 "all originals are kept until every required copy succeeds.")));
	opLayout->addWidget(
		makeOpRow(m_radioDelete, tr("Delete"),
				  tr("Move the selected files to the system Trash. Network and NEXIS drives "
					 "use MediaMuster Trash on the same drive. Emptying Trash frees space.")));

	m_radioCopy->setChecked(true);

	m_opGroup = new QButtonGroup(this);
	m_opGroup->addButton(m_radioCopy, Enum::to_underlying(Operation::Copy));
	m_opGroup->addButton(m_radioMove, Enum::to_underlying(Operation::Move));
	m_opGroup->addButton(m_radioDelete, Enum::to_underlying(Operation::Delete));

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

	m_chkPreserve =
		new QCheckBox(tr("Preserve Avid folder structure (Avid MediaFiles/MXF/<N>/...)"));
	m_chkPreserve->setChecked(false);
	destOuter->addWidget(m_chkPreserve);

	root->addWidget(m_destWidget);

	// MARK: Conflicts group

	m_conflictGroup = new QGroupBox(tr("Conflicts"));
	auto *conflictLayout = new QHBoxLayout(m_conflictGroup);

	auto *conflictLabel =
		new QLabel(tr("Some files already exist at the destination. Apply to all:"));
	conflictLabel->setWordWrap(true);
	conflictLayout->addWidget(conflictLabel, 1);

	m_conflictGlobalCombo = new QComboBox;
	m_conflictGlobalCombo->addItem(tr("Keep Both"),
								   Enum::to_underlying(ConflictPolicy::KeepBoth));
	m_conflictGlobalCombo->addItem(tr("Skip"), Enum::to_underlying(ConflictPolicy::Skip));

	m_conflictGlobalCombo->addItem(tr("— Mixed —"), -1);

	// 'Mixed' is informational; set programmatically when per-file combos diverge.
	if (auto *model = qobject_cast<QStandardItemModel *>(m_conflictGlobalCombo->model()))
		model->item(m_conflictGlobalCombo->count() - 1)->setFlags(Qt::NoItemFlags);
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

	connect(m_btnChoose, &QPushButton::clicked, this, &ManageMediaDialog::onChooseDestination);
	connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
	connect(m_btnExecute, &QPushButton::clicked, this, &QDialog::accept);

	connect(m_opGroup, &QButtonGroup::idToggled, this, [this](int, bool)
			{ onOperationChanged(); });
	connect(m_destPath, &QLineEdit::textChanged, this,
			[this](const QString &)
			{ updatePreview(); });
	connect(m_chkPreserve, &QCheckBox::toggled, this, [this](bool)
			{ updatePreview(); });
	connect(m_conflictGlobalCombo, &QComboBox::currentIndexChanged, this,
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
	const QString dir =
		QFileDialog::getExistingDirectory(this, tr("Choose destination folder"));
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

	// A destructive action must never be the default (Return) button.
	// For Delete, Return lands on Cancel instead; Copy/Move restore the
	// affirmative default. Matters because the dialog can open straight
	// into Delete mode (⌦ shortcut, context menu) and this dialog is
	// the only confirmation before the operation runs.
	m_btnExecute->setDefault(!isDel);
	m_btnCancel->setDefault(isDel);

	updatePreview();
}

// MARK: - Global conflict policy cascade

void ManageMediaDialog::onGlobalConflictPolicyChanged(int index)
{
	// 'Mixed' is set only programmatically; never push it down to
	// per-file combos when the user lands on it.
	if (m_conflictGlobalCombo->itemData(index).toInt() == -1)
		return;

	if (m_checkingDest)
	{
		updatePreview();
		return;
	}
	const ConflictPolicy policy =
		static_cast<ConflictPolicy>(m_conflictGlobalCombo->itemData(index).toInt());

	// Walk conflict rows directly; only they own a combo at
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
		applyConflictPolicyToRow(item, item->data(1, Qt::UserRole).toString(), policy,
			item->data(1, Qt::UserRole + 1).toString());
	}
	startDestinationCheck(false);
}

// MARK: - Preview rebuild

void ManageMediaDialog::updatePreview()
{
	// Invalidate old worker results before destroying their preview rows.
	++m_destCheckGeneration;
	m_previewTree->setSortingEnabled(false);
	m_previewTree->clear();
	m_perFileConflictCombos.clear();

	m_checkingDest = false;
	m_assessment = {};
	m_availableBytes = -1;
	m_pendingRows.clear();

	const Operation op = operation();
	const QString dest = m_destPath->text();
	const bool preserve = m_chkPreserve->isChecked();

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
				auto *item = new QTreeWidgetItem({mf.filePath, tr("(choose destination)")});
				item->setForeground(1, Qt::gray);
				m_previewTree->addTopLevelItem(item);
			}
		}
		else
		{
			// Pure, local pass: compute every destination and spot two
			// selected files that resolve to the *same* path (folder
			// flattening reuses filenames). No disk I/O on this thread — the
			// exists()/rename probes run on a pool thread below, because
			// thousands of synchronous stats against an SMB/Nexis share froze
			// the dialog on every option change.
			QVector<QString> destPaths;
			destPaths.reserve(m_files.size());
			QHash<QString, int> destCounts;
			for (const MediaFile &mf : m_files)
			{
				const QString dp = OperationPlan::destinationPath(mf.fileName, mf.mediaFolderName, dest, preserve, mf.omfEra);
				destPaths.append(dp);
				++destCounts[dp];
			}

			// Duplicate ordinals in m_files order — the order the engine
			// processes them. The first file to claim a clashing name keeps
			// it; later ones are renamed. Mirrors the runner's claimDestination
			// so the preview shows what actually happens.
			QHash<QString, int> seenSoFar;
			m_pendingRows.reserve(m_files.size());
			for (int idx = 0; idx < m_files.size(); ++idx)
			{
				const MediaFile &mf = m_files[idx];
				const QString &dp = destPaths[idx];
				auto *item = new QTreeWidgetItem({mf.filePath, dp});
				// Stash baseDest on the item so the global-cascade handler
				// can recompute the preview without a per-row closure.
				item->setData(1, Qt::UserRole, dp);
				m_previewTree->addTopLevelItem(item);

				PendingRow row;
				row.item = item;
				row.sourcePath = mf.filePath;
				row.destPath = dp;
				const bool dup = destCounts.value(dp) > 1;
				row.dupFirst = dup && seenSoFar.value(dp, 0) == 0;
				row.dupLater = dup && seenSoFar.value(dp, 0) > 0;
				m_pendingRows.append(row);
				++seenSoFar[dp];
			}

			startDestinationCheck(true);
		}

		// Conflict styling, the per-row combos, and the group's visibility
		// arrive with the sweep results in applyDestinationCheck.
		m_conflictGroup->setVisible(false);
	}

	syncGlobalFromPerFile();
	m_previewTree->setSortingEnabled(true);
	updateSummary();
}

// MARK: - Destination sweep results

void ManageMediaDialog::startDestinationCheck(bool includeConflicts)
{
	if (operation() == Operation::Delete || destination().isEmpty()) return;
	const int generation = ++m_destCheckGeneration;
	m_checkingDest = true;
	updateSummary();
	OpRequest request;
	request.kind = operation() == Operation::Copy ? OpKind::Copy : OpKind::Move;
	request.destRoot = destination();
	request.preserve = preserveStructure();
	request.items = OpManager::itemsFromMediaFiles(m_files, conflictPolicies());
	const auto defaultPolicy = conflictPolicyName(
		m_conflictGlobalCombo->currentData().toInt() == Enum::to_underlying(ConflictPolicy::Skip)
			? ConflictPolicy::Skip : ConflictPolicy::KeepBoth);
	QStringList paths;
	QVector<bool> dupLater;
	for (const auto &row : m_pendingRows)
	{
		paths.append(row.destPath);
		dupLater.append(row.dupLater);
	}
	auto *watcher = new QFutureWatcher<DestCheckResult>(this);
	connect(watcher, &QFutureWatcher<DestCheckResult>::finished, this,
		[this, watcher, generation, includeConflicts]
		{
			const auto result = watcher->result();
			watcher->deleteLater();
			if (generation != m_destCheckGeneration) return;
			applyDestinationCheck(result, includeConflicts);
		});
	watcher->setFuture(QtConcurrent::run(
		[request = std::move(request), paths, dupLater, defaultPolicy, includeConflicts]() mutable
		{
			DestCheckResult result;
			QHash<QPair<QString, QString>, bool> sameFiles;
			auto sameFile = [&](const QString &source, const QString &destination)
			{
				const auto paths = qMakePair(source, destination);
				const auto found = sameFiles.constFind(paths);
				if (found != sameFiles.cend()) return found.value();
				const bool same = OperationPlan::alreadyAtDestination(source, destination);
				sameFiles.insert(paths, same);
				return same;
			};
			if (includeConflicts)
			{
				result.exists.reserve(paths.size());
				result.renamed.resize(paths.size());
				for (int i = 0; i < paths.size(); ++i)
				{
					const bool occupied = QFileInfo::exists(paths[i]);
					if (!occupied) sameFiles.insert(qMakePair(request.items[i].src, paths[i]), false);
					const bool unchanged = occupied && sameFile(request.items[i].src, paths[i]);
					result.alreadyAtDestination.append(unchanged);
					const bool exists = occupied && !unchanged;
					result.exists.append(exists);
					if (exists) request.items[i].policy = defaultPolicy;
					if (!unchanged && (exists || dupLater[i]))
						if (const auto renamed = OperationPlan::findKeepBothPath(paths[i]))
							result.renamed[i] = *renamed;
				}
			}
			QHash<QPair<QString, QString>, bool> relocationByFolders;
			result.assessment = OperationPlan::assessCopyMove(request,
				[&](const QString &source, const QString &destination)
				{
					const auto folders = qMakePair(QFileInfo(source).absolutePath(), QFileInfo(destination).absolutePath());
					const auto found = relocationByFolders.constFind(folders);
					if (found != relocationByFolders.cend()) return found.value();
					const bool canRelocate = OperationPlan::sameVolumeForRename(source, destination);
					relocationByFolders.insert(folders, canRelocate);
					return canRelocate;
				}, false, sameFile);
			const QStorageInfo storage(request.destRoot);
			if (storage.isValid() && storage.isReady()) result.availableBytes = storage.bytesAvailable();
			return result;
		}));
}

void ManageMediaDialog::applyDestinationCheck(const DestCheckResult &result, bool includeConflicts)
{
	m_assessment = result.assessment;
	m_availableBytes = result.availableBytes;
	if (!includeConflicts)
	{
		m_checkingDest = false;
		updateSummary();
		return;
	}
	if (result.exists.size() != m_pendingRows.size()) return;

	// New per-file combos: default to 'Keep Both' if global is 'Mixed',
	// otherwise inherit global.
	const int globalIdx = (m_conflictGlobalCombo->currentData().toInt() == -1)
							  ? 0
							  : m_conflictGlobalCombo->currentIndex();

	int onDiskConflicts = 0;
	QVector<int> conflictRows;
	for (int i = 0; i < m_pendingRows.size(); ++i)
	{
		const PendingRow &row = m_pendingRows[i];
		if (result.alreadyAtDestination[i])
		{
			row.item->setToolTip(1, tr("Already at destination; no change needed."));
			continue;
		}
		if (result.exists[i])
		{
			// A real on-disk conflict: the user gets Keep Both / Skip
			// (combo added below). A row that's also a batch
			// dup still counts as on-disk — there really is a file at the
			// destination to act on.
			row.item->setForeground(1, Qt::red);
			row.item->setToolTip(1, tr("File already exists at destination"));
			++onDiskConflicts;
			conflictRows.append(i);
		}
		else if (row.dupLater)
		{
			// Clashes only with another *selected* file, not with disk.
			// The engine always keeps both here (the later file is renamed)
			// so the preview shows the automatic Keep Both result.
			// With 3+ identical names the preview can repeat a suffix (it
			// can't see files that don't exist yet), but the outcome — kept,
			// renamed — is faithful.
			const QString &renamed = result.renamed[i];
			row.item->setText(1, renamed.isNull() ? row.destPath : renamed);
			row.item->setToolTip(1, renamed.isNull()
										? tr("Another selected file targets this name "
											 "and all duplicate names are taken.")
										: tr("Renamed to keep both — another selected "
											 "file already targets this name."));
		}
		else if (row.dupFirst)
		{
			row.item->setToolTip(1, tr("Kept — another selected file has the same "
									   "name; the later copy is renamed."));
		}
	}

	if (onDiskConflicts > 0)
	{
		m_previewTree->setHeaderLabels({tr("Source"), tr("Destination"), tr("If exists")});
		m_previewTree->setColumnCount(3);

		for (int i : conflictRows)
		{
			const PendingRow &row = m_pendingRows[i];
			auto *combo = new QComboBox;
			combo->addItem(tr("Keep Both"), Enum::to_underlying(ConflictPolicy::KeepBoth));
			combo->addItem(tr("Skip"), Enum::to_underlying(ConflictPolicy::Skip));
			combo->setCurrentIndex(globalIdx);
			m_previewTree->setItemWidget(row.item, 2, combo);
			m_perFileConflictCombos.insert(row.sourcePath, combo);

			row.item->setData(1, Qt::UserRole + 1, result.renamed[i]);
			// Initial paint uses the sweep's precomputed rename so N
			// conflicted rows don't re-run the probe chain on the UI thread.
			applyConflictPolicyToRow(row.item, row.destPath,
									 static_cast<ConflictPolicy>(combo->currentData().toInt()),
									 result.renamed[i]);

			connect(combo, &QComboBox::currentIndexChanged, this,
					[this, item = row.item, baseDest = row.destPath, combo](int)
					{
						applyConflictPolicyToRow(
							item, baseDest,
							static_cast<ConflictPolicy>(combo->currentData().toInt()),
							item->data(1, Qt::UserRole + 1).toString());
						syncGlobalFromPerFile();
						startDestinationCheck(false);
					});
		}

		m_conflictGroup->setTitle(
			tr("Conflicts (%1 of %2 files)")
				.arg(Format::count(onDiskConflicts), Format::count(m_files.size())));
	}
	m_conflictGroup->setVisible(onDiskConflicts > 0);

	m_checkingDest = false;
	syncGlobalFromPerFile();
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

	QString summary =
		tr("%1 %n file(s) (%2)", nullptr, count).arg(opVerb, Format::bytes(totalBytes));
	if (m_checkingDest)
		summary += tr(" — checking destination...");
	m_summaryLabel->setText(summary);

	bool canExecute = !m_files.isEmpty();
	// Hold Execute while the destination sweep is in flight: launching a
	// Copy/Move before conflicts are known could skip something the user
	// never got the chance to see.
	if (m_checkingDest)
		canExecute = false;
	const QString dest = m_destPath->text();

	if (operation() != Operation::Delete && dest.isEmpty())
		canExecute = false;

	// The worker supplied both the whole-job estimate and available capacity.
	// Changing a policy starts another check; this paint never probes storage.
	if (!m_checkingDest && operation() != Operation::Delete && !dest.isEmpty() &&
		m_availableBytes >= 0 && m_assessment.temporaryBytes > m_availableBytes)
	{
		m_spaceWarning->setText(tr("Insufficient space: %1 needed, %2 free on destination volume")
			.arg(Format::bytes(m_assessment.temporaryBytes)).arg(Format::bytes(m_availableBytes)));
		m_spaceWarning->setVisible(true);
		canExecute = false;
	}
	else
		m_spaceWarning->setVisible(false);

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
		(allSame && commonIdx >= 0) ? commonIdx : m_conflictGlobalCombo->findData(-1));
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

QHash<QString, ManageMediaDialog::ConflictPolicy> ManageMediaDialog::conflictPolicies() const
{
	QHash<QString, ConflictPolicy> out;
	for (auto it = m_perFileConflictCombos.constBegin(); it != m_perFileConflictCombos.constEnd();
		 ++it)
	{
		out.insert(it.key(), static_cast<ConflictPolicy>(it.value()->currentData().toInt()));
	}
	return out;
}