#include "nativefile.h"

#include <QDir>
#include <QFile>
#include <QStorageInfo>

#ifdef Q_OS_MAC
#include <cerrno>
#include <fcntl.h>
#include <sys/clonefile.h>
#endif
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

		// The allowlist itself. Everything else — smbfs, nfs, whatever a
		// Nexis calls its filesystem, exfat/msdos sticks, and names we have
		// never seen — deliberately fails. Unknown must fail safe: a wrongly
		// trusted volume gets native fast paths without verification and
		// file IDs that may not be stable, and nothing downstream would
		// notice.
		const QString fs = QString::fromLatin1(info.fileSystemType()).toLower();
		return fs == QLatin1String("apfs") || fs == QLatin1String("hfs") ||
			   fs == QLatin1String("ntfs") || fs == QLatin1String("refs");
	}

	// MARK: - Durability barriers

	SyncResult syncFile(QFile &f, Durability level)
	{
		// flush() first: Qt buffers writes in userspace, and fsync on a fd
		// whose bytes are still in Qt's buffer would confirm nothing.
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
		// milliseconds, which is why callers only ask for Platter at the
		// one instant that matters (before a Move deletes its source).
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

	bool syncDirectory(const QString &dirPath)
	{
#if defined(Q_OS_WIN)
		// FILE_FLAG_BACKUP_SEMANTICS is the only way CreateFileW hands out
		// a DIRECTORY handle; FlushFileBuffers on it persists the entries.
		// This is the upgrade path the v1 journal named in a comment and
		// never built — journals on Windows could vanish in a power cut
		// right after creation because only the file's bytes were synced,
		// not the directory entry naming it.
		const QString native = QDir::toNativeSeparators(dirPath);
		const HANDLE h = ::CreateFileW(reinterpret_cast<const wchar_t *>(native.utf16()),
									   GENERIC_WRITE, // FlushFileBuffers requires write access
									   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
									   nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (h == INVALID_HANDLE_VALUE)
			return false;
		const bool ok = ::FlushFileBuffers(h) != 0;
		::CloseHandle(h);
		return ok;
#else
		const int fd = ::open(QFile::encodeName(dirPath).constData(), O_RDONLY);
		if (fd == -1)
			return false;
		const bool ok = ::fsync(fd) == 0;
		::close(fd);
		return ok;
#endif
	}

	// MARK: - APFS clone

	bool clone(const QString &src, const QString &dst)
	{
#ifdef Q_OS_MAC
		// Test seam: force the buffered read/write path so its cancel and
		// failure-restore branches are reachable. clonefile is atomic and
		// same-volume, so it otherwise finishes before either can trigger.
		// Never set in production.
		if (qEnvironmentVariableIsSet("MEDIAMUSTER_DISABLE_CLONEFILE"))
			return false;
		// clonefile refuses an existing destination and refuses to cross
		// volumes — both failures are silent falls-through to the buffered
		// loop, not errors. The empty destination slot is guaranteed by the
		// caller's park-aside.
		return clonefile(QFile::encodeName(src).constData(), QFile::encodeName(dst).constData(),
						 0) == 0;
#else
		Q_UNUSED(src);
		Q_UNUSED(dst);
		return false;
#endif
	}

	// MARK: - Windows native copy

#if defined(Q_OS_WIN)
	namespace
	{
		struct WinCopyContext
		{
			const std::function<void(qint64, qint64)> *progress = nullptr;
			const std::atomic<bool> *cancel = nullptr;
		};

		// Called by the OS copy engine after each chunk. Returning
		// PROGRESS_CANCEL makes CopyFileExW abort AND delete its own
		// partial destination — the partial never survives to masquerade
		// as real media, which is the property the engine's own loop gets
		// from its ParkedFile discard.
		DWORD CALLBACK winCopyProgress(LARGE_INTEGER totalSize, LARGE_INTEGER transferred,
									   LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD, HANDLE, HANDLE,
									   LPVOID param)
		{
			const auto *ctx = static_cast<const WinCopyContext *>(param);
			if (ctx->cancel && ctx->cancel->load(std::memory_order_acquire))
				return PROGRESS_CANCEL;
			if (ctx->progress && *ctx->progress)
				(*ctx->progress)(transferred.QuadPart, totalSize.QuadPart);
			return PROGRESS_CONTINUE;
		}
	} // namespace
#endif

	WinCopyOutcome copyWin(const QString &src, const QString &dst,
						   const std::function<void(qint64, qint64)> &progress,
						   const std::atomic<bool> &cancel, QString *errorOut)
	{
#if defined(Q_OS_WIN)
		WinCopyContext ctx{&progress, &cancel};
		const QString nSrc = QDir::toNativeSeparators(src);
		const QString nDst = QDir::toNativeSeparators(dst);
		// FAIL_IF_EXISTS keeps the no-overwrite guarantee after the park;
		// see the header for why RESTARTABLE is deliberately absent.
		if (::CopyFileExW(reinterpret_cast<const wchar_t *>(nSrc.utf16()),
						  reinterpret_cast<const wchar_t *>(nDst.utf16()), winCopyProgress, &ctx,
						  nullptr, COPY_FILE_FAIL_IF_EXISTS))
			return WinCopyOutcome::Succeeded;

		const DWORD err = ::GetLastError();
		if (err == ERROR_REQUEST_ABORTED)
			return WinCopyOutcome::Cancelled;
		// A pre-existing destination is a RACE, not a copy failure: the
		// slot was checked and parked before this call, so whatever sits
		// there now belongs to another process and is not ours to remove.
		if (err == ERROR_FILE_EXISTS || err == ERROR_ALREADY_EXISTS)
			return WinCopyOutcome::RefusedExists;
		if (errorOut)
			*errorOut = qt_error_string(static_cast<int>(err));
		return WinCopyOutcome::Failed;
#else
		Q_UNUSED(src);
		Q_UNUSED(dst);
		Q_UNUSED(progress);
		Q_UNUSED(cancel);
		Q_UNUSED(errorOut);
		return WinCopyOutcome::Unavailable;
#endif
	}
} // namespace NativeFile
