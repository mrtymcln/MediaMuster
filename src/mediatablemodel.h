#pragma once

#include "mediafile.h"
#include "binmetadataresolver.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

struct AvbBin;

/// One row per MediaFile. Base columns follow the default table order;
/// optional detail columns are appended to keep existing indexes stable.
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
		Codec,
		Resolution,
		Fps,
		SampleRate,
		BitDepth,
		Type,
		FileName,
		SourceFile,
		Created,
		Location,
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

	/// Fill missing clip/original-bin names from matching master clips in
	/// successfully loaded bins. Conflicting values stay unknown. Removing
	/// a bin retracts only the fallback metadata; scanner values take precedence.
	void setAvbBins(const QVector<AvbBin> &bins);

	/// Groups contiguous deletions into single beginRemoveRows /
	/// endRemoveRows ranges, so the view is preserved.
	void removeFilesByPath(const QSet<QString> &paths);

	const MediaFile &fileAt(int row) const;
	const QVector<MediaFile> &allFiles() const { return m_files; }

	/// Debug toggle: Codec column shows the raw 16-byte
	/// essence-container UL hex instead of the resolved codec name.
	void setShowCodecHex(bool on);
	bool showCodecHex() const { return m_showCodecHex; }

	/// Appended experimental columns; changing the gate preserves rows,
	/// existing column numbers and persistent indexes in those columns.
	void setPrecomputesEnabled(bool enabled);
	bool precomputesEnabled() const { return m_precomputesEnabled; }

private:
	void applyAvbMetadata(bool notify);
	BinMetadataResolver m_binMetadata;
	QVector<MediaFile> m_files;
	bool m_showCodecHex = false;
	bool m_precomputesEnabled = false;
};
