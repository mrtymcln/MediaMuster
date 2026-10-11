#pragma once

#include "mediafile.h"
#include "avbmetadataresolver.h"

#include <QAbstractTableModel>
#include <QSet>
#include <QString>
#include <QVector>

struct AvbBin;

/// One row per MediaFile. Optional columns are appended to keep logical indexes
/// stable; the view positions Clip Duration beside Duration and details beside Type.
class MediaTableModel : public QAbstractTableModel
{
	Q_OBJECT
public:
	// MARK: - Columns

	enum class Column : int
	{
		ClipName,
		Project,
		OriginalBin,
		Kind,
		Duration,
		SizeMB,
		Compression,
		Resolution,
		FrameRate,
		SampleRate,
		BitDepth,
		Type,
		Created,
		FileName,
		SourceFile,
		Location,
		MobId,
		MasterMobId,
		KelpieId,
		PrecomputeCategory,
		EffectCategory,
		Effect,
		EffectSequence,
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
	void setScanIssues(const QVector<ScanIssue> &issues) { m_scanIssues = issues; }
	const QVector<ScanIssue> &scanIssues() const { return m_scanIssues; }
	/// Confirmed transfers only: moves retain identity; copies allocate a new one.
	void applyTransfer(const QString &source, const QString &destination, bool copy);
	void setOmfScanEnabled(bool enabled);
	bool omfScanEnabled() const { return m_omfScanEnabled; }
	int omfScanColumn() const;

	/// Attach or retract matching master-clip observations from loaded bins.
	/// MediaEngine rows refresh under the shared policy; unresolved ties stay blank.
	/// Removing a bin retains its evidence history while excluding its values.
	void setAvbBins(const QVector<AvbBin> &bins);

	/// Groups contiguous deletions into single beginRemoveRows /
	/// endRemoveRows ranges, so the view is preserved.
	void removeFilesByPath(const QSet<QString> &paths);

	const MediaFile &fileAt(int row) const;
	const QVector<MediaFile> &allFiles() const { return m_files; }

	/// Appended experimental columns; changing the gate preserves rows,
	/// existing column numbers and persistent indexes in those columns.
	void setPrecomputesEnabled(bool enabled);
	bool precomputesEnabled() const { return m_precomputesEnabled; }

	/// Logically appended after active columns; the view places it beside Duration.
	/// Its logical index moves when precompute details toggle.
	void setClipDurationEnabled(bool enabled);
	int clipDurationColumn() const;
	bool clipDurationEnabled() const { return m_clipDurationEnabled; }

private:
	void applyAvbMetadata(bool notify);
	AvbMetadataResolver m_binMetadata;
	QVector<MediaFile> m_files;
	bool m_precomputesEnabled = false;
	bool m_clipDurationEnabled = false;
	bool m_omfScanEnabled = false;
	KelpieIdAllocator m_ids;
	QVector<ScanIssue> m_scanIssues;
};
