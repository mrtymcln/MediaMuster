#include "opfile.h"

#include <QDir>
#include <QFileInfo>
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
#endif
#endif

namespace
{
QString hex(quint64 value)
{
	return QString::number(value, 16);
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
#else
QString nativeError()
{
	return QString::fromLocal8Bit(std::strerror(errno));
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

bool OpFile::makeDirectory(const QString &path, QString &error)
{
	if (!safePath(path))
	{
		error = QStringLiteral("A path contains a symbolic link or unsupported reparse point: %1")
					.arg(path);
		return false;
	}
	if (QFileInfo(path).isDir())
		return true;
	const QString parent = QFileInfo(path).absolutePath();
	if (parent == path || !makeDirectory(parent, error))
		return false;
	if (!QDir().mkdir(path) && !QFileInfo(path).isDir())
	{
		error = QStringLiteral("Cannot create folder %1").arg(path);
		return false;
	}
	if (!NativeFile::syncDirectory(parent))
	{
		error = QStringLiteral("Cannot confirm the new folder was recorded: %1").arg(path);
		return false;
	}
	return true;
}

std::unique_ptr<OpFile> OpFile::open(const QString &path, bool create, QString &error)
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
	const DWORD access = GENERIC_READ | DELETE | (create ? GENERIC_WRITE : 0);
	HANDLE h = ::CreateFileW(reinterpret_cast<const wchar_t *>(native.utf16()), access,
							 FILE_SHARE_READ, nullptr, create ? CREATE_NEW : OPEN_EXISTING,
							 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	out->m_protected = h != INVALID_HANDLE_VALUE;
	if (h == INVALID_HANDLE_VALUE && !create)
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
						   _O_BINARY | (create ? _O_RDWR : _O_RDONLY));
	if (fd < 0)
	{
		::CloseHandle(h);
		error = QStringLiteral("Cannot attach file handle.");
		return {};
	}
#else
	fd = ::open(QFile::encodeName(path).constData(),
				(create ? O_RDWR | O_CREAT | O_EXCL : O_RDONLY) | O_NOFOLLOW | O_CLOEXEC, 0600);
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
	if (!out->m_file.open(fd, create ? QIODevice::ReadWrite : QIODevice::ReadOnly,
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
	OpStamp out;
#ifdef Q_OS_WIN
	BY_HANDLE_FILE_INFORMATION info{};
	if (!::GetFileInformationByHandle(handle(m_file), &info))
		return out;
	FILE_ID_INFO extended{};
	if (::GetFileInformationByHandleEx(handle(m_file), FileIdInfo, &extended, sizeof(extended)))
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
#else
	struct stat info{};
	if (::fstat(m_file.handle(), &info))
		return out;
	out.fileId = hex(info.st_ino);
	out.volumeId = hex(info.st_dev);
	out.size = info.st_size;
#ifdef Q_OS_MAC
	out.modified = info.st_mtimespec.tv_sec * 1000000000LL + info.st_mtimespec.tv_nsec;
#else
	out.modified = info.st_mtim.tv_sec * 1000000000LL + info.st_mtim.tv_nsec;
#endif
#endif
	return out;
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
	return NativeFile::syncFile(m_file, NativeFile::Durability::Platter);
}

bool OpFile::preserveMetadataFrom(OpFile &source, QString &error)
{
#ifdef Q_OS_WIN
	FILETIME created{}, accessed{}, modified{};
	if (::GetFileTime(handle(source.m_file), &created, &accessed, &modified) &&
		::SetFileTime(handle(m_file), &created, &accessed, &modified))
	{
		error = QStringLiteral("File bytes and timestamps copied. Windows security and "
							   "alternate-stream metadata are not copied; the source is retained.");
		return false;
	}
#elif defined(Q_OS_MAC)
	// Metadata only: the data fork still uses our single copy/hash loop.
	// This also preserves resource forks and ACLs where supported.
	if (::fcopyfile(source.m_file.handle(), m_file.handle(), nullptr, COPYFILE_METADATA) == 0)
		return true;
#else
	struct stat info{};
	if (::fstat(source.m_file.handle(), &info) == 0)
	{
#ifdef Q_OS_MAC
		const timespec times[] = {info.st_atimespec, info.st_mtimespec};
#else
		const timespec times[] = {info.st_atim, info.st_mtim};
#endif
		if (::fchmod(m_file.handle(), info.st_mode & 0777) == 0 &&
			::futimens(m_file.handle(), times) == 0)
			return true;
	}
#endif
	error = QStringLiteral("Could not preserve all file metadata: %1").arg(nativeError());
	return false;
}

bool OpFile::canStreamCopy(QString &error) const
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
							   O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	const int toDir = ::open(QFile::encodeName(QFileInfo(to).absolutePath()).constData(),
							 O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
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
