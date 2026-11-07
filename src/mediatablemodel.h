#pragma once

#include "mediafile.h"

#include <QAbstractTableModel>
#include <QSet>
#include <QString>
#include <QVector>

/// One row per MediaFile. Presentation selects the column schema; optional
/// columns are appended and the view positions precompute details beside Type.
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
		SourcePath,
		Location,
		NoDatabase,
		NoReference,
		FileMobId,
		MasterMobId,
		KelpieId,
		PrecomputeCategory,
		EffectCategory,
		Effect,
		EffectSequence,
		Count_
	};

	explicit MediaTableModel(QObject *parent = nullptr,
							 MediaPresentation presentation = MediaPresentation::OldScanner);

	MediaPresentation presentation() const { return m_presentation; }
	// Logical positions before feature gating. Excluded columns have no index.
	int columnIndex(Column column) const;
	Column columnAt(int index) const;

	// MARK: - QAbstractItemModel overrides

	int rowCount(const QModelIndex &parent = {}) const override;
	int columnCount(const QModelIndex &parent = {}) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

	// MARK: - Bulk updates

	/// Rows arrive with table values prepared by the scanner adapter.
	void setMediaFiles(const QVector<MediaFile> &files);
	void setScanIssues(const QVector<ScanIssue> &issues) { m_scanIssues = issues; }
	const QVector<ScanIssue> &scanIssues() const { return m_scanIssues; }
	/// Confirmed transfers only: moves retain identity; copies allocate a new one.
	void applyTransfer(const QString &source, const QString &destination, bool copy);
	void setOmfScanEnabled(bool enabled);
	bool omfScanEnabled() const { return m_omfScanEnabled; }
	int omfScanColumn() const;

	/// Groups contiguous deletions into single beginRemoveRows /
	/// endRemoveRows ranges, so the view is preserved.
	void removeFilesByPath(const QSet<QString> &paths);

	const MediaFile &fileAt(int row) const;
	const QVector<MediaFile> &allFiles() const { return m_files; }

	/// Appended precompute columns; changing the gate preserves rows and the
	/// existing ordinary columns. An enabled OmfScan column follows the details.
	void setPrecomputesEnabled(bool enabled);
	bool precomputesEnabled() const { return m_precomputesEnabled; }

private:
	MediaPresentation m_presentation;
	QVector<MediaFile> m_files;
	bool m_precomputesEnabled = false;
	bool m_omfScanEnabled = false;
	KelpieIdAllocator m_ids;
	QVector<ScanIssue> m_scanIssues;
};
