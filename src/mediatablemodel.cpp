#include "mediatablemodel.h"
#include "enumutil.h"

#include <QStringList>
#include <QFileInfo>
#include <QStorageInfo>
#include "volumeidentity.h"

namespace
{
	MediaFieldStatus filesystemStatus(bool known, EvidenceBasis basis = EvidenceBasis::Recorded)
	{
		MediaFieldStatus status;
		status.captured = true;
		status.readState = known ? PropertyReadState::Present : PropertyReadState::NotRead;
		status.readReason = known ? PropertyReadReason::None : PropertyReadReason::CoverageNotEstablished;
		status.applicability = PropertyApplicability::Applicable;
		status.agreement = known ? PropertyAgreement::SingleSource : PropertyAgreement::NotCompared;
		status.hasSelectedValue = known;
		status.unknownFreshnessSources.add(MetadataSource::Filesystem);
		if (known)
		{
			status.selectedSource = MetadataSource::Filesystem;
			status.selectedBasis = basis;
			status.selectedFreshness = SourceFreshness::Unknown;
			status.presentSources.add(MetadataSource::Filesystem);
		}
		return status;
	}
}

MediaTableModel::MediaTableModel(QObject *parent)
	: QAbstractTableModel(parent)
{
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
	return Enum::to_underlying(m_precomputesEnabled ? Column::Count_ : Column::PrecomputeCategory);
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

void MediaTableModel::setMediaFiles(const QVector<MediaFile> &files)
{
	beginResetModel();
	m_files = files;
	m_scanIssues.clear();
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
		MediaFile transferred = m_files[row];
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
		// Only physical facts change. Metadata and the frozen scan safety record
		// still describe this media; no old observations need to be rebuilt.
		for (const auto property : {MediaProperty::Location, MediaProperty::Filename, MediaProperty::Size,
			MediaProperty::Modified})
			transferred.metadataStatus.field(property) = filesystemStatus(true);
		transferred.metadataStatus.field(MediaProperty::Created) = filesystemStatus(transferred.created.isValid());
		transferred.metadataStatus.field(MediaProperty::VolumeIdentifier) = filesystemStatus(!transferred.scanStamp.volumeIdentifier.isEmpty());

		// Destination databases have not been parsed by this transfer. Their
		// presence cannot establish that this new location is absent from them.
		const auto databases = info.dir().entryList({QStringLiteral("*.pmr"), QStringLiteral("*.mdb")}, QDir::Files | QDir::NoSymLinks);
		transferred.dbStatus = databases.isEmpty() ? MediaFile::DbStatus::NoDatabase : MediaFile::DbStatus::DbUnreadable;
		transferred.metadataStatus.field(MediaProperty::DatabaseStatus) = filesystemStatus(true, EvidenceBasis::Derived);
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
	if (m_omfScanEnabled && index.column() == omfScanColumn())
		return role == Qt::DisplayRole ? QVariant(f.omfEra ? QStringLiteral("true") : QStringLiteral("false")) : role == Qt::UserRole ? QVariant(f.omfEra)
																																	  : QVariant{};

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
		case Column::Compression:
			return f.compression;
		case Column::Resolution:
			return f.resolution;
		case Column::FrameRate:
			return f.frameRate;
		case Column::SampleRate:
			return f.sampleRateDisplay();
		case Column::BitDepth:
			return f.bitDepth;
		case Column::Type:
			return f.typeDisplay();
		case Column::Created:
			return f.createdDisplay();
		case Column::FileName:
			return f.fileName;
		case Column::SourceFile:
			return f.sourceFileName;
		case Column::Location:
			return f.mediaFilePath;
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
			return f.sampleRateHz() > 0 ? QVariant(f.sampleRateHz()) : QVariant();
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

	if (m_omfScanEnabled && section == omfScanColumn())
		return QStringLiteral("OmfScan");

	const char *headers[] = {"Clip Name", "Project", "Bin", "Kind", "Duration", "Size (MB)",
							 "Compression", "Resolution", "Frame Rate", "Sample Rate", "Bit Depth", "Type",
							 "Date Created", "Filename", "Source Filename", "Location", "FileMobId", "MasterMobId", "KelpieId",
							 "Precompute Category", "Effect Category", "Effect", "Effect Sequence"};
	static_assert(sizeof(headers) / sizeof(headers[0]) == Enum::to_underlying(Column::Count_),
				  "Column enum and headers[] array got out of sync — "
				  "add or remove a header string when changing the Column enum");
	if (section >= 0 && section < Enum::to_underlying(Column::Count_))
		return QString(headers[section]);
	return {};
}
