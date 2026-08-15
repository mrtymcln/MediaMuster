#include "volumemanager.h"
#include "avidlayout.h"
#include "logging.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QOperatingSystemVersion>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QtConcurrent>

#include <algorithm>

#ifdef Q_OS_MAC
#include <unistd.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// MARK: - Construction/destruction

VolumeManager::VolumeManager(QObject *parent)
	: QObject(parent)
{
	connect(&m_timer, &QTimer::timeout, this, &VolumeManager::pollVolumes);
	connect(&m_pollWatcher, &QFutureWatcher<QVector<VolumeInfo>>::finished, this,
			&VolumeManager::onPollFinished);
}

VolumeManager::~VolumeManager()
{
	// Wait for the pool-thread lambda; it still holds `this`,
	// so racing destruction would be a use-after-free.
	if (m_pollWatcher.isRunning())
		m_pollWatcher.waitForFinished();
}

// MARK: - Monitoring

void VolumeManager::startMonitoring(int intervalMs)
{
	qCDebug(lcVolume) << "monitoring volumes, polling every" << intervalMs << "ms";
	m_timer.start(intervalMs);
}

void VolumeManager::setBusy(bool busy)
{
	const bool wasBusy = m_busy;
	m_busy = busy;

	// Immediate catch-up poll so changes during busy surface fast.
	if (wasBusy && !busy)
		pollVolumes();
}

void VolumeManager::pollVolumes()
{
	// Don't compete with a running scan/op for slow-network I/O.
	if (m_busy)
		return;

	// Skip if a previous walk is still in flight. Next tick retries.
	if (m_pollWatcher.isRunning())
		return;

	// Off-thread so the UI doesn't hang on a flaky mount.
	// QFutureWatcher delivers the result to onPollFinished.
	m_pollWatcher.setFuture(QtConcurrent::run([this]
											  { return detectVolumes(); }));
}

void VolumeManager::onPollFinished()
{
	// Stay silent unless something the volume list actually shows changed: name,
	// path, the Avid badge/bold/sort-first key, or the type icon. Free space is
	// deliberately excluded — it churns by the byte and would rebuild the list on
	// every poll for no visible gain (it's a hover-only tooltip figure).
	const auto current = m_pollWatcher.result();
	const bool sameForDisplay =
		current.size() == m_lastVolumes.size() &&
		std::equal(current.cbegin(), current.cend(), m_lastVolumes.cbegin(),
				   [](const VolumeInfo &a, const VolumeInfo &b)
				   {
					   return a.name == b.name && a.path == b.path &&
							  a.hasAvidMedia == b.hasAvidMedia && a.volumeType == b.volumeType;
				   });
	if (sameForDisplay)
		return;

	// A mount, unmount, or display-affecting change happened. Log this event.
	QStringList summary;
	for (const VolumeInfo &v : current)
		summary << QStringLiteral("%1[%2%3]")
					   .arg(v.name, v.volumeType,
							v.hasAvidMedia ? QStringLiteral(",avid") : QString());
	qCInfo(lcVolume).noquote() << "volumes changed:" << current.size() << "mounted —"
							   << summary.join(QStringLiteral("  "));

	m_lastVolumes = current;
	emit volumesChanged(current);
}

// MARK: - Known Avid locations

QStringList VolumeManager::knownAvidLocations()
{
	QStringList paths;

#ifdef Q_OS_MAC
	paths << "/Users/Shared/AvidMediaComposer"
		  << "/Users/Shared/Avid MediaComposer" << QDir::homePath() + "/AvidMediaComposer"
		  << QDir::homePath() + "/Avid MediaComposer"
		  << QDir::homePath() + "/Documents/Avid MediaComposer";
#endif

#ifdef Q_OS_WIN
	paths << "C:/Users/Public/Documents/Avid Media Composer"
		  << "C:/Users/Public/AvidMediaComposer"
		  << QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
				 "/Avid MediaComposer";
#endif

	return paths;
}

// MARK: - VolumeInfo factory

VolumeInfo VolumeManager::makeVolumeInfo(const QString &name, const QString &path,
										 const QStorageInfo &storage)
{
	VolumeInfo info;
	info.name = name;
	info.path = path;
	if (storage.isValid())
	{
		info.totalBytes = storage.bytesTotal();
		info.usedBytes = info.totalBytes - storage.bytesAvailable();
	}
	info.volumeType = detectVolumeType(name, path, storage);
	info.hasAvidMedia = hasAvidMediaFolder(path);
	return info;
}

// MARK: - Detection

QVector<VolumeInfo> VolumeManager::detectVolumes() const
{
	QVector<VolumeInfo> volumes;
	QSet<QString> seenPaths;
	constexpr qint64 kMinAvidVolumeBytes = qint64(500) * 1024 * 1024;

	// MARK: Pass 1 — QStorageInfo (covers most mounts cleanly)

	for (const QStorageInfo &vol : QStorageInfo::mountedVolumes())
	{
		if (!vol.isValid() || !vol.isReady())
			continue;

		const QString mountPath = vol.rootPath();

		// Mark every mount we rule on as seen — whether we keep OR reject it —
		// before applying the filters below. Otherwise the /Volumes walk in
		// pass 2 resurrects anything we filtered out here (a sub-500 MB DMG at
		// /Volumes/Foo would slip straight back in).
		if (seenPaths.contains(mountPath))
			continue;
		seenPaths.insert(mountPath);

		if (vol.bytesTotal() < kMinAvidVolumeBytes)
			continue;

		// Skip the system/boot volume: MC 2020.4 or later can no longer write media
		// to a system-volume root (macOS SIP / Windows protection), so it never holds
		// Avid media there. Real system-drive media lives in Users/Shared or
		// Public\Documents and is surfaced by pass 3; a user who needs the system
		// volume itself can still add it via File > Add Folder or Volume.
#if defined(Q_OS_MAC)
		if (mountPath == "/" || mountPath.startsWith("/System/Volumes/") ||
			mountPath == "/private/var/vm")
			continue;
#elif defined(Q_OS_WIN)
		if (const QString sysDrive = qEnvironmentVariable("SystemDrive");
			!sysDrive.isEmpty() && mountPath.startsWith(sysDrive, Qt::CaseInsensitive))
			continue;
#endif

		QString name = vol.displayName();
		if (name.isEmpty())
			name = vol.name();

#ifdef Q_OS_MAC
		if (mountPath.startsWith("/Volumes/"))
			name = QDir(mountPath).dirName();
#endif
		if (name.isEmpty())
			name = mountPath;

		volumes.append(makeVolumeInfo(name, mountPath, vol));
	}

#ifdef Q_OS_MAC
	// MARK: Pass 2 — /Volumes walk (catches what QStorageInfo missed)

	QDir volumesDir("/Volumes");
	if (volumesDir.exists())
	{
		for (const QFileInfo &entry : volumesDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
		{
			const QString path = entry.absoluteFilePath();
			if (seenPaths.contains(path))
				continue;

			seenPaths.insert(path);
			const VolumeInfo info = makeVolumeInfo(entry.fileName(), path, QStorageInfo(path));
			if (info.totalBytes > 0)
				volumes.append(info);
		}
	}
#endif

	// MARK: Pass 3 — Avid directories on existing mounts

	for (const QString &avidPath : knownAvidLocations())
	{
		QDir avidDir(avidPath);
		if (!avidDir.exists() || seenPaths.contains(avidPath) || !hasAvidMediaFolder(avidPath))
			continue;

		seenPaths.insert(avidPath);
		// Path is a known Avid location (loop guard above), so hasAvidMedia
		// resolves true via hasAvidMediaFolder — no need to force it here.
		volumes.append(makeVolumeInfo(avidDir.dirName(), avidPath, QStorageInfo(avidPath)));
	}

	// MARK: Sort — Avid-bearing volumes first, then alphabetical

	std::sort(volumes.begin(), volumes.end(),
			  [](const VolumeInfo &a, const VolumeInfo &b)
			  {
				  if (a.hasAvidMedia != b.hasAvidMedia)
					  return a.hasAvidMedia > b.hasAvidMedia;
				  return a.name.toLower() < b.name.toLower();
			  });

	return volumes;
}

QStringList VolumeManager::allScannablePaths() const
{
	// Reuse the most recent detection rather than re-walking every mount.
	// detectVolumes() stat-walks QStorageInfo, the /Volumes tree, and the
	// known Avid locations — seconds on a slow Nexis/SMB share — and the 5 s
	// poller (plus the startup seed) already keeps m_lastVolumes current, so
	// "Scan All" stays instant and matches the volume list on screen. Fall
	// back to a fresh walk only when nothing has been detected yet.
	const QVector<VolumeInfo> volumes = m_lastVolumes.isEmpty() ? detectVolumes() : m_lastVolumes;

	QStringList paths;
	paths.reserve(volumes.size());
	for (const VolumeInfo &d : volumes)
		paths.append(d.path);
	return paths;
}

// MARK: - macOS Full Disk Access

bool VolumeManager::hasFullDiskAccess()
{
#ifdef Q_OS_MAC
	const QStringList probes = {
		QDir::homePath() + "/Library/Application Support/com.apple.TCC",
		QDir::homePath() + "/Library/Safari",
		QDir::homePath() + "/Library/Mail",
		QDir::homePath() + "/Library/Calendars",
	};

	for (const QString &path : probes)
	{
		const QByteArray native = QFile::encodeName(path);
		if (::access(native.constData(), F_OK) != 0)
			continue;
		return ::access(native.constData(), R_OK) == 0;
	}
	return true;
#else
	return true;
#endif
}

void VolumeManager::openFullDiskAccessSettings()
{
#ifdef Q_OS_MAC
	const auto current = QOperatingSystemVersion::current();
	const auto ventura = QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 13, 0);
	const QString url =
		(current >= ventura)
			? QStringLiteral("x-apple.systempreferences:com.apple.settings.PrivacySecurity."
							 "extension?Privacy_AllFiles")
			: QStringLiteral(
				  "x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles");
	QProcess::startDetached("open", {url});
#endif
}

// MARK: - Volume type heuristics

QString VolumeManager::detectVolumeType(const QString &name, const QString &path,
										const QStorageInfo &storage)
{
	const QString upper = name.toUpper();
	// Reuse the caller's QStorageInfo instead of a second statfs on `path`; on a
	// dead SMB/Nexis mount that stat can block for seconds, every 5 s poll.
	const QString fsType = QString::fromLatin1(storage.fileSystemType()).toLower();

	// Nexis has "AvidFOS" as filesystem. Name substring is a
	// fallback when the filesystem-type query is empty.
	if (fsType == "avidfos" || fsType == "avidfs" || upper.contains("NEXIS"))
		return QStringLiteral("Nexis");

#if defined(Q_OS_MAC)
	Q_UNUSED(path);
	if (fsType == "smbfs" || fsType == "afpfs" || fsType == "nfs" || fsType == "cifs")
		return QStringLiteral("Network");
#elif defined(Q_OS_WIN)
	if (path.startsWith("\\\\"))
		return QStringLiteral("Network"); // UNC

	// GetDriveTypeW needs wide chars; toStdWString() is correct here
	// (QFile::encodeName would hand narrow bytes to the W variant).
	if (GetDriveTypeW(path.toStdWString().c_str()) == DRIVE_REMOTE)
		return QStringLiteral("Network");
#else
	Q_UNUSED(path);
#endif

	// Everything else — fixed and local disks — is Internal.
	return QStringLiteral("Internal");
}

bool VolumeManager::hasAvidMediaFolder(const QString &path)
{
	// The system root's own `<root>/Avid MediaFiles` case is gone now that the
	// boot volume is skipped; the known Avid locations are surfaced directly by
	// detectVolumes' pass 3, each of which matches here on its own path.
	return QDir(path).exists(AvidLayout::kAvidMediaFilesDir);
}