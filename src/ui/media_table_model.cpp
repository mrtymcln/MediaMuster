#include "media_table_model.h"
#include "support/enum_util.h"

#include <QStringList>
#include <QFileInfo>
#include <QStorageInfo>
#include "storage/volume_identity.h"

// MARK: - Construction and columns

MediaTableModel::MediaTableModel(QObject *parent)
	: QAbstractTableModel(parent)
{
}

int MediaTableModel::columnIndex(Column column) const
{
	return Enum::toUnderlying(column);
}

MediaTableModel::Column MediaTableModel::columnAt(int index) const
{
	if (index < 0 || index >= omfScanColumn())
		return Column::Count_;
	return static_cast<Column>(index);
}

int MediaTableModel::rowCount(const QModelIndex &parent) const
{
	return parent.isValid() ? 0 : m_files.size();
}
int MediaTableModel::columnCount(const QModelIndex &parent) const
{
	if (parent.isValid())
		return 0;
	return omfScanColumn() + (m_omfScanEnabled ? 1 : 0);
}

int MediaTableModel::omfScanColumn() const
{
	return columnIndex(m_precomputesEnabled ? Column::Count_ : Column::PrecomputeCategory);
}

void MediaTableModel::setOmfScanEnabled(bool enabled)
{
	if (m_omfScanEnabled == enabled)
		return;
	const int column = omfScanColumn();
	if (enabled)
		beginInsertColumns({}, column, column);
	else
		beginRemoveColumns({}, column, column);
	m_omfScanEnabled = enabled;
	if (enabled)
		endInsertColumns();
	else
		endRemoveColumns();
}

// MARK: - Media row updates

void MediaTableModel::setMediaFiles(const QVector<MediaFileRecord> &files)
{
	beginResetModel();
	m_files = files;
	m_ids.reset();
	for (const auto &file : m_files)
		m_ids.reserveThrough(file.kelpieId);
	QSet<KelpieId> assigned;
	for (auto &file : m_files)
	{
		if (file.kelpieId == 0 || assigned.contains(file.kelpieId))
			file.kelpieId = m_ids.allocate();
		assigned.insert(file.kelpieId);
	}
	endResetModel();
}

void MediaTableModel::applyTransfer(const QString &source, const QString &destination, bool copy)
{
	if (source == destination || destination.isEmpty())
		return;
	const QFileInfo info(destination);
	if (!info.isFile())
		return;
	for (const auto &file : m_files)
		if (file.mediaFilePath == info.absoluteFilePath())
			return; // Repeated completion notification must not invent another location.
	for (int row = 0; row < m_files.size(); ++row)
	{
		if (m_files[row].mediaFilePath != source)
			continue;
		MediaFileRecord transferred = m_files[row];
		transferred.mediaFilePath = info.absoluteFilePath();
		transferred.fileName = info.fileName();
		transferred.mediaFolderName = info.dir().dirName();
		transferred.sizeBytes = info.size();
		transferred.created = info.birthTime();
		transferred.modified = info.lastModified();
		const QStorageInfo storage(destination);
		transferred.volumePath = storage.rootPath();
		transferred.volumeName = storage.name();
		transferred.scanStamp.path = transferred.mediaFilePath;
		transferred.scanStamp.modified = transferred.modified;
		transferred.scanStamp.volumeIdentifier = VolumeIdentity::capture(destination).identifier();
		transferred.isQuarantined = info.dir().dirName().compare(QStringLiteral("Quarantined Files"), Qt::CaseInsensitive) == 0;
		// Destination databases have not been parsed by this transfer. Their
		// presence cannot establish that this new location is absent from them.
		const auto databases = info.dir().entryList({QStringLiteral("*.pmr"), QStringLiteral("*.mdb")}, QDir::Files | QDir::NoSymLinks);
		transferred.dbStatus = databases.isEmpty() ? MediaFileRecord::DbStatus::NoDatabase : MediaFileRecord::DbStatus::DbUnreadable;
		if (copy)
		{
			transferred.kelpieId = m_ids.allocate();
			const int next = m_files.size();
			beginInsertRows({}, next, next);
			m_files.append(std::move(transferred));
			endInsertRows();
		}
		else
		{
			m_files[row] = std::move(transferred);
			emit dataChanged(index(row, 0), index(row, columnCount() - 1));
		}
		return;
	}
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
		const bool removeThis = paths.contains(m_files[i].mediaFilePath);
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

// MARK: - Row access

const MediaFileRecord &MediaTableModel::fileAt(int row) const
{
	// Filtering can invalidate mapped rows. Assert in debug and return an empty record in release.
	Q_ASSERT(row >= 0 && row < m_files.size());
	if (row < 0 || row >= m_files.size())
	{
		static const MediaFileRecord empty;
		return empty;
	}
	return m_files[row];
}

// MARK: - Precompute columns

void MediaTableModel::setPrecomputesEnabled(bool enabled)
{
	if (m_precomputesEnabled == enabled)
		return;
	const int first = columnIndex(Column::PrecomputeCategory);
	const int last = columnIndex(Column::Count_) - 1;
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

// MARK: - Cell and header values

namespace
{
	int kindSortRank(MediaFileRecord::Kind kind)
	{
		switch (kind)
		{
		case MediaFileRecord::Kind::Video:
			return 0;
		case MediaFileRecord::Kind::Audio:
			return 1;
		case MediaFileRecord::Kind::Unknown:
			return 2;
		}
		return 2;
	}

	int typeSortRank(MediaFileRecord::Type type)
	{
		switch (type)
		{
		case MediaFileRecord::Type::Media:
			return 0;
		case MediaFileRecord::Type::Precompute:
			return 1;
		case MediaFileRecord::Type::Unknown:
			return 2;
		}
		return 2;
	}
}

QVariant MediaTableModel::data(const QModelIndex &index, int role) const
{
	if (!index.isValid() || index.row() >= m_files.size() || index.column() >= columnCount())
		return {};
	const MediaFileRecord &f = m_files[index.row()];
	if (m_omfScanEnabled && index.column() == omfScanColumn())
	{
		if (role == SortRole)
			return int(f.omfEra);
		if (role == Qt::DisplayRole)
			return f.omfEra ? QStringLiteral("true") : QStringLiteral("false");
		return {};
	}

	const auto column = columnAt(index.column());
	if (role == SortRole)
	{
		switch (column)
		{
		case Column::Kind:
			return kindSortRank(f.kind);
		case Column::Type:
			return typeSortRank(f.type);
		case Column::SizeMB:
			return f.sizeBytes;
		case Column::BitDepth:
		{
			// Missing bit depth is first, before a known zero.
			const auto depth = f.bellaDepthBits();
			return depth ? qint64(*depth) : qint64{-1};
		}
		case Column::Created:
			return f.created;
		case Column::NoDatabase:
			return int(f.isNoDatabase());
		case Column::NoReference:
			return int(f.isNoReference());
		default:
			break;
		}
	}
	if (role == Qt::DisplayRole || role == SortRole)
	{
		switch (column)
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
		case Column::Compression:
			return f.compression;
		case Column::Resolution:
			return f.resolutionDisplay();
		case Column::FrameRate:
			return f.frameRateDisplay();
		case Column::SampleRate:
			return f.sampleRateDisplay();
		case Column::BitDepth:
			return f.bitDepthDisplay();
		case Column::Type:
			return f.typeDisplay();
		case Column::Created:
			return f.createdDisplay();
		case Column::Location:
			return f.mediaFilePath;
		case Column::NoDatabase:
			return f.isNoDatabase() ? QStringLiteral("true") : QStringLiteral("false");
		case Column::NoReference:
			return f.isNoReference() ? QStringLiteral("true") : QStringLiteral("false");
		case Column::FileMobId:
			return f.fileMobId;
		case Column::MasterMobId:
			return f.masterMobId;
		case Column::KelpieId:
			return QVariant::fromValue(f.kelpieId);
		case Column::PrecomputeCategory:
			return f.precomputeCategoryDisplay();
		case Column::EffectCategory:
			return f.effectCategoryDisplay();
		case Column::Effect:
			return f.effectDisplay();
		case Column::Count_:
			break;
		}
	}
	if (role == Qt::ToolTipRole && (column == Column::NoDatabase || column == Column::NoReference))
	{
		QString explanation = f.dbStatusText().why;
		if (f.dbStatus == MediaFileRecord::DbStatus::Listed)
			explanation = QStringLiteral("A validated local PMR or MDB entry references this physical file. "
										 "This does not establish database freshness or sequence usage.");
		if (column == Column::NoReference && f.isNoDatabase())
			explanation += QStringLiteral(" No Reference is false because absence was not established; "
										  "this does not confirm that the file is listed.");
		return explanation;
	}
	if (role == Qt::ToolTipRole && column == Column::Project)
	{
		// Share missing-project and database explanations with the filter tabs.
		QStringList lines;
		if (f.hasNoProject())
			lines << MediaFileRecord::noProjectWhy();
		if (f.dbStatus != MediaFileRecord::DbStatus::Listed)
			lines << f.dbStatusText().label + QStringLiteral(": ") + f.dbStatusText().why;
		if (!lines.isEmpty())
			return lines.join(QStringLiteral("\n\n"));
	}
	if (role == Qt::ToolTipRole && column == Column::Kind &&
		f.kind == MediaFileRecord::Kind::Unknown)
		return QStringLiteral("The available metadata has not identified this file as audio or video.");
	if (role == Qt::ToolTipRole && column == Column::Type &&
		f.type == MediaFileRecord::Type::Unknown)
		return QStringLiteral("The available metadata has not identified this file as media or a precompute.");
	if (role == Qt::TextAlignmentRole && column == Column::SizeMB)
		return QVariant(int(Qt::AlignRight | Qt::AlignVCenter));
	return {};
}

QVariant MediaTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 || section >= columnCount())
		return {};

	if (m_omfScanEnabled && section == omfScanColumn())
		return QStringLiteral("OmfScan");
	const auto column = columnAt(section);
	const char *headers[] = {"Clip Name", "Bin", "Project", "Duration", "Kind", "Type",
							 "Compression", "Resolution", "Frame Rate", "Sample Rate", "Bit Depth",
							 "Size (MB)", "Date Created", "File Path", "FileMobId", "MasterMobId", "KelpieId", "No Database", "No Reference",
							 "Precompute Category", "Effect Category", "Effect"};
	static_assert(sizeof(headers) / sizeof(headers[0]) == Enum::toUnderlying(Column::Count_),
				  "Column enum and headers[] array got out of sync — "
				  "add or remove a header string when changing the Column enum");
	if (column != Column::Count_)
		return QString(headers[Enum::toUnderlying(column)]);
	return {};
}
