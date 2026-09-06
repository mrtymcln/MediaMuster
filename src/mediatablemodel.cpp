#include "mediatablemodel.h"
#include "avbparser.h"
#include "enumutil.h"
#include "mobid.h"

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
	return Enum::to_underlying(m_effectDetailsEnabled ? Column::Count_ : Column::PrecomputeCategory);
}

void MediaTableModel::setMediaFiles(const QVector<MediaFile> &files)
{
	beginResetModel();
	m_files = files;
	applyAvbMetadata(false);
	endResetModel();
}

void MediaTableModel::AvbMetadata::merge(const AvbMob &mob)
{
	if (!mob.name.isEmpty())
	{
		if (!clipName.isEmpty() && clipName != mob.name)
			nameConflict = true;
		else
			clipName = mob.name;
	}
	if (!mob.originalBinUid.isEmpty())
	{
		if (!originalBinUid.isEmpty() && originalBinUid != mob.originalBinUid)
			binConflict = true;
		else
			originalBinUid = mob.originalBinUid;
	}
	if (!mob.originalBin.isEmpty())
	{
		if (!originalBin.isEmpty() && originalBin != mob.originalBin)
			binConflict = true;
		else
			originalBin = mob.originalBin;
	}
}

void MediaTableModel::setAvbBins(const QVector<AvbBin> &bins)
{
	QHash<QString, AvbMetadata> metadata;
	for (const AvbBin &bin : bins)
	{
		if (!bin.valid || !bin.complete)
			continue;
		for (const AvbMob &mob : bin.mobs)
		{
			// SourceMob names describe imported files/tapes. Only a MasterMob
			// can supply the editor's clip name and that clip's original bin.
			if (mob.mobType != AvbMob::masterMobType || mob.mobId.isEmpty())
				continue;
			const QSet<QString> keys{mob.mobId, MobId::toPmrForm(mob.mobId)};
			for (const QString &key : keys)
			{
				if (key.isEmpty())
					continue;
				metadata[key].merge(mob);
			}
		}
	}
	m_avbMetadata = std::move(metadata);
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
		const QString previousName = file.clipName;
		const QString previousBin = file.originalBin;
		if (file.clipNameSource == MediaFile::ClipNameSource::Avb)
		{
			file.clipName.clear();
			file.clipNameSource = MediaFile::ClipNameSource::None;
		}
		if (file.originalBinFromAvb)
		{
			file.originalBin.clear();
			file.originalBinFromAvb = false;
		}
		const auto found = m_avbMetadata.constFind(file.masterMobId);
		if (found != m_avbMetadata.cend())
		{
			const AvbMetadata &value = found.value();
			if (file.clipName.isEmpty() && !value.nameConflict && !value.clipName.isEmpty())
			{
				file.clipName = value.clipName;
				file.clipNameSource = MediaFile::ClipNameSource::Avb;
			}
			if (file.originalBin.isEmpty() && !value.binConflict && !value.originalBin.isEmpty())
			{
				file.originalBin = value.originalBin;
				file.originalBinFromAvb = true;
			}
		}
		if (file.clipName != previousName || file.originalBin != previousBin)
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

void MediaTableModel::setShowRawCodecHex(bool on)
{
	if (m_showRawCodecHex == on)
		return;
	m_showRawCodecHex = on;
	if (!m_files.isEmpty())
	{
		const int codecCol = Enum::to_underlying(Column::Codec);
		emit dataChanged(index(0, codecCol), index(m_files.size() - 1, codecCol),
						 {Qt::DisplayRole});
	}
}

void MediaTableModel::setEffectDetailsEnabled(bool enabled)
{
	if (m_effectDetailsEnabled == enabled)
		return;
	const int first = Enum::to_underlying(Column::PrecomputeCategory);
	const int last = Enum::to_underlying(Column::Count_) - 1;
	if (enabled)
	{
		beginInsertColumns({}, first, last);
		m_effectDetailsEnabled = true;
		endInsertColumns();
	}
	else
	{
		beginRemoveColumns({}, first, last);
		m_effectDetailsEnabled = false;
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
		case Column::FileName:
			return f.fileName;
		case Column::Project:
			return f.projectDisplay();
		case Column::OriginalBin:
			return f.originalBin;
		case Column::Kind:
			return f.kindDisplay();
		case Column::Codec:
			return f.codecDisplay(m_showRawCodecHex);
		case Column::Resolution:
			return f.resolution;
		case Column::Fps:
			return f.fps;
		case Column::Duration:
			return f.durationDisplay();
		case Column::SizeMB:
			return f.sizeMBDisplay();
		case Column::Location:
			return f.filePath;
		case Column::Created:
			return f.createdDisplay();
		case Column::Modified:
			return f.modifiedDisplay();
		case Column::Type:
			return f.typeDisplay();
		case Column::SourceFile:
			return f.sourceFileName;
		case Column::PrecomputeCategory:
			return f.precomputeCategoryDisplay();
		case Column::Effect:
			return f.effectDisplay();
		case Column::EffectCategory:
			return f.effectCategoryDisplay();
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
		if (static_cast<Column>(index.column()) == Column::Created)
			return f.created;
		if (static_cast<Column>(index.column()) == Column::Modified)
			return f.modified;
		return data(index, Qt::DisplayRole);
	}
	return {};
}

QVariant MediaTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal || section < 0 || section >= columnCount())
		return {};

	if (role == Qt::ToolTipRole)
	{
		if (section == Enum::to_underlying(Column::OriginalBin))
		{
			return QStringLiteral("The bin this clip was originally imported into.");
		}
		if (section == Enum::to_underlying(Column::Location))
		{
			return QStringLiteral("The filepath for this clip.");
		}
		if (section == Enum::to_underlying(Column::Created))
		{
			return QStringLiteral("Blank when the file system doesn't record a creation date.");
		}
		if (section == Enum::to_underlying(Column::Modified))
		{
			return QStringLiteral("The file's modification date, as the file system records it.");
		}
		if (section == Enum::to_underlying(Column::SourceFile))
		{
			return QStringLiteral("The file this clip was imported from, as Avid recorded it. "
								  "Blank when Avid recorded none — media it generated itself "
								  "(renders, tones, mixdowns) or a tape capture.");
		}
		return {};
	}

	if (role != Qt::DisplayRole)
		return {};

	// "Bin" matches the CSV export's heading for the same field; the header
	// tooltip above still explains it is the bin the clip was imported into.
	const char *headers[] = {"Clip Name", "Filename", "Project", "Bin", "Kind",
							 "Codec", "Resolution", "FPS", "Duration",
							 "Size (MB)", "Location", "Date Created", "Date Modified",
							 "Type", "Source File", "Precompute Category", "Effect Category", "Effect", "Effect Sequence"};
	static_assert(sizeof(headers) / sizeof(headers[0]) == Enum::to_underlying(Column::Count_),
				  "Column enum and headers[] array got out of sync — "
				  "add or remove a header string when changing the Column enum");
	if (section >= 0 && section < Enum::to_underlying(Column::Count_))
		return QString(headers[section]);
	return {};
}
