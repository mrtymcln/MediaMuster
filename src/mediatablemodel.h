#pragma once

#include "mediafile.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

struct AvbBin;
struct AvbMob;

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
	void setShowRawCodecHex(bool on);
	bool showRawCodecHex() const { return m_showRawCodecHex; }

	/// Appended experimental columns; changing the gate preserves rows,
	/// existing column numbers and persistent indexes in those columns.
	void setEffectDetailsEnabled(bool enabled);
	bool effectDetailsEnabled() const { return m_effectDetailsEnabled; }

private:
	struct AvbMetadata
	{
		void merge(const AvbMob &mob);

		QString clipName;
		QString originalBin;
		QString originalBinUid;
		bool nameConflict = false;
		bool binConflict = false;
	};
	void applyAvbMetadata(bool notify);
	QHash<QString, AvbMetadata> m_avbMetadata;
	QVector<MediaFile> m_files;
	bool m_showRawCodecHex = false;
	bool m_effectDetailsEnabled = false;
};
