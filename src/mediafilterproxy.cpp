#include "mediafilterproxy.h"
#include "mediafile.h"
#include "mediatablemodel.h"

#include <algorithm>
#include <utility>

// MARK: - Numeric sort helpers

namespace
{
	// Preserve Audio/Video order and group unresolved kinds afterwards.
	// Duration ties use the same total order; treating Unknown as equivalent
	// to both Audio and Video would violate the sort's strict weak ordering.
	int kindSortRank(MediaFile::Kind kind)
	{
		switch (kind)
		{
		case MediaFile::Kind::Audio:
			return 0;
		case MediaFile::Kind::Video:
			return 1;
		case MediaFile::Kind::Unknown:
			return 2;
		}
		return 2;
	}
	int typeSortRank(MediaFile::Type type)
	{
		switch (type)
		{
		case MediaFile::Type::Media:
			return 0;
		case MediaFile::Type::Precompute:
			return 1;
		case MediaFile::Type::Unknown:
			return 2;
		}
		return 2;
	}

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

	std::pair<qint64, qint64> bellaResolutionSortValue(const MediaFile &file)
	{
		if (!file.bellaValues || !file.bellaValues->resolution)
			return {0, 0};
		const auto &resolution = *file.bellaValues->resolution;
		return {resolution.width, resolution.height};
	}
} // namespace

MediaFilterProxy::MediaFilterProxy(QObject *parent)
	: QSortFilterProxyModel(parent)
{
}

void MediaFilterProxy::setSourceModel(QAbstractItemModel *sourceModel)
{
	QSortFilterProxyModel::setSourceModel(sourceModel);
	m_sourceModel = qobject_cast<MediaTableModel *>(sourceModel);
}

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

bool MediaFilterProxy::matchesMode(FilterMode mode, const MediaFile &f)
{
	switch (mode)
	{
	case FilterMode::All:
		return true;
	case FilterMode::Video:
		return f.kind == MediaFile::Kind::Video;
	case FilterMode::Audio:
		return f.kind == MediaFile::Kind::Audio;
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
		return f.type == MediaFile::Type::Precompute;
	}
	return true;
}

bool MediaFilterProxy::filterAcceptsRow(int row, const QModelIndex &parent) const
{
	Q_UNUSED(parent);
	if (!m_sourceModel || row >= m_sourceModel->allFiles().size())
		return false;
	const MediaFile &f = m_sourceModel->fileAt(row);

	if (!matchesMode(m_mode, f))
		return false;

	// The sidebar hands over displayed names, so "No project" selects the
	// rows whose project is empty.
	if (!m_selectedProjects.isEmpty() && !m_selectedProjects.contains(f.projectDisplay()))
		return false;

	if (m_precomputesEnabled && !m_precomputeTreeFilter.matches(f))
		return false;

	if (m_precomputesEnabled && !m_precomputeVolumePath.isEmpty() &&
		(f.type != MediaFile::Type::Precompute || f.volumePath != m_precomputeVolumePath))
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
			   (m_precomputesEnabled && f.type == MediaFile::Type::Precompute &&
				(matches(f.precomputeCategoryFilterLabel()) || matches(f.effectDisplay()) ||
				 matches(f.effectCategoryDisplay())));
	}
	return true;
}

bool MediaFilterProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
	// Compare stored fields directly to avoid converting each value through QVariant.
	if (!m_sourceModel)
		return QSortFilterProxyModel::lessThan(left, right);

	const MediaFile &leftFile = m_sourceModel->fileAt(left.row());
	const MediaFile &rightFile = m_sourceModel->fileAt(right.row());

	if (m_sourceModel->omfScanEnabled() && left.column() == m_sourceModel->omfScanColumn())
		return leftFile.omfEra < rightFile.omfEra;

	using Col = MediaTableModel::Column;
	switch (m_sourceModel->columnAt(left.column()))
	{
	case Col::SizeMB:
		// Exact integer compare; the MB string is display-only.
		return leftFile.sizeBytes < rightFile.sizeBytes;

	case Col::Created:
	{
		// Invalid (unknown) datetimes sort before every valid one, so blank
		// rows group together predictably.
		return leftFile.created < rightFile.created;
	}
	case Col::ClipName:
		// The exact string the column displays; shared rule, can't drift.
		return QString::compare(leftFile.clipNameDisplay(), rightFile.clipNameDisplay(),
								Qt::CaseInsensitive) < 0;

	case Col::Compression:
		return QString::compare(leftFile.compression, rightFile.compression,
								Qt::CaseInsensitive) < 0;

	case Col::Kind:
		return kindSortRank(leftFile.kind) < kindSortRank(rightFile.kind);

	case Col::Duration:
	{
		// Compare the displayed HH, MM, SS, FF; equal values sort audio first.
		const auto ls = leftFile.durationTimecode();
		const auto rs = rightFile.durationTimecode();
		if (ls != rs)
			return ls < rs;
		return kindSortRank(leftFile.kind) < kindSortRank(rightFile.kind);
	}

	case Col::FileName:
		return QString::compare(leftFile.fileName, rightFile.fileName, Qt::CaseInsensitive) < 0;
	case Col::Project:
		return QString::compare(leftFile.projectDisplay(), rightFile.projectDisplay(), Qt::CaseInsensitive) < 0;
	case Col::OriginalBin:
		return QString::compare(leftFile.originalBin, rightFile.originalBin, Qt::CaseInsensitive) < 0;
	case Col::Resolution:
		return bellaResolutionSortValue(leftFile) < bellaResolutionSortValue(rightFile);
	case Col::FrameRate:
		return exactRateLess(leftFile.bellaValues ? leftFile.bellaValues->frameRateValue : std::nullopt,
							 rightFile.bellaValues ? rightFile.bellaValues->frameRateValue : std::nullopt);
	case Col::SampleRate:
		return exactRateLess(leftFile.bellaValues ? leftFile.bellaValues->sampleRateValue : std::nullopt,
							 rightFile.bellaValues ? rightFile.bellaValues->sampleRateValue : std::nullopt);
	case Col::BitDepth:
		return leftFile.bellaDepthBits() < rightFile.bellaDepthBits();
	case Col::Location:
		return QString::compare(leftFile.mediaFilePath, rightFile.mediaFilePath, Qt::CaseInsensitive) < 0;
	case Col::NoDatabase:
		return leftFile.isNoDatabase() < rightFile.isNoDatabase();
	case Col::NoReference:
		return leftFile.isNoReference() < rightFile.isNoReference();
	case Col::KelpieId:
		return leftFile.kelpieId < rightFile.kelpieId;
	case Col::FileMobId:
		return leftFile.fileMobId < rightFile.fileMobId;
	case Col::MasterMobId:
		return leftFile.masterMobId < rightFile.masterMobId;
	case Col::Type:
		return typeSortRank(leftFile.type) < typeSortRank(rightFile.type);
	case Col::PrecomputeCategory:
	case Col::Effect:
	case Col::EffectCategory:
		// Model data applies the same precompute-only display rule.
		return QString::compare(left.data().toString(), right.data().toString(), Qt::CaseInsensitive) < 0;
	case Col::Count_:
		break;
	}
	return false;
}
