#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QStorageInfo;

/// One mounted volume the user might want to scan.
struct VolumeInfo
{
	QString name;
	QString path;
	qint64 totalBytes = 0;
	qint64 usedBytes = 0;
	QString volumeType; ///< "Local", "Network", or "Nexis".
	bool hasAvidMedia = false;
};

// MARK: - VolumeManager

/// Detects mounted volumes that might contain Avid media on request.
/// QStorageInfo alone misses some Nexis mounts, so
/// detection cross-references three sources (QStorageInfo, a direct
/// /Volumes walk on macOS, and known Avid install paths) and merges.
class VolumeManager : public QObject
{
	Q_OBJECT
public:
	explicit VolumeManager(QObject *parent = nullptr);

	// MARK: - Detection

	/// Synchronous detection; may block on slow mounts. Startup and Refresh
	/// Volumes call this on the UI thread.
	QVector<VolumeInfo> detectVolumes() const;

private:
	static bool hasAvidMediaFolder(const QString &path);
	static QStringList knownAvidLocations();
	static QString detectVolumeType(const QString &name, const QString &path,
									const QStorageInfo &storage);

public:
	/// Shared metadata mapping for detected volumes and manually added folders.
	/// Invalid storage information leaves byte counts at zero.
	static VolumeInfo makeVolumeInfo(const QString &name, const QString &path,
									 const QStorageInfo &storage);
};
