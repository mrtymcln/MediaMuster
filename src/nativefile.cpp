#include "nativefile.h"

#include <QDir>
#include <QFile>
#include <QStorageInfo>
#include <cerrno>
#include <cstring>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace NativeFile
{
// MARK: - Volume trust

bool isProvenLocalVolume(const QString &path)
{
	if (path.isEmpty())
		return false;

#if defined(Q_OS_WIN)
	// UNC paths first, before QStorageInfo gets a say: a share mounted
	// as \\server\share reports the SERVER'S filesystem name — usually
	// "NTFS" — which would sail through the allowlist below. The
	// prefix, not the filesystem name, is the truth here.
	if (path.startsWith(QStringLiteral("\\\\")) || path.startsWith(QStringLiteral("//")))
		return false;
#endif

	const QStorageInfo info(path);
	if (!info.isValid() || !info.isReady())
		return false;

#if defined(Q_OS_WIN)
	// Same trap, mapped-drive edition: an SMB share mounted as X: also
	// reports the remote filesystem's name. DRIVE_REMOTE is the OS's
	// own word for "this letter is a network mount".
	QString root = QDir::toNativeSeparators(info.rootPath());
	if (!root.isEmpty())
	{
		if (!root.endsWith(QLatin1Char('\\')))
			root += QLatin1Char('\\');
		if (::GetDriveTypeW(reinterpret_cast<const wchar_t *>(root.utf16())) == DRIVE_REMOTE)
			return false;
	}
#endif

	// Unknown and network filesystems are not qualified for persistent
	// volume identity. Every filesystem still uses the same verified copy.
	const QString fs = QString::fromLatin1(info.fileSystemType()).toLower();
	return fs == QLatin1String("apfs") || fs == QLatin1String("hfs") ||
		   fs == QLatin1String("ntfs") || fs == QLatin1String("refs");
}

// MARK: - Durability barriers

SyncResult syncFile(QFile &f, Durability level)
{
	// flush() first: QFile buffers writes in userspace, and fsync on a fd
	// whose bytes are still in that buffer would confirm nothing.
	if (!f.flush())
		return SyncResult::Failed;
	const int fd = f.handle();
	if (fd == -1)
		return SyncResult::Failed;

#if defined(Q_OS_WIN)
	// _commit calls FlushFileBuffers, which both writes the OS cache
	// through AND asks the device to flush its own cache — Windows has
	// no separate "harder" barrier, so Disk and Platter are one call.
	Q_UNUSED(level);
	return ::_commit(fd) == 0 ? SyncResult::Ok : SyncResult::Failed;
#else
#ifdef Q_OS_MAC
	// macOS is the platform where fsync explicitly does NOT flush the
	// drive's write cache; F_FULLFSYNC is the real barrier. It costs
	// milliseconds; the journal and verified copy both request it.
	if (level == Durability::Platter)
	{
		if (::fcntl(fd, F_FULLFSYNC) == 0)
			return SyncResult::Ok;
		// The errno decides what a refusal MEANS (adversarial review
		// 2026-08-30, finding 6). Only "this filesystem doesn't offer
		// the full barrier" may degrade: network mounts (SMB, Nexis)
		// answer ENOTSUP, some third-party filesystems EINVAL/ENOTTY —
		// there the server's write-acknowledgement is the only
		// guarantee anyway, and the caller records that honestly. Any
		// OTHER errno — above all EIO, the DEVICE failed the flush —
		// means the bytes may not be on the platter. Falling back to
		// plain fsync there would re-queue writes the OS already
		// considers done and return 0: success theatre over a dying
		// drive, right before a Move deletes the only other copy. That
		// must be Failed, so the caller keeps the source.
		if (errno == ENOTSUP || errno == EINVAL || errno == ENOTTY)
			return ::fsync(fd) == 0 ? SyncResult::OkDegraded : SyncResult::Failed;
		return SyncResult::Failed;
	}
#else
	Q_UNUSED(level);
#endif
	return ::fsync(fd) == 0 ? SyncResult::Ok : SyncResult::Failed;
#endif
}

bool syncDirectory(const QString &dirPath, QString *error)
{
	if (error)
		error->clear();
#if defined(Q_OS_WIN)
	// Directory access and flush support depend on the filesystem/client.
	// Keep refusals visible, including which native request failed.
	const QString native = QDir::toNativeSeparators(dirPath);
	const HANDLE h = ::CreateFileW(reinterpret_cast<const wchar_t *>(native.utf16()),
								   GENERIC_WRITE, // FlushFileBuffers requires write access
								   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
								   OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (h == INVALID_HANDLE_VALUE)
	{
		const DWORD code = ::GetLastError();
		if (error)
			*error = QStringLiteral("CreateFileW(directory) failed for %1 (Windows error %2).")
						 .arg(dirPath).arg(code);
		return false;
	}
	const bool ok = ::FlushFileBuffers(h) != 0;
	const DWORD code = ok ? ERROR_SUCCESS : ::GetLastError();
	::CloseHandle(h);
	if (!ok && error)
		*error = QStringLiteral("FlushFileBuffers(directory) failed for %1 (Windows error %2).")
					 .arg(dirPath).arg(code);
	return ok;
#else
	const int fd = ::open(QFile::encodeName(dirPath).constData(), O_RDONLY);
	if (fd == -1)
	{
		const int code = errno;
		if (error)
			*error = QStringLiteral("open(directory) failed for %1 (POSIX error %2: %3).")
						 .arg(dirPath).arg(code).arg(QString::fromLocal8Bit(std::strerror(code)));
		return false;
	}
	const bool ok = ::fsync(fd) == 0;
	const int code = ok ? 0 : errno;
	::close(fd);
	if (!ok && error)
		*error = QStringLiteral("fsync(directory) failed for %1 (POSIX error %2: %3).")
					 .arg(dirPath).arg(code).arg(QString::fromLocal8Bit(std::strerror(code)));
	return ok;
#endif
}

} // namespace NativeFile
