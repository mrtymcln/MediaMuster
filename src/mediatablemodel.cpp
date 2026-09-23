#include "mediatablemodel.h"
#include "enumutil.h"

#include <utility>

MediaTableModel::MediaTableModel(QObject *parent)
	: QAbstractTableModel(parent)
{
}

int MediaTableModel::rowCount(const QModelIndex &) const
{
	return m_files.size();
}
int MediaTableModel::columnCount(const QModelIndex &) const
{
	return Enum::to_underlying(m_precomputesEnabled ? Column::Count_ : Column::PrecomputeCategory);
}

void MediaTableModel::setMediaFiles(const QVector<MediaFile> &files)
{
	beginResetModel();
	m_files = files;
	applyAvbMetadata(false);
	endResetModel();
}

void MediaTableModel::setAvbBins(const QVector<AvbBin> &bins)
{
	m_binMetadata.setBins(bins);
	applyAvbMetadata(true);
}

void MediaTableModel::applyAvbMetadata(bool notify)
{
	int firstChanged = -1;
	int lastChanged = -1;
	const int rows = rowCount();
	for (int row = 0; row < rows; ++row)
	{
		MediaFile &file = m_files[row];
		if (m_binMetadata.apply(file))
		{
			if (firstChanged < 0)
				firstChanged = row;
			lastChanged = row;
		}
	}
	if (notify && firstChanged >= 0)
		emit dataChanged(index(firstChanged, static_cast<int>(Column::ClipName)),
						 index(lastChanged, static_cast<int>(Column::OriginalBin)),
						 {Qt::DisplayRole, Qt::UserRole});
}

void MediaTableModel::removeFilesByPath(const QSet<QString> &paths)
{
	if (paths.isEmpty() || m_files.isEmpty())
		return;

	// Back-to-front so lower index rows stay valid through each erase.
	// Groups contiguous removals into begin/endRemoveRows ranges.
	int rangeEnd = -1; // -1 means "no range in progress"
	for (int i = m_files.size() - 1; i >= 0; --i)
	{
		const bool removeThis = paths.contains(m_files[i].filePath);
		if (removeThis && rangeEnd == -1)
		{
			rangeEnd = i;
		}
		else if (!removeThis && rangeEnd != -1)
		{
			beginRemoveRows({}, i + 1, rangeEnd);
			m_files.erase(m_files.begin() + i + 1, m_files.begin() + rangeEnd + 1);
			endRemoveRows();
			rangeEnd = -1;
		}
	}
	if (rangeEnd != -1)
	{
		beginRemoveRows({}, 0, rangeEnd);
		m_files.erase(m_files.begin(), m_files.begin() + rangeEnd + 1);
		endRemoveRows();
	}
}

const MediaFile &MediaTableModel::fileAt(int row) const
{
	// Every proxy-mapped lookup funnels through here, so this is the one
	// place to catch a bogus row (e.g. a -1 from mapToSource during a
	// filter shuffle). Assert loudly in debug; in release hand back a
	// safe empty record rather than reading off the end of the vector.
	Q_ASSERT(row >= 0 && row < m_files.size());
	if (row < 0 || row >= m_files.size())
	{
		static const MediaFile empty;
		return empty;
	}
	return m_files[row];
}

void MediaTableModel::setShowCodecHex(bool on)
{
	if (m_showCodecHex == on)
		return;
	m_showCodecHex = on;
	if (!m_files.isEmpty())
	{
		const int codecCol = Enum::to_underlying(Column::Codec);
		emit dataChanged(index(0, codecCol), index(m_files.size() - 1, codecCol),
						 {Qt::DisplayRole});
	}
}

void MediaTableModel::setPrecomputesEnabled(bool enabled)
{
	if (m_precomputesEnabled == enabled)
		return;
	const int first = Enum::to_underlying(Column::PrecomputeCategory);
	const int last = Enum::to_underlying(Column::Count_) - 1;
	if (enabled)
	{
		beginInsertColumns({}, first, last);
		m_precomputesEnabled = true;
		endInsertColumns();
	}
	else
	{
		beginRemoveColumns({}, first, last);
		m_precomputesEnabled = false;
		endRemoveColumns();
	}
}

QVariant MediaTableModel::data(const QModelIndex &index, int role) const
{
	if (!index.isValid() || index.row() >= m_files.size() || index.column() >= columnCount())
		return {};
	const MediaFile &f = m_files[index.row()];

	if (role == Qt::DisplayRole)
	{
		switch (static_cast<Column>(index.column()))
		{
		case Column::ClipName:
			return f.clipNameDisplay();
		case Column::Project:
			return f.projectDisplay();
		case Column::OriginalBin:
			return f.originalBin;
		case Column::Kind:
			return f.kindDisplay();
		case Column::Duration:
			return f.durationDisplay();
		case Column::SizeMB:
			return f.sizeMBDisplay();
		case Column::Codec:
			return f.codecDisplay(m_showCodecHex);
		case Column::Resolution:
			return f.resolution;
		case Column::Fps:
			return f.fps;
		case Column::SampleRate:
			return f.sampleRateDisplay();
		case Column::BitDepth:
			return f.bitDepth;
		case Column::Type:
			return f.typeDisplay();
		case Column::FileName:
			return f.fileName;
		case Column::SourceFile:
			return f.sourceFileName;
		case Column::Created:
			return f.createdDisplay();
		case Column::Location:
			return f.filePath;
		case Column::PrecomputeCategory:
			return f.precomputeCategoryDisplay();
		case Column::EffectCategory:
			return f.effectCategoryDisplay();
		case Column::Effect:
			return f.effectDisplay();
		case Column::EffectSequence:
			return f.type == MediaFile::Type::Precompute ? f.effectSequence : QString();
		case Column::Count_:
			break;
		}
	}
	if (role == Qt::ToolTipRole && static_cast<Column>(index.column()) == Column::Project)
	{
		// Explain the row where the user is looking: why there is no project
		// name, and/or why the folder's databases couldn't vouch for the file.
		// The sentences live on MediaFile so the tabs and CSV say the same.
		QStringList lines;
		if (f.hasNoProject())
			lines << MediaFile::noProjectWhy();
		if (f.dbStatus != MediaFile::DbStatus::Listed)
			lines << f.dbStatusText().label + QStringLiteral(": ") + f.dbStatusText().why;
		if (!lines.isEmpty())
			return lines.join(QStringLiteral("\n\n"));
	}
	if (role == Qt::ToolTipRole && static_cast<Column>(index.column()) == Column::SourceFile)
		return f.sourceFilePath; // the full path Avid recorded; the cell shows the name
	if (role == Qt::ToolTipRole && static_cast<Column>(index.column()) == Column::Kind &&
		f.kind == MediaFile::Kind::Unknown)
		return QStringLiteral("The available metadata has not identified this file as audio or video.");
	if (role == Qt::ToolTipRole && static_cast<Column>(index.column()) == Column::Type &&
		f.type == MediaFile::Type::Unknown)
		return QStringLiteral("The available metadata has not identified this file as media or a precompute.");
	if (role == Qt::TextAlignmentRole && static_cast<Column>(index.column()) == Column::SizeMB)
		return QVariant(int(Qt::AlignRight | Qt::AlignVCenter));
	if (role == Qt::UserRole)
	{
		if (static_cast<Column>(index.column()) == Column::SizeMB)
			return f.sizeBytes;
		if (static_cast<Column>(index.column()) == Column::SampleRate)
			return f.sampleRate > 0 ? QVariant(f.sampleRate) : QVariant();
		if (static_cast<Column>(index.column()) == Column::Created)
			return f.created;
		return data(index, Qt::DisplayRole);
	}
	return {};
}

QVariant MediaTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 || section >= columnCount())
		return {};

	const char *headers[] = {"Clip Name", "Project", "Bin", "Kind", "Duration", "Size (MB)",
							 "Codec", "Resolution", "FPS", "Sample Rate", "Bit Depth", "Type",
							 "Filename", "Source File", "Date Created", "Location",
							 "Precompute Category", "Effect Category", "Effect", "Effect Sequence"};
	static_assert(sizeof(headers) / sizeof(headers[0]) == Enum::to_underlying(Column::Count_),
				  "Column enum and headers[] array got out of sync — "
				  "add or remove a header string when changing the Column enum");
	if (section >= 0 && section < Enum::to_underlying(Column::Count_))
		return QString(headers[section]);
	return {};
}
