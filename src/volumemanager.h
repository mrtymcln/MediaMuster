#pragma once

#include "mediafile.h"
#include <QFutureWatcher>
#include <QObject>
#include <QTimer>

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

	/// Synchronous — only call from the UI thread when blocking is
	/// acceptable (startup, 'Scan All'). The 5 second poll tick runs this
	/// off-thread via QtConcurrent; see pollVolumes / onPollFinished.
	QVector<VolumeInfo> detectVolumes() const;

	QStringList allScannablePaths() const;

	/// Prime the polling identity cache. Call from the UI thread after a
	/// synchronous detectVolumes() so the first poll has a baseline.
	void seedLastVolumes(QVector<VolumeInfo> drives)
	{
		m_lastVolumes = std::move(drives);
	}

	// MARK: - Monitoring

	/// Emits volumesChanged only when the mount set (by name + path)
	/// actually changes — identical polls stay silent.
	void startMonitoring(int intervalMs = 5000);
	void stopMonitoring();

	/// Skip polling while a scan or file op is in flight — avoids
	/// contending with active ops over slow network mutexes.
	/// Fires an immediate poll on busy/false so changes surface
	/// promptly.
	void setBusy(bool busy);

	// MARK: - macOS TCC

	/// Without Full Disk Access, scanning any
	/// protected path fails silently with permission errors.
	static bool hasFullDiskAccess();

	static void openFullDiskAccessSettings();

signals:
	void volumesChanged(const QVector<VolumeInfo> &volumes);

public:
	/// Public so dtor can wait for in-flight detection.
	~VolumeManager() override;

private slots:
	void pollVolumes();
	void onPollFinished();

private:
	QTimer m_timer;
	QVector<VolumeInfo> m_lastVolumes;
	bool m_busy = false;

	QFutureWatcher<QVector<VolumeInfo>> m_pollWatcher;

	static QString detectVolumeType(const QString &name, const QString &path);
	static bool hasAvidMediaFolder(const QString &path);
	static QStringList knownAvidLocations();
};