#pragma once

#include "media/media_file.h"

#include <QAbstractTableModel>
#include <QSet>
#include <QString>
#include <QVector>

/// One row per MediaFile. Optional columns are appended and the view positions
/// precompute details after Bit Depth.
class MediaTableModel : public QAbstractTableModel
{
	Q_OBJECT
public:
	// MARK: - Columns

	enum class Column : int
	{
		ClipName,
		OriginalBin,
		Project,
		Duration,
		Kind,
		Type,
		Compression,
		Resolution,
		FrameRate,
		SampleRate,
		BitDepth,
		SizeMB,
		Created,
		Location,
		FileMobId,
		MasterMobId,
		KelpieId,
		NoDatabase,
		NoReference,
		PrecomputeCategory,
		EffectCategory,
		Effect,
		Count_
	};

	explicit MediaTableModel(QObject *parent = nullptr);

	// Logical positions before feature gating. Excluded columns have no index.
	int columnIndex(Column column) const;
	Column columnAt(int index) const;

	/// Typed values for Qt's sort comparator. DisplayRole supplies the cell text.
	static constexpr int SortRole = Qt::UserRole + 1;

	// MARK: - QAbstractItemModel overrides

	int rowCount(const QModelIndex &parent = {}) const override;
	int columnCount(const QModelIndex &parent = {}) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

	// MARK: - Bulk updates

	/// Rows arrive with table values prepared by the scanner adapter.
	void setMediaFiles(const QVector<MediaFile> &files);
	/// Confirmed transfers only: moves retain identity; copies allocate a new one.
	void applyTransfer(const QString &source, const QString &destination, bool copy);
	void setOmfScanEnabled(bool enabled);
	bool omfScanEnabled() const { return m_omfScanEnabled; }
	int omfScanColumn() const;

	/// Groups contiguous deletions into single beginRemoveRows /
	/// endRemoveRows ranges, so the view is preserved.
	void removeFilesByPath(const QSet<QString> &paths);

	// MARK: - Row access

	const MediaFile &fileAt(int row) const;
	const QVector<MediaFile> &allFiles() const { return m_files; }

	// MARK: - Precompute columns

	/// Appended precompute columns; changing the gate preserves rows and the
	/// existing ordinary columns. An enabled OmfScan column follows the details.
	void setPrecomputesEnabled(bool enabled);
	bool precomputesEnabled() const { return m_precomputesEnabled; }

private:
	// MARK: - Model state

	QVector<MediaFile> m_files;
	bool m_precomputesEnabled = false;
	bool m_omfScanEnabled = false;
	KelpieIdAllocator m_ids;
};
