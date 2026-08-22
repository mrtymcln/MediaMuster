#pragma once

#include "mediafile.h"

#include <QAbstractTableModel>
#include <QSet>
#include <QString>
#include <QVector>

/// One row per MediaFile. Columns ordered identity > context >
/// technical, matching the visual order in the UI.
class MediaTableModel : public QAbstractTableModel
{
	Q_OBJECT
public:
	// MARK: - Columns

	enum class Column : int
	{
		ClipName,
		FileName,
		Project,
		OriginalBin,
		Kind,
		Codec,
		Resolution,
		Fps,
		Duration,
		SizeMB,
		Location,
		Created,
		Modified,
		Type,
		SourceFile,
		Count_
	};

	explicit MediaTableModel(QObject *parent = nullptr);

	// MARK: - QAbstractItemModel overrides

	int rowCount(const QModelIndex &parent = {}) const override;
	int columnCount(const QModelIndex &parent = {}) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

	// MARK: - Bulk updates

	void setMediaFiles(const QVector<MediaFile> &files);

	/// Groups contiguous deletions into single beginRemoveRows /
	/// endRemoveRows ranges, so the view is preserved.
	void removeFilesByPath(const QSet<QString> &paths);

	const MediaFile &fileAt(int row) const;
	const QVector<MediaFile> &allFiles() const { return m_files; }

	/// Debug toggle: Codec column shows the raw 16-byte
	/// essence-container UL hex instead of the resolved codec name.
	void setShowRawCodecHex(bool on);
	bool showRawCodecHex() const { return m_showRawCodecHex; }

private:
	QVector<MediaFile> m_files;
	bool m_showRawCodecHex = false;
};