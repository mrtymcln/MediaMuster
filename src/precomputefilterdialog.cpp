#include "precomputefilterdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
	bool covers(const PrecomputeFilterPath &parent, const PrecomputeFilterPath &child)
	{
		return (parent.precomputeCategory.isEmpty() || parent.precomputeCategory == child.precomputeCategory) &&
			   (parent.effectCategory.isEmpty() || parent.effectCategory == child.effectCategory) &&
			   (parent.effect.isEmpty() || parent.effect == child.effect);
	}
}

PrecomputeFilterDialog::PrecomputeFilterDialog(const QVector<MediaFile> &files,
											   const PrecomputeFilter &selection, const QString &selectedVolume, QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Filter Precomputes"));
	setMinimumSize(380, 330);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(20, 20, 20, 20);
	layout->setSpacing(12);

	m_volumes = new QComboBox(this);
	m_volumes->setObjectName(QStringLiteral("precomputeVolume"));
	m_volumes->setMinimumWidth(220);
	m_volumes->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_volumes->setMinimumContentsLength(14);
	m_volumes->setAccessibleName(tr("Volume"));
	m_volumes->addItem(tr("all"), QString{});
	QMap<QString, QString> volumes;
	QMap<QString, int> nameCounts;
	for (const auto &file : files)
		if (!file.volumePath.isEmpty() && !volumes.contains(file.volumePath))
		{
			const QString name = file.volumeName.isEmpty() ? file.volumePath : file.volumeName;
			volumes.insert(file.volumePath, name);
			++nameCounts[name];
		}
	for (auto it = volumes.cbegin(); it != volumes.cend(); ++it)
	{
		const QString label = nameCounts[it.value()] > 1
								  ? tr("%1 (%2)").arg(it.value(), it.key())
								  : it.value();
		m_volumes->addItem(label, it.key());
		m_volumes->setItemData(m_volumes->count() - 1, it.key(), Qt::ToolTipRole);
	}
	// Keep a previously selected, disconnected volume explicit. Replacing it
	// with All volumes would silently widen the editor's filter.
	if (!selectedVolume.isEmpty() && m_volumes->findData(selectedVolume) < 0)
		m_volumes->addItem(selectedVolume, selectedVolume);
	m_volumes->setCurrentIndex(std::max(0, m_volumes->findData(selectedVolume)));
	auto *volumeLabel = new QLabel(tr("Volume:"), this);
	volumeLabel->setBuddy(m_volumes);
	auto *volumeRow = new QHBoxLayout;
	volumeRow->setSpacing(8);
	volumeRow->addWidget(volumeLabel);
	volumeRow->addWidget(m_volumes);
	volumeRow->addStretch();
	layout->addLayout(volumeRow);

	m_tree = new QTreeWidget(this);
	m_tree->setObjectName(QStringLiteral("precomputeChoices"));
	m_tree->setAccessibleName(tr("Precompute hierarchy"));
	m_tree->setColumnCount(1);
	m_tree->setHeaderHidden(true);
	m_tree->setRootIsDecorated(true);
	m_tree->setAlternatingRowColors(false);
	m_tree->setUniformRowHeights(true);
	m_tree->setIndentation(18);
	m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
	m_tree->setAllColumnsShowFocus(true);
	m_tree->header()->setStretchLastSection(false);
	m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	layout->addWidget(m_tree, 1);

	auto *footer = new QHBoxLayout;
	m_matchCount = new QLabel(this);
	m_matchCount->setObjectName(QStringLiteral("precomputeMatchingCount"));
	footer->addWidget(m_matchCount);
	footer->addStretch();
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
	buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("applyPrecomputeFilter"));
	buttons->button(QDialogButtonBox::Ok)->setDefault(true);
	buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("cancelPrecomputeFilter"));
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	footer->addWidget(buttons);
	layout->addLayout(footer);

	buildTree(files);
	applySelection(selection);
	connect(m_volumes, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int)
			{ updateMatchingCount(); });
	connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column)
			{
		if (column != 0) return;
		const QSignalBlocker blocker(m_tree);
		setSubtreeChecked(item, item->checkState(0) == Qt::Unchecked ? Qt::Unchecked : Qt::Checked);
		for (auto *parentItem = item->parent(); parentItem; parentItem = parentItem->parent())
			updateParentChecks(parentItem);
		updateMatchingCount(); });
	for (const bool expand : {false, true})
	{
		auto *shortcut = new QShortcut(QKeySequence(expand ? Qt::CTRL | Qt::Key_Right : Qt::CTRL | Qt::Key_Left), m_tree);
		shortcut->setContext(Qt::WidgetShortcut);
		connect(shortcut, &QShortcut::activated, this, [this, expand]
				{
			if (auto *item = m_tree->currentItem()) item->setExpanded(expand); });
	}
	auto *cancelShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Period), this);
	connect(cancelShortcut, &QShortcut::activated, this, &QDialog::reject);
	updateMatchingCount();
	m_volumes->setFocus();
}

QTreeWidgetItem *PrecomputeFilterDialog::addChoice(QTreeWidgetItem *parent, const QString &label,
												   const PrecomputeFilterPath &path)
{
	auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
	item->setText(0, label);
	item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
	item->setCheckState(0, Qt::Unchecked);
	m_paths.insert(item, path);
	m_counts.insert(item, {});
	return item;
}

void PrecomputeFilterDialog::buildTree(const QVector<MediaFile> &files)
{
	const QSignalBlocker blocker(m_tree);
	auto *root = addChoice(nullptr, tr("Precomputes"), {});
	const QString rendered = QStringLiteral("Rendered Effects");
	const QString titles = QStringLiteral("Titles and Matte Keys");
	const QString unknown = QStringLiteral("unknown");
	QMap<QString, QTreeWidgetItem *> typeItems;
	for (const auto &type : {rendered, titles, unknown})
		typeItems.insert(type, addChoice(root, type, {type, {}, {}}));
	QMap<QString, QMap<QString, QTreeWidgetItem *>> categories;
	QMap<QString, QMap<QString, QMap<QString, QTreeWidgetItem *>>> effects;
	for (const auto &file : files)
	{
		if (file.type != MediaFile::Type::Precompute)
			continue;
		const QString type = file.precomputeCategoryDisplay();
		const QString category = type == rendered ? file.effectCategoryDisplay() : QString{};
		const QString effect = file.effectDisplay();
		auto *parent = typeItems.value(type);
		if (!parent)
			continue;
		if (!category.isEmpty())
		{
			auto *&categoryItem = categories[type][category];
			if (!categoryItem)
				categoryItem = addChoice(parent, category, {type, category, {}});
			parent = categoryItem;
		}
		auto *&effectItem = effects[type][category][effect];
		if (!effectItem)
			effectItem = addChoice(parent, effect, {type, category, effect});
		for (auto *item = effectItem; item; item = item->parent())
		{
			auto &counts = m_counts[item];
			++counts.total;
			++counts.volumes[file.volumePath];
		}
	}
	for (auto *typeItem : typeItems)
	{
		typeItem->sortChildren(0, Qt::AscendingOrder);
		for (int i = 0; i < typeItem->childCount(); ++i)
			typeItem->child(i)->sortChildren(0, Qt::AscendingOrder);
	}
	root->setExpanded(true);
	typeItems.value(rendered)->setExpanded(true);
}

void PrecomputeFilterDialog::applySelection(const PrecomputeFilter &selection)
{
	const QSignalBlocker blocker(m_tree);
	const QVector<PrecomputeFilterPath> paths = selection.active ? selection.paths : QVector<PrecomputeFilterPath>{{}};
	for (auto it = m_paths.cbegin(); it != m_paths.cend(); ++it)
	{
		const bool checked = std::any_of(paths.cbegin(), paths.cend(), [&it](const auto &path)
										 { return covers(path, it.value()); });
		it.key()->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
		if (checked && selection.active)
			for (auto *parent = it.key()->parent(); parent; parent = parent->parent())
				parent->setExpanded(true);
	}
	// Children determine mixed states. Work from leaves towards the root.
	const auto update = [this](auto &&self, QTreeWidgetItem *item) -> void
	{
		for (int i = 0; i < item->childCount(); ++i)
			self(self, item->child(i));
		updateParentChecks(item);
	};
	update(update, m_tree->topLevelItem(0));
}

void PrecomputeFilterDialog::setSubtreeChecked(QTreeWidgetItem *item, Qt::CheckState state)
{
	item->setCheckState(0, state);
	for (int i = 0; i < item->childCount(); ++i)
		setSubtreeChecked(item->child(i), state);
}

void PrecomputeFilterDialog::updateParentChecks(QTreeWidgetItem *item)
{
	if (item->childCount() == 0)
		return;
	bool all = true;
	bool any = false;
	for (int i = 0; i < item->childCount(); ++i)
	{
		const auto state = item->child(i)->checkState(0);
		all = all && state == Qt::Checked;
		any = any || state != Qt::Unchecked;
	}
	item->setCheckState(0, all ? Qt::Checked : any ? Qt::PartiallyChecked
												   : Qt::Unchecked);
}

void PrecomputeFilterDialog::collectSelection(QTreeWidgetItem *item, QVector<PrecomputeFilterPath> &paths) const
{
	if (item->checkState(0) == Qt::Checked)
		paths.append(m_paths.value(item));
	else
		for (int i = 0; i < item->childCount(); ++i)
			collectSelection(item->child(i), paths);
}

PrecomputeFilter PrecomputeFilterDialog::precomputeFilter() const
{
	PrecomputeFilter filter;
	filter.active = true;
	collectSelection(m_tree->topLevelItem(0), filter.paths);
	return filter;
}

QString PrecomputeFilterDialog::selectedVolume() const
{
	return m_volumes->currentData().toString();
}

qint64 PrecomputeFilterDialog::matchingCount(QTreeWidgetItem *item) const
{
	if (item->checkState(0) == Qt::Checked)
	{
		const auto counts = m_counts.value(item);
		return selectedVolume().isEmpty() ? counts.total : counts.volumes.value(selectedVolume());
	}
	qint64 count = 0;
	for (int i = 0; i < item->childCount(); ++i)
		count += matchingCount(item->child(i));
	return count;
}

void PrecomputeFilterDialog::updateMatchingCount()
{
	const qint64 count = matchingCount(m_tree->topLevelItem(0));
	m_matchCount->setText(count == 1 ? tr("1 matching file") : tr("%1 matching files").arg(count));
}
