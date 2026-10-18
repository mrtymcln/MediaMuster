#include "volumemanager.h"
#include "conventions.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStorageInfo>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// MARK: - Construction

VolumeManager::VolumeManager(QObject *parent)
	: QObject(parent)
{
}

// MARK: - Known Avid locations

QStringList VolumeManager::knownAvidLocations()
{
	// Other intact media trees can be added manually. Automatic detection
	// probes these bases directly rather than searching user folders.
	return Conventions::systemDriveMediaBases();
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

		// Record rejected mounts too, so the /Volumes fallback cannot add them back.
		if (seenPaths.contains(mountPath))
			continue;
		seenPaths.insert(mountPath);

		if (vol.bytesTotal() < kMinAvidVolumeBytes)
			continue;

		// Known Avid locations on system volumes are probed separately below.
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

	// A skipped system mount may still contain a supported Avid media location.
	const auto alreadyListed = [&volumes](const QString &path)
	{
		return std::any_of(volumes.cbegin(), volumes.cend(),
						   [&path](const VolumeInfo &v)
						   { return v.path == path; });
	};
	for (const QString &avidPath : knownAvidLocations())
	{
		QDir avidDir(avidPath);
		if (!avidDir.exists() || alreadyListed(avidPath) || !hasAvidMediaFolder(avidPath))
			continue;

		// A drive root has no dirName; show the path itself.
		QString name = avidDir.dirName();
		if (name.isEmpty())
			name = avidPath;
		volumes.append(makeVolumeInfo(name, avidPath, QStorageInfo(avidPath)));
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

// MARK: - Volume type heuristics

QString VolumeManager::detectVolumeType(const QString &name, const QString &path,
										const QStorageInfo &storage)
{
	const QString upper = name.toUpper();
	// Reuse the caller's QStorageInfo instead of a second statfs on `path`.
	// An unresponsive SMB/Nexis mount can make that extra stat block discovery.
	const QString fsType = QString::fromLatin1(storage.fileSystemType()).toLower();

	// Recognise Avid filesystem types or a volume name containing "NEXIS".
	// The name heuristic applies even when the filesystem type is known.
	if (fsType == "avidfos" || fsType == "avidfs" || upper.contains("NEXIS"))
		return QStringLiteral("Nexis");

#if defined(Q_OS_MAC)
	Q_UNUSED(path);
	if (fsType == "smbfs" || fsType == "afpfs" || fsType == "nfs" || fsType == "cifs")
		return QStringLiteral("Network");
#elif defined(Q_OS_WIN)
	if (path.startsWith("\\\\"))
		return QStringLiteral("Network"); // UNC

	if (GetDriveTypeW(path.toStdWString().c_str()) == DRIVE_REMOTE)
		return QStringLiteral("Network");
#else
	Q_UNUSED(path);
#endif

	// Local includes internal and external disks.
	return QStringLiteral("Local");
}

bool VolumeManager::hasAvidMediaFolder(const QString &path)
{
	const QDir dir(path);
	return dir.exists(Conventions::kAvidMediaFilesDir) || dir.exists(Conventions::kOmfMediaFilesDir);
}
