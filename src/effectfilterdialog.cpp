#include "effectfilterdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

EffectFilterDialog::EffectFilterDialog(const QVector<MediaFile> &files,
	const QStringList &selectedEffects, const QString &selectedVolume, QWidget *parent)
	: QDialog(parent), m_files(files),
	  m_selectedEffects(selectedEffects.cbegin(), selectedEffects.cend())
{
	setWindowTitle(tr("Filter by Effect"));
	resize(600, 500);
	auto *layout = new QVBoxLayout(this);
	auto *explanation = new QLabel(tr("Choose effects and a volume to show matching precomputes. "
		"Counts refer to the scanned volume; other table filters still apply."), this);
	explanation->setWordWrap(true);
	layout->addWidget(explanation);

	auto *volumeLabel = new QLabel(tr("&Volume:"), this);
	m_volumes = new QComboBox(this);
	m_volumes->setObjectName(QStringLiteral("effectVolume"));
	volumeLabel->setBuddy(m_volumes);
	m_volumes->addItem(tr("All scanned volumes"), QString{});
	QMap<QString, QString> volumes;
	QMap<QString, int> nameCounts;
	for (const auto &file : m_files)
		if (!file.volumePath.isEmpty() && !volumes.contains(file.volumePath))
		{
			const QString name = file.volumeName.isEmpty() ? file.volumePath : file.volumeName;
			volumes.insert(file.volumePath, name);
			++nameCounts[name];
		}
	for (auto it = volumes.cbegin(); it != volumes.cend(); ++it)
	{
		const QString label = nameCounts[it.value()] > 1
			? tr("%1 (%2)").arg(it.value(), it.key()) : it.value();
		m_volumes->addItem(label, it.key());
		m_volumes->setItemData(m_volumes->count() - 1, it.key(), Qt::ToolTipRole);
	}
	const int volumeIndex = m_volumes->findData(selectedVolume);
	m_volumes->setCurrentIndex(volumeIndex >= 0 ? volumeIndex : 0);
	layout->addWidget(volumeLabel);
	layout->addWidget(m_volumes);

	m_search = new QLineEdit(this);
	m_search->setObjectName(QStringLiteral("effectSearch"));
	m_search->setPlaceholderText(tr("Search effects or categories"));
	m_search->setClearButtonEnabled(true);
	layout->addWidget(m_search);

	m_effects = new QTreeWidget(this);
	m_effects->setObjectName(QStringLiteral("effectChoices"));
	m_effects->setHeaderLabels({tr("Effect"), tr("Category"), tr("Files")});
	m_effects->setRootIsDecorated(false);
	m_effects->setAlternatingRowColors(true);
	m_effects->setSelectionMode(QAbstractItemView::NoSelection);
	m_effects->header()->setStretchLastSection(false);
	m_effects->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_effects->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	m_effects->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	layout->addWidget(m_effects, 1);

	m_summary = new QLabel(this);
	m_summary->setObjectName(QStringLiteral("effectSelectionSummary"));
	layout->addWidget(m_summary);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
	auto *clear = buttons->addButton(tr("Clear Filter"), QDialogButtonBox::ResetRole);
	clear->setObjectName(QStringLiteral("clearEffectFilter"));
	connect(clear, &QPushButton::clicked, this, [this]() {
		m_selectedEffects.clear();
		m_volumes->setCurrentIndex(0);
		accept();
	});
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	connect(m_volumes, qOverload<int>(&QComboBox::currentIndexChanged), this,
		[this](int) { rebuildEffects(); });
	connect(m_search, &QLineEdit::textChanged, this,
		[this](const QString &) { filterChoices(); });
	connect(m_effects, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
		if (column != 0) return;
		const QString name = item->data(0, Qt::UserRole).toString();
		if (item->checkState(0) == Qt::Checked) m_selectedEffects.insert(name);
		else m_selectedEffects.remove(name);
		updateSelectionCount();
	});
	rebuildEffects();
	m_search->setFocus();
}

QStringList EffectFilterDialog::selectedEffects() const
{
	QStringList names = m_selectedEffects.values();
	names.sort();
	return names;
}

QString EffectFilterDialog::selectedVolume() const
{
	return m_volumes->currentData().toString();
}

void EffectFilterDialog::rebuildEffects()
{
	struct Count { int files = 0; QSet<QString> categories; };
	QMap<QString, Count> counts;
	const QString volume = selectedVolume();
	for (const auto &file : m_files)
	{
		if (file.type != MediaFile::Type::Precompute || file.effect.isEmpty() ||
			(!volume.isEmpty() && file.volumePath != volume)) continue;
		auto &count = counts[file.effect];
		++count.files;
		if (!file.effectCategory.isEmpty()) count.categories.insert(file.effectCategory);
	}
	const auto names = counts.keys();
	m_selectedEffects.intersect(QSet<QString>(names.cbegin(), names.cend()));
	const QSignalBlocker block(m_effects);
	m_effects->clear();
	for (auto it = counts.cbegin(); it != counts.cend(); ++it)
	{
		QStringList categories = it->categories.values();
		categories.sort();
		auto *item = new QTreeWidgetItem(m_effects,
			{it.key(), categories.join(QStringLiteral(" / ")), QString::number(it->files)});
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
		item->setData(0, Qt::UserRole, it.key());
		item->setCheckState(0, m_selectedEffects.contains(it.key()) ? Qt::Checked : Qt::Unchecked);
		item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
		item->setToolTip(0, it.key());
		item->setToolTip(1, item->text(1));
	}
	filterChoices();
}

void EffectFilterDialog::filterChoices()
{
	const QString query = m_search->text().normalized(QString::NormalizationForm_C);
	for (int i = 0; i < m_effects->topLevelItemCount(); ++i)
	{
		auto *item = m_effects->topLevelItem(i);
		const bool matches = item->text(0).normalized(QString::NormalizationForm_C).contains(query, Qt::CaseInsensitive) ||
			item->text(1).normalized(QString::NormalizationForm_C).contains(query, Qt::CaseInsensitive);
		item->setHidden(!matches);
	}
	updateSelectionCount();
}

void EffectFilterDialog::updateSelectionCount()
{
	if (m_effects->topLevelItemCount() == 0)
		m_summary->setText(tr("No named precomputes in this volume."));
	else if (m_selectedEffects.isEmpty())
		m_summary->setText(selectedVolume().isEmpty()
			? tr("No effects selected — Apply clears this filter.")
			: tr("No effects selected — Apply shows all precomputes on this volume."));
	else
		m_summary->setText(tr("%n effect(s) selected", nullptr, m_selectedEffects.size()));
}
