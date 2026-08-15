#include "mediafilterproxy.h"
#include "mediafile.h"
#include "mediatablemodel.h"

#include <QSize>
#include <algorithm>

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

	// The string in the form search comparisons run in (NFC). macOS
	// filesystems often hand back decomposed names — 'é' stored as 'e'
	// plus a combining accent — which render identically to composed
	// keyboard input but fail a plain contains() against it. Comparing
	// NFC-on-NFC makes both forms of either side match; the ASCII fast
	// path skips the allocation for the overwhelmingly common case.
	QString searchForm(const QString &s)
	{
		return isAsciiOnly(s) ? s : s.normalized(QString::NormalizationForm_C);
	}
	// FPS column holds "23.976", "25", "29.97"... for video and stays blank for
	// audio. Sort numerically so 100 sorts after 25 rather than lexically
	// before it; blank audio rows parse to 0 and group together.
	double fpsSortValue(const QString &fps)
	{
		return fps.toDouble();
	}

	// Resolution is "WxH" for video and blank for audio. Parse to a QSize so the
	// sort is by width then height (pixel dimensions), not lexical — otherwise
	// "720x576" sorts after "1920x1080". Non-video parses to (0, 0).
	QSize resolutionSortValue(const QString &res)
	{
		const int x = res.indexOf(QLatin1Char('x'));
		if (x < 0)
			return QSize(0, 0);
		return QSize(res.left(x).toInt(), res.mid(x + 1).toInt());
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
	m_mode = mode;
	invalidateRowsFilter();
}

void MediaFilterProxy::setSearchText(const QString &text)
{
	m_search = text;
	// Normalised once here, not per row in filterAcceptsRow.
	m_searchNfc = searchForm(text);
	invalidateRowsFilter();
}

void MediaFilterProxy::setProjectFilter(const QSet<QString> &projects)
{
	m_selectedProjects = projects;
	invalidateRowsFilter();
}

void MediaFilterProxy::setBinFilterMobs(bool isActive, const QSet<QString> &acceptedMobs)
{
	m_binFilterActive = isActive;
	m_binFilterAcceptedMobs = isActive ? acceptedMobs : QSet<QString>{};
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
	case FilterMode::NoReference:
		return f.isNoReference;
	case FilterMode::NoProject:
		return f.isNoProject;
	case FilterMode::NoDatabase:
		return f.isNoDatabase();
	case FilterMode::BadUmid:
		return f.isBadUmid;
	case FilterMode::NonPortable:
		return f.isNonPortable;
	case FilterMode::Quarantined:
		// Stamped by the scanner, which knows the folder; no path guessing here.
		return f.isQuarantined;
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

	if (!m_selectedProjects.isEmpty() && !m_selectedProjects.contains(f.project))
		return false;

	if (m_binFilterActive)
	{
		const bool hit =
			(!f.mobId.isEmpty() && m_binFilterAcceptedMobs.contains(f.mobId)) ||
			(!f.masterMobId.isEmpty() && m_binFilterAcceptedMobs.contains(f.masterMobId));
		if (!hit)
			return false;
	}

	if (!m_search.isEmpty())
	{
		// Qt::CaseInsensitive folds on the fly during the compare. Both
		// sides go through searchForm so an NFD filename matches NFC
		// keyboard input and vice versa; accents themselves stay
		// significant ("cafe" does not match "café" in either form).
		const auto matches = [this](const QString &s)
		{ return searchForm(s).contains(m_searchNfc, Qt::CaseInsensitive); };
		return matches(f.clipName) || matches(f.project) || matches(f.originalBin) ||
			   matches(f.mxfFolder) || matches(f.codec) || matches(f.volumeName) ||
			   matches(f.fileName);
	}
	return true;
}

bool MediaFilterProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
	// Bypass the data()/QVariant round-trip; sort directly off the
	// MediaFile fields the model exposes. Removes ~12M QVariant
	// constructions per sort of a 300K-row table.
	if (!m_sourceModel)
		return QSortFilterProxyModel::lessThan(left, right);

	const MediaFile &l = m_sourceModel->fileAt(left.row());
	const MediaFile &r = m_sourceModel->fileAt(right.row());

	using Col = MediaTableModel::Column;
	switch (static_cast<Col>(left.column()))
	{
	case Col::SizeMB:
		// Exact integer compare; the MB string is display-only.
		return l.sizeBytes < r.sizeBytes;

	case Col::Created:
	{
		// Invalid (unknown) datetimes sort before every valid one — Qt's
		// documented ordering — so blank rows group together predictably.
		return l.created < r.created;
	}

	case Col::ClipName:
		// The exact string the column displays; shared rule, can't drift.
		return QString::compare(l.clipNameDisplay(), r.clipNameDisplay(),
								Qt::CaseInsensitive) < 0;

	case Col::Codec:
	{
		// The exact string the column displays (with the model's raw-hex
		// toggle); shared rule, can't drift.
		const bool rawHex = m_sourceModel->showRawCodecHex();
		return QString::compare(l.codecDisplay(rawHex), r.codecDisplay(rawHex),
								Qt::CaseInsensitive) < 0;
	}

	case Col::Kind:
		// Preserve old display-string ordering: 'Audio' < 'Video'.
		if (l.kind == r.kind)
			return false;
		return l.kind == MediaFile::Kind::Audio;

	case Col::Duration:
	{
		// Sort by the same timecode arithmetic the column displays — frames
		// over the nominal base — so order and display can never disagree.
		// (Drop-frame changes rendering only, not the count, so it can't
		// affect ordering.) Ties break audio-first for stable grouping.
		auto tcSeconds = [](const MediaFile &f) -> double
		{
			if (f.durationFrames <= 0)
				return 0.0;
			const int base = f.effectiveTimecodeBase();
			return base > 0 ? double(f.durationFrames) / base : 0.0;
		};
		const double ls = tcSeconds(l);
		const double rs = tcSeconds(r);
		if (ls != rs)
			return ls < rs;
		return l.kind == MediaFile::Kind::Audio && r.kind == MediaFile::Kind::Video;
	}

	case Col::FileName:
		return QString::compare(l.fileName, r.fileName, Qt::CaseInsensitive) < 0;
	case Col::Project:
		return QString::compare(l.project, r.project, Qt::CaseInsensitive) < 0;
	case Col::OriginalBin:
		return QString::compare(l.originalBin, r.originalBin, Qt::CaseInsensitive) < 0;
	case Col::Resolution:
	{
		// Width first, then height; a string tiebreak keeps equal-dimension or
		// non-video rows in a stable, deterministic order.
		const QSize ls = resolutionSortValue(l.resolution);
		const QSize rs = resolutionSortValue(r.resolution);
		if (ls.width() != rs.width())
			return ls.width() < rs.width();
		if (ls.height() != rs.height())
			return ls.height() < rs.height();
		return QString::compare(l.resolution, r.resolution, Qt::CaseInsensitive) < 0;
	}
	case Col::Fps:
	{
		const double lf = fpsSortValue(l.fps);
		const double rf = fpsSortValue(r.fps);
		if (lf != rf)
			return lf < rf;
		return QString::compare(l.fps, r.fps, Qt::CaseInsensitive) < 0;
	}
	case Col::Volume:
		return QString::compare(l.volumeDisplay, r.volumeDisplay, Qt::CaseInsensitive) < 0;
	case Col::Type:
		// Preserve old display-string ordering: 'Media' < 'Precompute'.
		if (l.type == r.type)
			return false;
		return l.type == MediaFile::Type::Media;
	case Col::Count_:
		break;
	}
	return false;
}