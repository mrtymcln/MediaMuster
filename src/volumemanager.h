#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QStorageInfo;

/// A mounted volume or manually added location in the volume list.
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

/// Detect mounted volumes and known Avid locations on request.
/// The macOS /Volumes fallback includes mounts missing from QStorageInfo.
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
