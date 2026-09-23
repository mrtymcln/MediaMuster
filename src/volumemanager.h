#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <QString>
#include <QVector>
#include <utility>

class QStorageInfo;

/// One mounted volume the user might want to scan.
struct VolumeInfo
{
	QString name;
	QString path;
	qint64 totalBytes = 0;
	qint64 usedBytes = 0;
	QString volumeType; ///< "Internal", "Network", or "Nexis".
	bool hasAvidMedia = false;
};

Q_DECLARE_METATYPE(VolumeInfo)

// MARK: - VolumeManager

/// Detects mounted volumes that might contain Avid media and watches
/// for changes. QStorageInfo alone misses some Nexis mounts, so
/// detection cross-references three sources (QStorageInfo, a direct
/// /Volumes walk on macOS, and known Avid install paths) and merges.
class VolumeManager : public QObject
{
	Q_OBJECT
public:
	explicit VolumeManager(QObject *parent = nullptr);

	// MARK: - Detection

	/// Synchronous; only call from the UI thread when blocking is
	/// acceptable (startup, 'Scan All'). The 5 second poll tick runs this
	/// off-thread via the shared pool; see pollVolumes / onPollFinished.
	QVector<VolumeInfo> detectVolumes() const;

	QStringList allScannablePaths() const;

	/// Prime the polling identity cache. Call from the UI thread after a
	/// synchronous detectVolumes() so the first poll has a baseline.
	void seedLastVolumes(QVector<VolumeInfo> drives)
	{
		m_lastVolumes = std::move(drives);
	}

	// MARK: - Monitoring

	/// Emits volumesChanged only when something the volume list shows changes
	/// (name, path, Avid presence, or type); identical polls stay silent.
	void startMonitoring(int intervalMs = 5000);

	/// Skip polling while a scan or file op is in flight; avoids
	/// contending with active ops over slow network mutexes.
	/// Fires an immediate poll on busy/false so changes surface
	/// promptly.
	void setBusy(bool busy);

	// MARK: - Full Disk Access

	/// Read the user's protected TCC database. Any failure returns false.
	static bool hasFullDiskAccess();

	static void openFullDiskAccessSettings();

signals:
	void volumesChanged(const QVector<VolumeInfo> &volumes);

public:
	/// Blocks until any in-flight detection finishes: the pool-thread lambda
	/// still holds `this`, so racing destruction would be a use-after-free.
	~VolumeManager() override;

private slots:
	void pollVolumes();
	void onPollFinished();

private:
	QTimer m_timer;
	QVector<VolumeInfo> m_lastVolumes;
	bool m_busy = false;

	QFutureWatcher<QVector<VolumeInfo>> m_pollWatcher;

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
