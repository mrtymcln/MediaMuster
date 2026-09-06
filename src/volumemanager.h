#pragma once

#include "mediafile.h"
#include <QFutureWatcher>
#include <QObject>
#include <QTimer>

class QStorageInfo;

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

	// MARK: - macOS TCC

	/// Without Full Disk Access, scanning any
	/// protected path fails silently with permission errors.
	static bool hasFullDiskAccess();

	static void openFullDiskAccessSettings();

	// MARK: - Volume type

	/// "Internal", "Network", or "Nexis" for a mount, from its filesystem type
	/// (with a name/path fallback). Public because Icons::forVolumeType reuses
	/// it for hand-added paths — one list of network filesystems, so a
	/// detected volume and the same volume added by hand can't disagree.
	static QString detectVolumeType(const QString &name, const QString &path,
									const QStorageInfo &storage);

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

public:
	/// Stamp out a VolumeInfo from a name + path + its QStorageInfo. The
	/// three detection passes used to fill the same fields by hand; this is
	/// the one place that mapping lives. Bytes come back as 0 when `storage`
	/// is invalid (an unreadable /Volumes entry).
	///
	/// Public because a folder the user adds by hand has to become the same
	/// kind of row as a detected volume — same type, same size, same "has
	/// Avid media" bold. MainWindow::addVolumePath stamps one out here
	/// rather than filling the fields itself, which is how the two rows
	/// used to drift.
	static VolumeInfo makeVolumeInfo(const QString &name, const QString &path,
									 const QStorageInfo &storage);
};
