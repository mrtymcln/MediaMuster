#include "volumemanager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QOperatingSystemVersion>
#include <QSet>
#include <QStorageInfo>
#include <QStandardPaths>
#include <QProcess>
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
	connect(&m_pollWatcher, &QFutureWatcher<QVector<VolumeInfo>>::finished,
	        this, &VolumeManager::onPollFinished);
}

VolumeManager::~VolumeManager()
{
	// Wait for the pool-thread lambda — it still holds `this`,
	// so racing destruction would be a use-after-free.
	if (m_pollWatcher.isRunning())
		m_pollWatcher.waitForFinished();
}

// MARK: - Monitoring

void VolumeManager::startMonitoring(int intervalMs)
{
	m_timer.start(intervalMs);
}

void VolumeManager::stopMonitoring()
{
	m_timer.stop();
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
	m_pollWatcher.setFuture(QtConcurrent::run(
	    [this]
	    { return detectVolumes(); }));
}

void VolumeManager::onPollFinished()
{
	// Identity-only compare: if pairs match the last poll, stay silent.
	const auto current = m_pollWatcher.result();
	const bool sameIdentity = current.size() == m_lastVolumes.size() &&
	                          std::equal(current.cbegin(), current.cend(), m_lastVolumes.cbegin(),
	                                     [](const VolumeInfo &a, const VolumeInfo &b)
	                                     {
		                                     return a.name == b.name && a.path == b.path;
	                                     });
	if (sameIdentity)
		return;

	m_lastVolumes = current;
	emit volumesChanged(current);
}

// MARK: - Known Avid locations

QStringList VolumeManager::knownAvidLocations()
{
	QStringList paths;

#ifdef Q_OS_MAC
	paths << "/Users/Shared/AvidMediaComposer"
	      << "/Users/Shared/Avid MediaComposer"
	      << QDir::homePath() + "/AvidMediaComposer"
	      << QDir::homePath() + "/Avid MediaComposer"
	      << QDir::homePath() + "/Documents/Avid MediaComposer";
#endif

#ifdef Q_OS_WIN
	paths << "C:/Users/Public/Documents/Avid Media Composer"
	      << "C:/Users/Public/AvidMediaComposer"
	      << QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Avid MediaComposer";
#endif

	return paths;
}

// MARK: - Detection

QVector<VolumeInfo> VolumeManager::detectVolumes() const
{
	QVector<VolumeInfo> volumes;
	QSet<QString> seenPaths;

	// Below this size = housekeeping/recovery/firmlink, not media.
	constexpr qint64 kMinAvidVolumeBytes = qint64(500) * 1024 * 1024;

	// MARK: Pass 1 — QStorageInfo (covers most mounts cleanly)

	for (const QStorageInfo &vol : QStorageInfo::mountedVolumes())
	{
		if (!vol.isValid() || !vol.isReady())
			continue;

		const QString mountPath = vol.rootPath();

		if (vol.bytesTotal() < kMinAvidVolumeBytes)
			continue;

#ifdef Q_OS_MAC
		if (mountPath.startsWith("/System/Volumes/") || mountPath == "/private/var/vm")
			continue;
#endif

		if (seenPaths.contains(mountPath))
			continue;
		seenPaths.insert(mountPath);

		QString name = vol.displayName();
		if (name.isEmpty())
			name = vol.name();

#ifdef Q_OS_MAC
		if (mountPath == "/")
		{
			if (name.isEmpty())
				name = "Macintosh HD";
		}
		else if (mountPath.startsWith("/Volumes/"))
		{
			name = QDir(mountPath).dirName();
		}
#endif
		if (name.isEmpty())
			name = mountPath;

		VolumeInfo info;
		info.name = name;
		info.path = mountPath;
		info.totalBytes = vol.bytesTotal();
		info.freeBytes = vol.bytesAvailable();
		info.usedBytes = info.totalBytes - info.freeBytes;
		info.volumeType = detectVolumeType(name, mountPath);
		info.hasAvidMedia = hasAvidMediaFolder(mountPath);
		info.isMounted = true;
		volumes.append(info);
	}

#ifdef Q_OS_MAC
	// MARK: Pass 2 — /Volumes walk (catches what QStorageInfo missed)

	// Some Nexis/SMB mounts are missing from
	// QStorageInfo::mountedVolumes() but appear in /Volumes.
	QDir volumesDir("/Volumes");
	if (volumesDir.exists())
	{
		for (const QFileInfo &entry : volumesDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
		{
			const QString path = entry.absoluteFilePath();
			if (seenPaths.contains(path))
				continue;

			seenPaths.insert(path);
			QStorageInfo si(path);
			VolumeInfo info;
			info.name = entry.fileName();
			info.path = path;
			info.totalBytes = si.isValid() ? si.bytesTotal() : 0;
			info.freeBytes = si.isValid() ? si.bytesAvailable() : 0;
			info.usedBytes = info.totalBytes - info.freeBytes;
			info.volumeType = detectVolumeType(info.name, path);
			info.hasAvidMedia = hasAvidMediaFolder(path);
			info.isMounted = true;
			if (info.totalBytes > 0)
				volumes.append(info);
		}
	}
#endif

	// MARK: Pass 3 — Avid directories on existing mounts

	for (const QString &avidPath : knownAvidLocations())
	{
		QDir avidDir(avidPath);
		if (!avidDir.exists() || seenPaths.contains(avidPath) || !avidDir.exists("Avid MediaFiles"))
			continue;

		seenPaths.insert(avidPath);
		QStorageInfo si(avidPath);
		VolumeInfo info;
		info.name = avidDir.dirName();
		info.path = avidPath;
		info.totalBytes = si.isValid() ? si.bytesTotal() : 0;
		info.freeBytes = si.isValid() ? si.bytesAvailable() : 0;
		info.usedBytes = info.totalBytes - info.freeBytes;
		info.volumeType = detectVolumeType(info.name, avidPath);
		info.hasAvidMedia = true;
		info.isMounted = true;
		volumes.append(info);
	}

	// MARK: Sort — Avid-bearing volumes first, then alphabetical

	std::sort(volumes.begin(), volumes.end(), [](const VolumeInfo &a, const VolumeInfo &b)
	          {
        if (a.hasAvidMedia != b.hasAvidMedia)
            return a.hasAvidMedia > b.hasAvidMedia;
        return a.name.toLower() < b.name.toLower(); });

	return volumes;
}

QStringList VolumeManager::allScannablePaths() const
{
	QStringList paths;
	for (const VolumeInfo &d : detectVolumes())
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
	const auto ventura = QOperatingSystemVersion(
	    QOperatingSystemVersion::MacOS, 13, 0);
	const QString url = (current >= ventura)
	                        ? QStringLiteral("x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_AllFiles")
	                        : QStringLiteral("x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles");
	QProcess::startDetached("open", {url});
#endif
}

// MARK: - Volume type heuristics

QString VolumeManager::detectVolumeType(const QString &name, const QString &path)
{
	const QString upper = name.toUpper();
	QStorageInfo si(path);
	const QString fsType = QString::fromLatin1(si.fileSystemType()).toLower();

	// Nexis has "AvidFOS" as filesystem. Name substring is a
	// fallback when the filesystem-type query is empty.
	if (fsType == "avidfos" || fsType == "avidfs" || upper.contains("NEXIS"))
		return "Nexis";

#ifdef Q_OS_MAC
	if (path == "/")
		return "System";

	if (fsType == "smbfs" || fsType == "afpfs" || fsType == "nfs" || fsType == "cifs")
		return "Network";
#endif

#ifdef Q_OS_WIN
	if (path.startsWith("\\\\"))
		return "Network"; // UNC

	// GetDriveTypeW needs wide chars; toStdWString() is correct here
	// (QFile::encodeName would hand narrow bytes to the W variant).
	const UINT volumeType = GetDriveTypeW(path.toStdWString().c_str());
	if (volumeType == DRIVE_REMOTE)
		return "Network";

	if (volumeType == DRIVE_FIXED)
	{
		const QString sysDrive = qEnvironmentVariable("SystemDrive");
		if (!sysDrive.isEmpty() && path.startsWith(sysDrive, Qt::CaseInsensitive))
			return "System";
	}
#endif

	return "Local";
}

bool VolumeManager::hasAvidMediaFolder(const QString &path)
{
	if (QDir(path).exists("Avid MediaFiles"))
		return true;

#ifdef Q_OS_MAC
	// System root has no `/Avid MediaFiles` — Media Composer
	// now installs to a known location instead.
	if (path == "/")
	{
		for (const QString &loc : knownAvidLocations())
		{
			if (QDir(loc).exists("Avid MediaFiles"))
				return true;
		}
	}
#endif

	return false;
}