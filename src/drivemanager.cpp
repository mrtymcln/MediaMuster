#include "drivemanager.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStorageInfo>
#include <QStandardPaths>
#include <QProcess>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

DriveManager::DriveManager(QObject *parent) : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &DriveManager::pollDrives);
}

void DriveManager::startMonitoring(int intervalMs)
{
    m_timer.start(intervalMs);
}

void DriveManager::stopMonitoring()
{
    m_timer.stop();
}

void DriveManager::pollDrives()
{
    auto current = detectDrives();
    bool changed = current.size() != m_lastDrives.size();
    if (!changed)
    {
        for (int i = 0; i < static_cast<int>(current.size()); ++i)
        {
            if (current[i].name != m_lastDrives[i].name ||
                current[i].path != m_lastDrives[i].path)
            {
                changed = true;
                break;
            }
        }
    }
    if (changed)
    {
        m_lastDrives = current;
        emit drivesChanged(current);
    }
}

QStringList DriveManager::knownAvidLocations()
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

QVector<DriveInfo> DriveManager::detectDrives() const
{
    QVector<DriveInfo> drives;
    QSet<QString> seenPaths;

    // QStorageInfo covers most cases but can miss Nexis and some network mounts.
    for (const QStorageInfo &vol : QStorageInfo::mountedVolumes())
    {
        if (!vol.isValid() || !vol.isReady())
            continue;

        QString mountPath = vol.rootPath();
        if (vol.bytesTotal() < static_cast<qint64>(500) * 1024 * 1024)
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
#ifdef Q_OS_WIN
        if (name.isEmpty())
            name = mountPath;
#endif
        if (name.isEmpty())
            name = mountPath;

        DriveInfo info;
        info.name = name;
        info.path = mountPath;
        info.totalBytes = vol.bytesTotal();
        info.freeBytes = vol.bytesAvailable();
        info.usedBytes = info.totalBytes - info.freeBytes;
        info.driveType = detectDriveType(name, mountPath);
        info.hasAvidMedia = hasAvidMediaFolder(mountPath);
        info.isMounted = true;
        drives.append(info);
    }

#ifdef Q_OS_MAC
    // Walk /Volumes directly to catch anything QStorageInfo skips.
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
            DriveInfo info;
            info.name = entry.fileName();
            info.path = path;
            info.totalBytes = si.isValid() ? si.bytesTotal() : 0;
            info.freeBytes = si.isValid() ? si.bytesAvailable() : 0;
            info.usedBytes = info.totalBytes - info.freeBytes;
            info.driveType = detectDriveType(info.name, path);
            info.hasAvidMedia = hasAvidMediaFolder(path);
            info.isMounted = true;
            if (info.totalBytes > 0)
                drives.append(info);
        }
    }
#endif

    // Pick up Avid directories that sit inside an already-mounted
    // volume (typically the boot drive) rather than on their own mount point.
    for (const QString &avidPath : knownAvidLocations())
    {
        QDir avidDir(avidPath);
        if (!avidDir.exists() || seenPaths.contains(avidPath) || !avidDir.exists("Avid MediaFiles"))
            continue;

        seenPaths.insert(avidPath);
        QStorageInfo si(avidPath);
        DriveInfo info;
        info.name = avidDir.dirName();
        info.path = avidPath;
        info.totalBytes = si.isValid() ? si.bytesTotal() : 0;
        info.freeBytes = si.isValid() ? si.bytesAvailable() : 0;
        info.usedBytes = info.totalBytes - info.freeBytes;
        info.driveType = detectDriveType(info.name, avidPath);
        info.hasAvidMedia = true;
        info.isMounted = true;
        drives.append(info);
    }

#ifdef Q_OS_WIN
    // Probe well-known Nexis UNC paths that Windows won't enumerate on its own.
    for (const char *unc : {"\\\\Nexis", "\\\\Avid"})
    {
        QString uncPath = QString::fromLatin1(unc);
        if (QDir(uncPath).exists() && !seenPaths.contains(uncPath))
        {
            seenPaths.insert(uncPath);
            QStorageInfo si(uncPath);
            DriveInfo info;
            info.name = uncPath.mid(2);
            info.path = uncPath;
            info.totalBytes = si.isValid() ? si.bytesTotal() : 0;
            info.freeBytes = si.isValid() ? si.bytesAvailable() : 0;
            info.usedBytes = info.totalBytes - info.freeBytes;
            info.driveType = "Nexis";
            info.hasAvidMedia = hasAvidMediaFolder(uncPath);
            info.isMounted = true;
            drives.append(info);
        }
    }
#endif

    std::sort(drives.begin(), drives.end(), [](const DriveInfo &a, const DriveInfo &b)
              {
        if (a.hasAvidMedia != b.hasAvidMedia)
            return a.hasAvidMedia > b.hasAvidMedia;
        return a.name.toLower() < b.name.toLower(); });

    return drives;
}

QStringList DriveManager::allScannablePaths() const
{
    QStringList paths;
    for (const DriveInfo &d : detectDrives())
        paths.append(d.path);
    return paths;
}

bool DriveManager::hasFullDiskAccess()
{
#ifdef Q_OS_MAC
    // ~/Library/Safari/Bookmarks.plist is TCC-protected — readable only with
    // Full Disk Access. Fall back to ~/Library/Mail if Safari isn't installed.
    QFileInfo fi(QDir::homePath() + "/Library/Safari/Bookmarks.plist");
    if (!fi.exists())
        fi.setFile(QDir::homePath() + "/Library/Mail");
    return fi.exists() && fi.isReadable();
#else
    return true;
#endif
}

void DriveManager::openFullDiskAccessSettings()
{
#ifdef Q_OS_MAC
    QProcess::startDetached("open",
                            {"x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles"});
#endif
}

QString DriveManager::detectDriveType(const QString &name, const QString &path)
{
    const QString upper = name.toUpper();
    QStorageInfo si(path);
    const QString fsType = QString::fromLatin1(si.fileSystemType()).toLower();

    // AvidFOS is the Nexis filesystem. The name check is a fallback for when
    // the FS query comes back empty but the volume is clearly named after Nexis.
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
        return "Network";

    const UINT driveType = GetDriveTypeW(path.toStdWString().c_str());
    if (driveType == DRIVE_REMOTE)
        return "Network";

    if (driveType == DRIVE_FIXED)
    {
        const QString sysDrive = qEnvironmentVariable("SystemDrive");
        if (!sysDrive.isEmpty() && path.startsWith(sysDrive, Qt::CaseInsensitive))
            return "System";
    }
#endif

    return "Local";
}

bool DriveManager::hasAvidMediaFolder(const QString &path)
{
    if (QDir(path).exists("Avid MediaFiles"))
        return true;

#ifdef Q_OS_MAC
    // System drive: check the well-known install locations instead of the root.
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