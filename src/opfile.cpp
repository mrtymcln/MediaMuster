#include "opfile.h"

#include <QDir>
#include <QFileInfo>
#include <QUuid>
#include <cstring>
#ifdef Q_OS_WIN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#ifdef Q_OS_MAC
#include <stdio.h>
#include <copyfile.h>
#include <sys/acl.h>
#endif
#endif

namespace
{
	QString hex(quint64 value)
	{
		return QString::number(value, 16);
	}
	bool privateDirectoryName(const QString &path, bool stageOnly = false)
	{
		const QString name = QFileInfo(path).fileName();
		const QStringList prefixes = stageOnly
			? QStringList{QStringLiteral(".mediamuster-stage-"), QStringLiteral(".mediamuster-")}
			: QStringList{QStringLiteral(".mediamuster-stage-"), QStringLiteral(".mediamuster-retire-"),
						  QStringLiteral(".mediamuster-")};
		for (const auto &prefix : prefixes)
		{
			if (!name.startsWith(prefix))
				continue;
			const auto token = name.mid(prefix.size());
			const QUuid uuid(token);
			if (!uuid.isNull() && token == uuid.toString(QUuid::WithoutBraces))
				return true;
		}
		return false;
	}
#ifdef Q_OS_WIN
	HANDLE handle(const QFile &file)
	{
		return reinterpret_cast<HANDLE>(::_get_osfhandle(file.handle()));
	}
	QString nativeError()
	{
		return QStringLiteral("Windows file error %1").arg(::GetLastError());
	}
	OpStamp nativeStamp(HANDLE file)
	{
		OpStamp out;
		BY_HANDLE_FILE_INFORMATION info{};
		if (!::GetFileInformationByHandle(file, &info))
			return out;
		FILE_ID_INFO extended{};
		if (::GetFileInformationByHandleEx(file, FileIdInfo, &extended, sizeof(extended)))
		{
			out.fileId = QString::fromLatin1(
				QByteArray(reinterpret_cast<const char *>(extended.FileId.Identifier), 16).toHex());
			out.volumeId = hex(extended.VolumeSerialNumber);
		}
		else
		{
			out.fileId = hex((quint64(info.nFileIndexHigh) << 32) | info.nFileIndexLow);
			out.volumeId = hex(info.dwVolumeSerialNumber);
		}
		out.size = qint64((quint64(info.nFileSizeHigh) << 32) | info.nFileSizeLow);
		out.modified = qint64((quint64(info.ftLastWriteTime.dwHighDateTime) << 32) |
							  info.ftLastWriteTime.dwLowDateTime);
		return out;
	}
	HANDLE openDirectory(const QString &path, bool deleting = false)
	{
		const auto native = QDir::toNativeSeparators(path);
		HANDLE directory = ::CreateFileW(reinterpret_cast<const wchar_t *>(native.utf16()),
			FILE_READ_ATTRIBUTES | (deleting ? DELETE : 0),
			deleting ? FILE_SHARE_READ : FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (directory == INVALID_HANDLE_VALUE)
			return directory;
		BY_HANDLE_FILE_INFORMATION info{};
		if (!::GetFileInformationByHandle(directory, &info) ||
			!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
			(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
		{
			::CloseHandle(directory);
			::SetLastError(ERROR_DIRECTORY);
			return INVALID_HANDLE_VALUE;
		}
		return directory;
	}
#else
	QString nativeError()
	{
		return QString::fromLocal8Bit(std::strerror(errno));
	}
	OpStamp nativeStamp(const struct stat &info)
	{
		OpStamp out;
		out.fileId = hex(info.st_ino);
		out.volumeId = hex(info.st_dev);
		out.size = info.st_size;
#ifdef Q_OS_MAC
		out.modified = info.st_mtimespec.tv_sec * 1000000000LL + info.st_mtimespec.tv_nsec;
#else
		out.modified = info.st_mtim.tv_sec * 1000000000LL + info.st_mtim.tv_nsec;
#endif
		return out;
	}
	bool privateDirectory(int directory, struct stat &info)
	{
		if (::fstat(directory, &info) != 0 || !S_ISDIR(info.st_mode) ||
			info.st_uid != ::geteuid() || (info.st_mode & 0777) != 0700)
			return false;
#ifdef Q_OS_MAC
		// chmod's mode bits do not remove inherited ACL grants. A directory used
		// for descriptor-relative unlink must also have no extended access list.
		acl_t acl = ::acl_get_fd_np(directory, ACL_TYPE_EXTENDED);
		if (!acl)
			// On APFS an empty ACL has no stored attribute: this descriptor-based
			// query reports ENOENT even though fstat above confirmed the directory.
			return errno == ENOENT || errno == ENOTSUP;
		acl_entry_t entry{};
		errno = 0;
		const int rc = ::acl_get_entry(acl, ACL_FIRST_ENTRY, &entry);
		const bool empty = rc == -1 && errno == EINVAL;
		::acl_free(acl);
		return empty;
#else
		return true;
#endif
	}
#endif
} // namespace

bool OpStamp::sameObject(const OpStamp &other) const
{
	return valid() && other.valid() && fileId == other.fileId && volumeId == other.volumeId;
}
bool OpStamp::unchanged(const OpStamp &other) const
{
	return sameObject(other) && size == other.size && modified == other.modified;
}
QJsonObject OpStamp::json() const
{
	return {{"file", fileId},
			{"volume", volumeId},
			{"size", QString::number(size)},
			{"modified", QString::number(modified)}};
}
OpStamp OpStamp::fromJson(const QJsonObject &v)
{
	bool sizeOk = false, timeOk = false;
	const auto size = v["size"].toString().toLongLong(&sizeOk);
	const auto modified = v["modified"].toString().toLongLong(&timeOk);
	if (!sizeOk || !timeOk)
		return {};
	return {v["file"].toString(), v["volume"].toString(), size, modified};
}

bool OpFile::safePath(const QString &path)
{
	if (!QDir::isAbsolutePath(path) || QDir::cleanPath(path) != path)
		return false;
	// Reject links/reparse points in existing components. This is a scope
	// check, not a claim that path inspection provides a filesystem lock.
	QString cursor = path;
	while (!cursor.isEmpty())
	{
		const QFileInfo fi(cursor);
		if (fi.isSymLink())
			return false;
#ifdef Q_OS_WIN
		const auto attrs = ::GetFileAttributesW(
			reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(cursor).utf16()));
		if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
			return false;
#endif
		const QString parent = fi.absolutePath();
		if (parent == cursor)
			break;
		cursor = parent;
	}
	return true;
}

bool OpFile::occupied(const QString &path)
{
	const QFileInfo fi(path);
	return fi.exists() || fi.isSymLink();
}

NativeFile::SyncResult OpFile::makeDirectory(const QString &path, QString &error,
											 const NativeFile::DirectorySync &sync)
{
	using Sync = NativeFile::SyncResult;
	if (!safePath(path))
	{
		error = QStringLiteral("A path contains a symbolic link or unsupported reparse point: %1")
					.arg(path);
		return Sync::Failed;
	}
	if (QFileInfo(path).isDir())
		return Sync::Ok;
	const QString parent = QFileInfo(path).absolutePath();
	if (parent == path)
		return Sync::Failed;
	const auto parentSync = makeDirectory(parent, error, sync);
	if (parentSync == Sync::Failed)
		return Sync::Failed;
	const QString parentError = error;
	if (!QDir().mkdir(path) && !QFileInfo(path).isDir())
	{
		error = QStringLiteral("Cannot create folder %1").arg(path);
		return Sync::Failed;
	}
	QString syncError;
	const auto synced = sync(parent, &syncError);
	if (synced != Sync::Ok)
	{
		error = QStringLiteral("Folder created, but its directory persistence is unconfirmed: %1\n%2")
					.arg(path, syncError);
		return synced;
	}
	error = parentError;
	return parentSync;
}

OpStamp OpFile::inspectDirectory(const QString &path)
{
	if (!safePath(path))
		return {};
#ifdef Q_OS_WIN
	const auto directory = openDirectory(path);
	if (directory == INVALID_HANDLE_VALUE)
		return {};
	const auto identity = nativeStamp(directory);
	::CloseHandle(directory);
#else
	const int directory = ::open(QFile::encodeName(path).constData(),
		O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (directory < 0)
		return {};
	struct stat info{};
	const auto identity = ::fstat(directory, &info) == 0 ? nativeStamp(info) : OpStamp{};
	::close(directory);
#endif
	return identity;
}

NativeFile::SyncResult OpFile::makePrivateDirectory(
	const QString &path, OpStamp &identity, QString &error, const NativeFile::DirectorySync &sync)
{
	using Sync = NativeFile::SyncResult;
	identity = {};
	if (!safePath(path) || !privateDirectoryName(path))
	{
		error = QStringLiteral("A private operation folder requires a fresh, unredirected UUID path.");
		return Sync::Failed;
	}
#ifdef Q_OS_WIN
	const auto native = QDir::toNativeSeparators(path);
	if (!::CreateDirectoryW(reinterpret_cast<const wchar_t *>(native.utf16()), nullptr))
	{
		error = nativeError();
		return Sync::Failed;
	}
	// Windows cleanup targets protected object handles. It does not rely on
	// Unix permission bits or pathname-based file deletion.
	const auto directory = openDirectory(path);
	if (directory == INVALID_HANDLE_VALUE)
	{
		error = nativeError();
		return Sync::Failed;
	}
	identity = nativeStamp(directory);
	::CloseHandle(directory);
#else
	if (::mkdir(QFile::encodeName(path).constData(), 0700) != 0)
	{
		error = nativeError();
		return Sync::Failed;
	}
	const int directory = ::open(QFile::encodeName(path).constData(),
		O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (directory < 0)
	{
		error = nativeError();
		return Sync::Failed;
	}
	bool permissions = ::fchmod(directory, 0700) == 0;
#ifdef Q_OS_MAC
	acl_t empty = ::acl_init(0);
	if (!empty)
		permissions = false;
	else
	{
		if (::acl_set_fd_np(directory, empty, ACL_TYPE_EXTENDED) != 0 && errno != ENOTSUP)
			permissions = false;
		::acl_free(empty);
	}
#endif
	struct stat info{};
	if (!permissions || !privateDirectory(directory, info))
	{
		::close(directory);
		error = QStringLiteral("The operation folder could not be made private: %1").arg(path);
		return Sync::Failed;
	}
	identity = nativeStamp(info);
	::close(directory);
#endif
	if (!identity.valid() || !identity.sameObject(inspectDirectory(path)))
	{
		identity = {};
		error = QStringLiteral("The newly created operation folder could not be identified.");
		return Sync::Failed;
	}
	// Preserve the recorded identity even if persistence is unavailable. The
	// caller can journal the folder and later clean it without adopting a path.
	const auto ownSync = sync(path, &error);
	if (ownSync == Sync::Failed)
		return ownSync;
	const QString ownError = error;
	const auto parentSync = sync(QFileInfo(path).absolutePath(), &error);
	if (parentSync != Sync::Ok)
		return parentSync;
	error = ownError;
	return ownSync;
}

NativeFile::SyncResult OpFile::removeEmptyPrivateDirectory(
	const QString &path, const OpStamp &identity, QString &error,
	const NativeFile::DirectorySync &sync)
{
	using Sync = NativeFile::SyncResult;
	if (!identity.valid() || !safePath(path) || !privateDirectoryName(path))
	{
		error = QStringLiteral("Folder cleanup requires its recorded private directory identity.");
		return Sync::Failed;
	}
#ifdef Q_OS_WIN
	const auto directory = openDirectory(path, true);
	if (directory == INVALID_HANDLE_VALUE)
	{
		error = nativeError();
		return Sync::Failed;
	}
	if (!identity.sameObject(nativeStamp(directory)))
	{
		::CloseHandle(directory);
		error = QStringLiteral("The private folder was replaced; it was retained.");
		return Sync::Failed;
	}
	FILE_DISPOSITION_INFO disposition{};
	disposition.DeleteFile = TRUE;
	// The kernel rejects nonempty directories. Never enumerate/delete children.
	const bool removed = ::SetFileInformationByHandle(directory, FileDispositionInfo,
		&disposition, sizeof(disposition)) != 0;
	if (!removed)
		error = nativeError();
	::CloseHandle(directory);
	if (!removed)
		return Sync::Failed;
#else
	const QString parentPath = QFileInfo(path).absolutePath();
	const QByteArray name = QFile::encodeName(QFileInfo(path).fileName());
	const int parent = ::open(QFile::encodeName(parentPath).constData(),
		O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (parent < 0)
	{
		error = nativeError();
		return Sync::Failed;
	}
	const int directory = ::openat(parent, name.constData(),
		O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	struct stat info{}, atPath{};
	const bool matches = directory >= 0 && privateDirectory(directory, info) &&
		identity.sameObject(nativeStamp(info)) &&
		::fstatat(parent, name.constData(), &atPath, AT_SYMLINK_NOFOLLOW) == 0 &&
		S_ISDIR(atPath.st_mode) && identity.sameObject(nativeStamp(atPath));
	if (!matches)
	{
		if (directory >= 0)
			::close(directory);
		::close(parent);
		error = QStringLiteral("The private folder identity or permissions changed; it was retained.");
		return Sync::Failed;
	}
	// This can only remove an empty directory. Even if an external process
	// changes the name after validation, AT_REMOVEDIR cannot remove any files.
	const bool removed = ::unlinkat(parent, name.constData(), AT_REMOVEDIR) == 0;
	if (!removed)
		error = nativeError();
	::close(directory);
	::close(parent);
	if (!removed)
		return Sync::Failed;
#endif
	if (occupied(path))
	{
		error = QStringLiteral("Folder cleanup is unconfirmed; the path remains occupied.");
		return Sync::Failed;
	}
	return sync(QFileInfo(path).absolutePath(), &error);
}

std::unique_ptr<OpFile> OpFile::open(const QString &path, bool create, QString &error)
{
	return openImpl(path, create, create, error);
}

std::unique_ptr<OpFile> OpFile::openWritableExisting(const QString &path, QString &error)
{
	return openImpl(path, false, true, error);
}

std::unique_ptr<OpFile> OpFile::openImpl(const QString &path, bool create, bool writable,
										 QString &error)
{
	if (!safePath(path))
	{
		error = QStringLiteral("Unsupported or redirected path: %1").arg(path);
		return {};
	}
	auto out = std::unique_ptr<OpFile>(new OpFile);
	out->m_path = path;
	out->m_created = create;
	int fd = -1;
#ifdef Q_OS_WIN
	const QString native = QDir::toNativeSeparators(path);
	const DWORD access = GENERIC_READ | DELETE | (writable ? GENERIC_WRITE : 0);
	HANDLE h = ::CreateFileW(reinterpret_cast<const wchar_t *>(native.utf16()), access,
							 FILE_SHARE_READ, nullptr, create ? CREATE_NEW : OPEN_EXISTING,
							 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	out->m_protected = h != INVALID_HANDLE_VALUE;
	if (h == INVALID_HANDLE_VALUE && !create && !writable)
	{
		h = ::CreateFileW(reinterpret_cast<const wchar_t *>(native.utf16()), GENERIC_READ,
						  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
						  OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	}
	if (h == INVALID_HANDLE_VALUE)
	{
		error = nativeError();
		return {};
	}
	BY_HANDLE_FILE_INFORMATION info{};
	if (!::GetFileInformationByHandle(h, &info) ||
		(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
	{
		::CloseHandle(h);
		error = QStringLiteral("Only regular files are supported.");
		return {};
	}
	fd = ::_open_osfhandle(reinterpret_cast<intptr_t>(h),
						   _O_BINARY | (writable ? _O_RDWR : _O_RDONLY));
	if (fd < 0)
	{
		::CloseHandle(h);
		error = QStringLiteral("Cannot attach file handle.");
		return {};
	}
#else
	fd = ::open(QFile::encodeName(path).constData(),
				(create ? O_RDWR | O_CREAT | O_EXCL : writable ? O_RDWR
															   : O_RDONLY) |
					O_NOFOLLOW | O_CLOEXEC,
				0600);
	if (fd < 0)
	{
		error = nativeError();
		return {};
	}
	struct stat info{};
	if (::fstat(fd, &info) || !S_ISREG(info.st_mode))
	{
		::close(fd);
		error = QStringLiteral("Only regular files are supported.");
		return {};
	}
#endif
	if (!out->m_file.open(fd, writable ? QIODevice::ReadWrite : QIODevice::ReadOnly,
						  QFileDevice::AutoCloseHandle))
	{
#ifdef Q_OS_WIN
		::_close(fd);
#else
		::close(fd);
#endif
		error = QStringLiteral("Cannot attach file to the copy stream.");
		return {};
	}
	return out;
}

OpStamp OpFile::stamp() const
{
#ifdef Q_OS_WIN
	return nativeStamp(handle(m_file));
#else
	struct stat info{};
	return ::fstat(m_file.handle(), &info) == 0 ? nativeStamp(info) : OpStamp{};
#endif
}

OpStamp OpFile::inspect(const QString &path)
{
	QString error;
	auto file = open(path, false, error);
	return file ? file->stamp() : OpStamp{};
}
bool OpFile::stillAt(const QString &path, const OpStamp &expected) const
{
	return expected.unchanged(stamp()) && expected.unchanged(inspect(path));
}

NativeFile::SyncResult OpFile::sync()
{
	return NativeFile::syncFile(m_file);
}

#ifndef Q_OS_WIN
bool OpFile::preserveMetadataFrom(OpFile &source, QString &error)
{
#ifdef Q_OS_MAC
	// The native data transfer has finished. Copy resource forks, ACLs and
	// the remaining metadata separately before publication.
	if (::fcopyfile(source.m_file.handle(), m_file.handle(), nullptr, COPYFILE_METADATA) == 0)
		return true;
#else
	struct stat info{};
	if (::fstat(source.m_file.handle(), &info) == 0)
	{
		const timespec times[] = {info.st_atim, info.st_mtim};
		if (::fchmod(m_file.handle(), info.st_mode & 0777) == 0 &&
			::futimens(m_file.handle(), times) == 0)
			return true;
	}
#endif
	error = QStringLiteral("Could not preserve all file metadata: %1").arg(nativeError());
	return false;
}
#endif

bool OpFile::checkCopySupport(QString &error) const
{
#ifdef Q_OS_WIN
	BY_HANDLE_FILE_INFORMATION info{};
	if (!::GetFileInformationByHandle(handle(m_file), &info))
	{
		error = nativeError();
		return false;
	}
	if (info.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED)
	{
		error = QStringLiteral(
			"Encrypted Windows files require an encryption-aware copy. The source was retained.");
		return false;
	}
#else
	Q_UNUSED(error);
#endif
	return true;
}

OpFile::Relocation OpFile::relocate(const QString &from, const QString &to, QString &error)
{
	const OpStamp before = stamp();
	if (!safePath(from) || !safePath(to) || !before.unchanged(inspect(from)))
	{
		error = QStringLiteral("The file or its location changed before relocation.");
		return Relocation::Failed;
	}
#ifdef Q_OS_WIN
	if (!m_protected)
	{
		error = QStringLiteral("The file cannot be protected for relocation; it was retained.");
		return Relocation::Failed;
	}
	const QString native = QDir::toNativeSeparators(to);
	const DWORD bytes = DWORD(native.size() * sizeof(wchar_t));
	QByteArray storage(int(sizeof(FILE_RENAME_INFO) + bytes), '\0');
	auto *rename = reinterpret_cast<FILE_RENAME_INFO *>(storage.data());
	rename->ReplaceIfExists = FALSE;
	rename->RootDirectory = nullptr;
	rename->FileNameLength = bytes;
	std::memcpy(rename->FileName, native.utf16(), bytes);
	const bool ok =
		::SetFileInformationByHandle(handle(m_file), FileRenameInfo, rename, DWORD(storage.size()));
	const DWORD code = ::GetLastError();
	if (!ok)
	{
		error = nativeError();
		if (code == ERROR_ALREADY_EXISTS || code == ERROR_FILE_EXISTS)
			return Relocation::Exists;
		if (code == ERROR_NOT_SAME_DEVICE)
			return Relocation::CrossVolume;
		return Relocation::Failed;
	}
#elif defined(Q_OS_MAC)
	// Open the actual directories and bind rename to them. No QFile fallback.
	const int fromDir = ::open(QFile::encodeName(QFileInfo(from).absolutePath()).constData(),
							   O_SEARCH | O_NOFOLLOW | O_CLOEXEC);
	const int toDir = ::open(QFile::encodeName(QFileInfo(to).absolutePath()).constData(),
							 O_SEARCH | O_NOFOLLOW | O_CLOEXEC);
	if (fromDir < 0 || toDir < 0)
	{
		error = nativeError();
		if (fromDir >= 0)
			::close(fromDir);
		if (toDir >= 0)
			::close(toDir);
		return Relocation::Failed;
	}
	const int rc =
		::renameatx_np(fromDir, QFile::encodeName(QFileInfo(from).fileName()).constData(), toDir,
					   QFile::encodeName(QFileInfo(to).fileName()).constData(), RENAME_EXCL);
	const int code = errno;
	::close(fromDir);
	::close(toDir);
	if (rc != 0)
	{
		error = QString::fromLocal8Bit(std::strerror(code));
		return code == EEXIST  ? Relocation::Exists
			   : code == EXDEV ? Relocation::CrossVolume
							   : Relocation::Failed;
	}
#else
	error = QStringLiteral("Native no-overwrite relocation is not implemented on this platform.");
	return Relocation::Failed;
#endif
	m_path = to;
	if (!before.unchanged(inspect(to)))
	{
		error = QStringLiteral("The relocated file could not be confirmed at %1. Files have been "
							   "retained; recovery needs attention.")
					.arg(to);
		return Relocation::Failed;
	}
	return Relocation::Moved;
}

bool OpFile::removeProtected(QString &error)
{
#ifdef Q_OS_WIN
	if (m_protected && m_created)
	{
		FILE_DISPOSITION_INFO info{};
		info.DeleteFile = TRUE;
		if (::SetFileInformationByHandle(handle(m_file), FileDispositionInfo, &info, sizeof(info)))
		{
			m_file.close();
			return true;
		}
		error = nativeError();
		return false;
	}
#endif
	error = QStringLiteral("File retained at %1: protected removal is unavailable.").arg(m_path);
	return false;
}

bool OpFile::removePartial(const OpStamp &expectedFile, const OpStamp &expectedDirectory,
						   QString &error)
{
	const QFileInfo item(m_path);
	const QString parent = item.absolutePath();
	if (item.fileName() != QStringLiteral("payload.partial") ||
		!privateDirectoryName(parent, true) || !safePath(m_path) ||
		!expectedDirectory.valid() || !stillAt(m_path, expectedFile))
	{
		error = QStringLiteral("Partial cleanup requires its recorded isolated file and folder.");
		return false;
	}
#ifdef Q_OS_WIN
	const auto directory = openDirectory(parent);
	const bool sameDirectory = directory != INVALID_HANDLE_VALUE &&
		expectedDirectory.sameObject(nativeStamp(directory));
	if (directory != INVALID_HANDLE_VALUE)
		::CloseHandle(directory);
	if (!m_protected || !sameDirectory)
	{
		error = QStringLiteral("The partial file or its staging folder could not be protected.");
		return false;
	}
	FILE_DISPOSITION_INFO disposition{};
	disposition.DeleteFile = TRUE;
	if (!::SetFileInformationByHandle(handle(m_file), FileDispositionInfo, &disposition,
									 sizeof(disposition)))
	{
		error = nativeError();
		return false;
	}
	m_file.close();
#elif defined(Q_OS_MAC)
	const int directory = ::open(QFile::encodeName(parent).constData(),
		O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (directory < 0)
	{
		error = nativeError();
		return false;
	}
	struct stat directoryInfo{}, current{};
	const bool matches = privateDirectory(directory, directoryInfo) &&
		expectedDirectory.sameObject(nativeStamp(directoryInfo)) &&
		::fstatat(directory, "payload.partial", &current, AT_SYMLINK_NOFOLLOW) == 0 &&
		S_ISREG(current.st_mode) && expectedFile.unchanged(nativeStamp(current)) &&
		expectedFile.unchanged(stamp());
	if (!matches)
	{
		::close(directory);
		error = QStringLiteral("The staging folder or partial file changed; it was retained.");
		return false;
	}
	// The descriptor remains bound to the recorded, owner-only directory.
	// This has the same isolation boundary as retired-original removal: another
	// process deliberately modifying private files as this account is outside it.
	if (::unlinkat(directory, "payload.partial", 0) != 0)
	{
		error = nativeError();
		::close(directory);
		return false;
	}
	m_file.close();
	const bool synced = ::fsync(directory) == 0;
	if (!synced)
		error = QStringLiteral("Partial removed, but its staging folder could not be flushed: ") + nativeError();
	::close(directory);
	if (!synced)
		return false;
#else
	error = QStringLiteral("Protected partial removal is unavailable on this platform.");
	return false;
#endif
	if (occupied(m_path))
	{
		error = QStringLiteral("Partial cleanup is unconfirmed; the path remains occupied.");
		return false;
	}
#ifdef Q_OS_WIN
	return NativeFile::syncDirectory(parent, &error) == NativeFile::SyncResult::Ok;
#else
	return true;
#endif
}

bool OpFile::removeOriginal(QString &error)
{
	const QFileInfo item(m_path);
	const QFileInfo parent(item.absolutePath());
	const QString prefix = QStringLiteral(".mediamuster-retire-");
	const QString token = parent.fileName().mid(prefix.size());
	const auto before = stamp();
	if (item.fileName() != QStringLiteral("payload.retired") ||
		!parent.fileName().startsWith(prefix) || QUuid(token).isNull() ||
		!safePath(m_path) || !stillAt(m_path, before))
	{
		error = QStringLiteral("Original removal requires its recorded isolated retirement file.");
		return false;
	}
#ifdef Q_OS_WIN
	if (!m_protected)
	{
		error = QStringLiteral("The retired original could not be protected for removal.");
		return false;
	}
	FILE_DISPOSITION_INFO disposition{};
	disposition.DeleteFile = TRUE;
	if (!::SetFileInformationByHandle(handle(m_file), FileDispositionInfo, &disposition,
									  sizeof(disposition)))
	{
		error = nativeError();
		return false;
	}
	// Deletion was requested on this object, not on a pathname. Closing is part
	// of the action and must precede its journalled completion.
	m_file.close();
	if (occupied(m_path))
	{
		error = QStringLiteral("Original removal is pending; the retirement path remains occupied.");
		return false;
	}
#elif defined(Q_OS_MAC)
	const int directory = ::open(QFile::encodeName(parent.absoluteFilePath()).constData(),
								 O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	struct stat directoryInfo{}, current{};
	if (directory < 0)
	{
		error = nativeError();
		return false;
	}
	// Unlike an ordinary source directory, this private directory excludes
	// other users. Keep its descriptor bound through identity check and unlink.
	// An external writer using this same account is outside that isolation;
	// callers must not share or reuse retirement directories.
	const bool isolated = privateDirectory(directory, directoryInfo);
	const bool same = ::fstatat(directory, "payload.retired", &current, AT_SYMLINK_NOFOLLOW) == 0 &&
					  S_ISREG(current.st_mode) && hex(current.st_ino) == before.fileId &&
					  hex(current.st_dev) == before.volumeId && current.st_size == before.size &&
					  current.st_mtimespec.tv_sec * 1000000000LL + current.st_mtimespec.tv_nsec == before.modified;
	if (!isolated || !same)
	{
		::close(directory);
		error = QStringLiteral("The retirement directory or original identity is not protected.");
		return false;
	}
	if (::unlinkat(directory, "payload.retired", 0) != 0)
	{
		error = nativeError();
		::close(directory);
		return false;
	}
	m_file.close();
	const bool synced = ::fsync(directory) == 0;
	if (!synced)
		error = QStringLiteral("Original removed, but its retirement directory could not be flushed: ") + nativeError();
	::close(directory);
	return synced;
#else
	error = QStringLiteral("Protected original removal is unavailable on this platform.");
	return false;
#endif
#ifdef Q_OS_WIN
	return NativeFile::syncDirectory(parent.absoluteFilePath(), &error) == NativeFile::SyncResult::Ok;
#endif
}
