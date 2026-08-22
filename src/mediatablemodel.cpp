#include "mediatablemodel.h"
#include "enumutil.h"

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
	return Enum::to_underlying(Column::Count_);
}

void MediaTableModel::setMediaFiles(const QVector<MediaFile> &files)
{
	beginResetModel();
	m_files = files;
	endResetModel();
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

QVariant MediaTableModel::data(const QModelIndex &index, int role) const
{
	if (!index.isValid() || index.row() >= m_files.size())
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
			return f.project;
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
		case Column::Count_:
			break;
		}
	}
	if (role == Qt::ToolTipRole && static_cast<Column>(index.column()) == Column::Project)
	{
		// Explain the sentinel states in place, where the user is looking.
		if (f.dbIssue == MediaFile::DbIssue::Unreadable)
			return QStringLiteral("This folder's Avid database exists but is unreadable "
								  "(possibly corrupt), so this file's references could not "
								  "be checked.");
		if (f.dbIssue == MediaFile::DbIssue::NeverIndexed)
			return QStringLiteral("This folder has no Avid databases (msmFMID.pmr / "
								  "msmMMOB.mdb) — Avid has never indexed it.");
		if (f.isNoReference)
			return QStringLiteral("No reference in the folder's MDB or PMR databases. Expected "
								  "in Interplay environments — the media may still be in use.");
	}
	if (role == Qt::ToolTipRole && static_cast<Column>(index.column()) == Column::SourceFile)
		return f.sourceFilePath; // the full path Avid recorded; the cell shows the name
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
	if (orientation != Qt::Horizontal)
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
							 "Type", "Source File"};
	static_assert(sizeof(headers) / sizeof(headers[0]) == Enum::to_underlying(Column::Count_),
				  "Column enum and headers[] array got out of sync — "
				  "add or remove a header string when changing the Column enum");
	if (section >= 0 && section < Enum::to_underlying(Column::Count_))
		return QString(headers[section]);
	return {};
}