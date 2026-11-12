#include "media_filter_proxy.h"
#include "media/media_file_record.h"
#include "media_table_model.h"

#include <algorithm>
#include <utility>

// MARK: - Numeric sort helpers

namespace
{
	// True when every character is plain ASCII. ASCII has exactly one
	// Unicode form, so normalisation can never change such a string.
	bool isAsciiOnly(const QString &s)
	{
		return std::all_of(s.cbegin(), s.cend(), [](QChar c)
						   { return c.unicode() < 128; });
	}

	// macOS filenames can store accents separately from their letters.
	// NFC makes those names match composed keyboard input; ASCII needs no conversion.
	QString searchForm(const QString &s)
	{
		return isAsciiOnly(s) ? s : s.normalized(QString::NormalizationForm_C);
	}
	bool exactRateLess(const std::optional<Bella::TimingRate> &left, const std::optional<Bella::TimingRate> &right)
	{
		if (!left || !right)
			return !left && right.has_value();
		auto a = *left, b = *right;
		bool reversed = false;
		// Compare integer quotients, then reciprocate the remaining fractions.
		// This avoids overflow when both original terms occupy all 64 bits.
		for (;;)
		{
			const auto aWhole = a.numerator / a.denominator;
			const auto bWhole = b.numerator / b.denominator;
			if (aWhole != bWhole)
				return reversed ? aWhole > bWhole : aWhole < bWhole;
			a.numerator %= a.denominator;
			b.numerator %= b.denominator;
			if (!a.numerator || !b.numerator)
				return reversed ? a.numerator > b.numerator : a.numerator < b.numerator;
			std::swap(a.numerator, a.denominator);
			std::swap(b.numerator, b.denominator);
			reversed = !reversed;
		}
	}

	std::pair<qint64, qint64> bellaResolutionSortValue(const MediaFileRecord &file)
	{
		if (!file.bellaValues || !file.bellaValues->resolution)
			return {0, 0};
		const auto &resolution = *file.bellaValues->resolution;
		return {resolution.width, resolution.height};
	}
} // namespace

// MARK: - Construction and source model

MediaFilterProxy::MediaFilterProxy(QObject *parent)
	: QSortFilterProxyModel(parent)
{
	setSortRole(MediaTableModel::SortRole);
	setSortCaseSensitivity(Qt::CaseInsensitive);
}

void MediaFilterProxy::setSourceModel(QAbstractItemModel *sourceModel)
{
	QSortFilterProxyModel::setSourceModel(sourceModel);
	m_sourceModel = qobject_cast<MediaTableModel *>(sourceModel);
}

// MARK: - Filter settings

void MediaFilterProxy::setFilterMode(FilterMode mode)
{
	const FilterMode selected = !m_precomputesEnabled && mode == FilterMode::Precompute
									? FilterMode::All
									: mode;
	if (m_mode == selected)
		return;
	m_mode = selected;
	invalidateRowsFilter();
}

void MediaFilterProxy::setSearchText(const QString &text)
{
	// Normalised once here, not per row in filterAcceptsRow.
	const QString normalized = searchForm(text);
	if (m_searchNfc == normalized)
		return;
	m_searchNfc = normalized;
	invalidateRowsFilter();
}

void MediaFilterProxy::setProjectFilter(const QSet<QString> &projects)
{
	if (m_selectedProjects == projects)
		return;
	m_selectedProjects = projects;
	invalidateRowsFilter();
}

void MediaFilterProxy::setPrecomputesEnabled(bool enabled)
{
	if (m_precomputesEnabled == enabled)
		return;
	m_precomputesEnabled = enabled;
	if (!enabled)
	{
		if (m_mode == FilterMode::Precompute)
			m_mode = FilterMode::All;
		m_precomputeTreeFilter = {};
		m_precomputeVolumePath.clear();
	}
	invalidate();
}

void MediaFilterProxy::setPrecomputeTreeFilter(const PrecomputeFilter &filter)
{
	if (!m_precomputesEnabled)
		return;
	const PrecomputeFilter selected = filter.active ? filter : PrecomputeFilter{};
	if (m_precomputeTreeFilter == selected)
		return;
	m_precomputeTreeFilter = selected;
	invalidateRowsFilter();
}

void MediaFilterProxy::setPrecomputeVolumeFilter(const QString &volumePath)
{
	if (!m_precomputesEnabled || m_precomputeVolumePath == volumePath)
		return;
	m_precomputeVolumePath = volumePath;
	invalidateRowsFilter();
}

void MediaFilterProxy::setBinFilter(const AvbFilter &filter)
{
	const bool unchanged = m_binFilter.hasSameCriteria(filter);
	m_binFilter = filter;
	if (!unchanged)
		invalidateRowsFilter();
}

// MARK: - Row filtering

bool MediaFilterProxy::matchesMode(FilterMode mode, const MediaFileRecord &f)
{
	switch (mode)
	{
	case FilterMode::All:
		return true;
	case FilterMode::Video:
		return f.kind == MediaFileRecord::Kind::Video;
	case FilterMode::Audio:
		return f.kind == MediaFileRecord::Kind::Audio;
	case FilterMode::NoDatabase:
		return f.isNoDatabase(); // both couldn't-check states; the tooltip says which
	case FilterMode::NoReference:
		return f.isNoReference();
	case FilterMode::NonPortable:
		return f.isNonPortable;
	case FilterMode::Quarantined:
		// Stamped by the scanner, which knows the folder; no path guessing here.
		return f.isQuarantined;
	case FilterMode::Precompute:
		// The usage code's verdict (header or database); never a name shape.
		return f.type == MediaFileRecord::Type::Precompute;
	}
	return true;
}

bool MediaFilterProxy::filterAcceptsRow(int row, const QModelIndex &parent) const
{
	Q_UNUSED(parent);
	if (!m_sourceModel || row >= m_sourceModel->allFiles().size())
		return false;
	const MediaFileRecord &f = m_sourceModel->fileAt(row);

	if (!matchesMode(m_mode, f))
		return false;

	// The sidebar hands over displayed names, so "No project" selects the
	// rows whose project is empty.
	if (!m_selectedProjects.isEmpty() && !m_selectedProjects.contains(f.projectDisplay()))
		return false;

	if (m_precomputesEnabled && !m_precomputeTreeFilter.matches(f))
		return false;

	if (m_precomputesEnabled && !m_precomputeVolumePath.isEmpty() &&
		(f.type != MediaFileRecord::Type::Precompute || f.volumePath != m_precomputeVolumePath))
		return false;

	if (!m_binFilter.matches(f.fileMobId))
		return false;

	if (!m_searchNfc.isEmpty())
	{
		// Case-insensitive comparison folds on the fly. Both
		// sides go through searchForm so an NFD filename matches NFC
		// keyboard input and vice versa; accents themselves stay
		// significant ("cafe" does not match "café" in either form).
		const auto matches = [this](const QString &s)
		{ return searchForm(s).contains(m_searchNfc, Qt::CaseInsensitive); };
		// mediaFilePath covers the filename and the Avid folder — both are
		// substrings of it — so the visible path cell is searchable
		// and the two narrower fields need no separate pass. volumeName
		// stays: a Windows path ("E:/...") need not contain the label.
		return matches(f.clipName) || matches(f.project) || matches(f.originalBin) ||
			   matches(f.compression) || matches(f.volumeName) || matches(f.mediaFilePath) ||
			   (m_precomputesEnabled && f.type == MediaFileRecord::Type::Precompute &&
				(matches(f.precomputeCategoryFilterLabel()) || matches(f.effectDisplay()) ||
				 matches(f.effectCategoryDisplay())));
	}
	return true;
}

// MARK: - Column sorting

bool MediaFilterProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
	if (!m_sourceModel)
		return QSortFilterProxyModel::lessThan(left, right);

	using Col = MediaTableModel::Column;
	const auto column = m_sourceModel->columnAt(left.column());
	switch (column)
	{
	case Col::Duration:
	{
		const auto leftDuration = m_sourceModel->fileAt(left.row()).durationTimecode();
		const auto rightDuration = m_sourceModel->fileAt(right.row()).durationTimecode();
		if (leftDuration != rightDuration)
			return leftDuration < rightDuration;
		const int kindColumn = m_sourceModel->columnIndex(Col::Kind);
		return QSortFilterProxyModel::lessThan(left.siblingAtColumn(kindColumn), right.siblingAtColumn(kindColumn));
	}
	case Col::Resolution:
		return bellaResolutionSortValue(m_sourceModel->fileAt(left.row())) <
			   bellaResolutionSortValue(m_sourceModel->fileAt(right.row()));
	case Col::FrameRate:
	case Col::SampleRate:
	{
		const auto &leftValues = m_sourceModel->fileAt(left.row()).bellaValues;
		const auto &rightValues = m_sourceModel->fileAt(right.row()).bellaValues;
		if (column == Col::FrameRate)
			return exactRateLess(leftValues ? leftValues->frameRateValue : std::nullopt,
								 rightValues ? rightValues->frameRateValue : std::nullopt);
		return exactRateLess(leftValues ? leftValues->sampleRateValue : std::nullopt,
							 rightValues ? rightValues->sampleRateValue : std::nullopt);
	}
	default:
		return QSortFilterProxyModel::lessThan(left, right);
	}
}
