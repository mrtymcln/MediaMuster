#include "nativefile.h"

#include <QDir>
#include <QFile>
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
// MARK: - Durability barriers

SyncResult syncFile(QFile &f)
{
	// Flush QFile's userspace buffer before asking the OS to persist its writes.
	if (!f.flush())
		return SyncResult::Failed;
	const int fd = f.handle();
	if (fd == -1)
		return SyncResult::Failed;

#if defined(Q_OS_WIN)
	// _commit calls FlushFileBuffers, including the device cache request.
	return ::_commit(fd) == 0 ? SyncResult::Ok : SyncResult::Failed;
#elif defined(Q_OS_MAC)
	// fsync alone does not flush the device cache on macOS.
	if (::fcntl(fd, F_FULLFSYNC) == 0)
		return SyncResult::Ok;
	// Only an unsupported full barrier may fall back to fsync. An I/O error
	// must remain a failure so Move retains its source and recovery evidence.
	if (errno == ENOTSUP || errno == EINVAL || errno == ENOTTY)
		return ::fsync(fd) == 0 ? SyncResult::OkDegraded : SyncResult::Failed;
	return SyncResult::Failed;
#else
	return ::fsync(fd) == 0 ? SyncResult::Ok : SyncResult::Failed;
#endif
}

SyncResult directoryFlushResult(int code)
{
	if (code == 0)
		return SyncResult::Ok;
#ifdef Q_OS_WIN
	if (code == ERROR_INVALID_FUNCTION || code == ERROR_NOT_SUPPORTED)
#else
	if (code == ENOTSUP)
#endif
		return SyncResult::OkDegraded;
	return SyncResult::Failed;
}

SyncResult syncDirectory(const QString &dirPath, QString *error)
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
		return SyncResult::Failed;
	}
	const bool ok = ::FlushFileBuffers(h) != 0;
	const DWORD code = ok ? ERROR_SUCCESS : ::GetLastError();
	::CloseHandle(h);
	if (!ok && error)
		*error = QStringLiteral("FlushFileBuffers(directory) failed for %1 (Windows error %2).")
					 .arg(dirPath).arg(code);
	return directoryFlushResult(static_cast<int>(code));
#else
#ifdef Q_OS_MAC
	// Flushing or using a directory-relative operation needs traversal, not
	// directory enumeration. In particular macOS permits a known Trash item
	// while denying an O_RDONLY listing handle for its protected parent.
	const int fd = ::open(QFile::encodeName(dirPath).constData(), O_SEARCH | O_NOFOLLOW | O_CLOEXEC);
#else
	const int fd = ::open(QFile::encodeName(dirPath).constData(), O_RDONLY);
#endif
	if (fd == -1)
	{
		const int code = errno;
		if (error)
			*error = QStringLiteral("open(directory) failed for %1 (POSIX error %2: %3).")
						 .arg(dirPath).arg(code).arg(QString::fromLocal8Bit(std::strerror(code)));
		return SyncResult::Failed;
	}
	const bool ok = ::fsync(fd) == 0;
	const int code = ok ? 0 : errno;
	::close(fd);
	if (!ok && error)
		*error = QStringLiteral("fsync(directory) failed for %1 (POSIX error %2: %3).")
					 .arg(dirPath).arg(code).arg(QString::fromLocal8Bit(std::strerror(code)));
	return directoryFlushResult(code);
#endif
}

} // namespace NativeFile
