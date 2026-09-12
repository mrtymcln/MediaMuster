#include "opcopier.h"
#include "third_party/xxhash.h"
#include <QByteArray>
#include <QDir>
#include <exception>
#include <limits>
#include <cerrno>
#include <cstring>
#if defined(Q_OS_MAC)
#include <copyfile.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

namespace
{
constexpr qint64 kHashChunkSize = 4 * 1024 * 1024;
struct Hash
{
	XXH3_state_t *state = XXH3_createState();
	Hash()
	{
		if (state)
			XXH3_64bits_reset(state);
	}
	~Hash()
	{
		if (state)
			XXH3_freeState(state);
	}
	Hash(const Hash &) = delete;
	Hash &operator=(const Hash &) = delete;
	QString digest() const
	{
		return QStringLiteral("%1").arg(XXH3_64bits_digest(state), 16, 16, QLatin1Char('0'));
	}
};
#ifdef Q_OS_MAC
struct NativeCopyContext
{
	const std::atomic<bool> &cancel;
	const OpCopier::Progress &progress;
	qint64 size;
	std::exception_ptr exception;
};
int copyStatus(int what, int stage, copyfile_state_t state, const char *, const char *, void *raw)
{
	auto &context = *static_cast<NativeCopyContext *>(raw);
	if (context.cancel.load())
		return COPYFILE_QUIT;
	if (what == COPYFILE_COPY_DATA && stage == COPYFILE_PROGRESS)
	{
		off_t copied = 0;
		if (copyfile_state_get(state, COPYFILE_STATE_COPIED, &copied) == 0 && context.progress)
		{
			try { context.progress(copied, context.size, false); }
			catch (...) { context.exception = std::current_exception(); return COPYFILE_QUIT; }
		}
	}
	return context.cancel.load() ? COPYFILE_QUIT : COPYFILE_CONTINUE;
}
#elif defined(Q_OS_WIN)
OpStamp handleStamp(HANDLE file)
{
	BY_HANDLE_FILE_INFORMATION basic{};
	if (!::GetFileInformationByHandle(file, &basic))
		return {};
	OpStamp stamp;
	FILE_ID_INFO extended{};
	if (::GetFileInformationByHandleEx(file, FileIdInfo, &extended, sizeof(extended)))
	{
		stamp.fileId = QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(extended.FileId.Identifier), 16).toHex());
		stamp.volumeId = QString::number(extended.VolumeSerialNumber, 16);
	}
	else
	{
		stamp.fileId = QString::number((quint64(basic.nFileIndexHigh) << 32) | basic.nFileIndexLow, 16);
		stamp.volumeId = QString::number(basic.dwVolumeSerialNumber, 16);
	}
	stamp.size = qint64((quint64(basic.nFileSizeHigh) << 32) | basic.nFileSizeLow);
	stamp.modified = qint64((quint64(basic.ftLastWriteTime.dwHighDateTime) << 32) | basic.ftLastWriteTime.dwLowDateTime);
	return stamp;
}
struct NativeCopyContext
{
	const std::atomic<bool> &cancel;
	const OpCopier::Progress &progress;
	OpStamp expected;
	OpStamp destination;
	bool changed = false;
	std::exception_ptr exception;
	HANDLE destinationHandle = INVALID_HANDLE_VALUE;
	~NativeCopyContext()
	{
		if (destinationHandle != INVALID_HANDLE_VALUE) ::CloseHandle(destinationHandle);
	}
};
DWORD CALLBACK copyStatus(LARGE_INTEGER total, LARGE_INTEGER transferred, LARGE_INTEGER,
	LARGE_INTEGER, DWORD, DWORD, HANDLE source, HANDLE destination, LPVOID raw)
{
	auto &context = *static_cast<NativeCopyContext *>(raw);
	// Alternate-stream callbacks can report that stream's length. Bind every
	// stream to the expected file object; recheck main-stream size/time below.
	if (!context.expected.sameObject(handleStamp(source)))
	{
		context.changed = true;
		return PROGRESS_STOP;
	}
	const auto landed = handleStamp(destination);
	if (!landed.valid() || (context.destination.valid() && !context.destination.sameObject(landed)))
	{
		context.changed = true;
		return PROGRESS_STOP;
	}
	context.destination = landed;
	if (context.destinationHandle == INVALID_HANDLE_VALUE &&
		!::DuplicateHandle(::GetCurrentProcess(), destination, ::GetCurrentProcess(),
			&context.destinationHandle, 0, FALSE, DUPLICATE_SAME_ACCESS))
	{
		context.changed = true;
		return PROGRESS_STOP;
	}
	if (context.cancel.load())
		return PROGRESS_STOP;
	try
	{
		if (context.progress)
			context.progress(transferred.QuadPart, total.QuadPart, false);
	}
	catch (...) { context.exception = std::current_exception(); return PROGRESS_STOP; }
	return context.cancel.load() ? PROGRESS_STOP : PROGRESS_CONTINUE;
}
#endif
} // namespace
OpCopier::Result OpCopier::hash(OpFile &file, const std::atomic<bool> &cancel,
								const Progress &progress)
{
	Result out;
	Hash hash;
	const auto before = file.stamp();
	if (!hash.state || !before.valid() || !file.io().seek(0))
	{
		out.error = "Cannot start checksum readback.";
		return out;
	}
	QByteArray buffer(int(kHashChunkSize), Qt::Uninitialized);
	qint64 read = 0;
	while (read < before.size)
	{
		if (cancel.load())
		{
			out.outcome = Outcome::Cancelled;
			return out;
		}
		const auto n = file.io().read(buffer.data(), qMin(kHashChunkSize, before.size - read));
		if (n <= 0)
		{
			out.error = "Checksum readback failed before the complete file was read.";
			return out;
		}
		XXH3_64bits_update(hash.state, buffer.constData(), size_t(n));
		read += n;
		if (progress)
			progress(read, before.size, true);
	}
	if (cancel.load())
	{
		out.outcome = Outcome::Cancelled;
		return out;
	}
	if (!before.unchanged(file.stamp()))
	{
		out.error = "The destination changed during checksum readback.";
		return out;
	}
	out.hash = hash.digest();
	out.outcome = Outcome::Succeeded;
	return out;
}
bool OpCopier::isRetryableNativeError(int error)
{
#ifdef Q_OS_WIN
	return error == ERROR_BUSY || error == ERROR_NETWORK_BUSY || error == ERROR_NOT_READY ||
		error == ERROR_SEM_TIMEOUT || error == ERROR_RETRY ||
		error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION;
#elif defined(Q_OS_MAC)
	return error == EAGAIN || error == EINTR || error == ETIMEDOUT || error == EBUSY;
#else
	Q_UNUSED(error);
	return false;
#endif
}

OpCopier::Result OpCopier::copy(OpFile &source, OpFile &destination,
									const std::atomic<bool> &cancel, const Progress &progress,
									const std::function<void()> &beforeReadback, bool verify)
{
	Result out;
	const auto before = source.stamp();
	if (!source.checkCopySupport(out.error))
		return out;
	if (!before.valid() || !source.stillAt(source.path(), before) ||
		!destination.m_created || destination.stamp().size != 0 ||
		!destination.stillAt(destination.path(), destination.stamp()))
	{
		out.error = "Cannot start native copying with these source or staging identities.";
		return out;
	}
	if (cancel.load())
	{
		out.outcome = Outcome::Cancelled;
		return out;
	}
#ifdef Q_OS_MAC
	if (!source.io().seek(0) || !destination.io().seek(0))
	{
		out.error = "Cannot position the native copy handles.";
		return out;
	}
	copyfile_state_t state = ::copyfile_state_alloc();
	if (!state)
	{
		out.error = "Cannot allocate native copy state.";
		return out;
	}
	NativeCopyContext context{cancel, progress, before.size, {}};
	::copyfile_state_set(state, COPYFILE_STATE_STATUS_CB, reinterpret_cast<void *>(copyStatus));
	::copyfile_state_set(state, COPYFILE_STATE_STATUS_CTX, &context);
	const int copied = ::fcopyfile(source.io().handle(), destination.io().handle(), state, COPYFILE_DATA);
	const int copyError = errno;
	::copyfile_state_free(state);
	if (context.exception)
		std::rethrow_exception(context.exception);
	if (copied != 0)
	{
		out.outcome = cancel.load() ? Outcome::Cancelled : Outcome::Failed;
		out.error = QStringLiteral("Native copying failed: %1 (POSIX %2).")
			.arg(QString::fromLocal8Bit(std::strerror(copyError))).arg(copyError);
		out.retryable = out.outcome == Outcome::Failed && isRetryableNativeError(copyError);
		return out;
	}
#elif defined(Q_OS_WIN)
	// CopyFileEx opens its own paths. Dispose only of the exclusively created
	// empty placeholder, then request CREATE_NEW semantics from the native API.
	// The callback checks the actual source HANDLE, not just its pathname.
	if (!destination.removeProtected(out.error))
		return out;
	source.m_file.close();
	NativeCopyContext context{cancel, progress, before, {}, false, {}};
	const QString sourcePath = QDir::toNativeSeparators(source.path());
	const QString destinationPath = QDir::toNativeSeparators(destination.path());
	const BOOL copied = ::CopyFileExW(reinterpret_cast<LPCWSTR>(sourcePath.utf16()),
		reinterpret_cast<LPCWSTR>(destinationPath.utf16()), copyStatus, &context, nullptr,
		COPY_FILE_FAIL_IF_EXISTS);
	const DWORD copyError = ::GetLastError();
	// A copied read-only attribute must not prevent reopening our staging file
	// with the write/delete rights needed for flush and publication. Change it
	// only through the native-created object's retained HANDLE, then restore
	// it through the protected reopened HANDLE before any final publication.
	FILE_BASIC_INFO copiedAttributes{};
	bool restoreReadOnly = false;
	if (copied && context.destinationHandle != INVALID_HANDLE_VALUE &&
		::GetFileInformationByHandleEx(context.destinationHandle, FileBasicInfo,
			&copiedAttributes, sizeof(copiedAttributes)) &&
		(copiedAttributes.FileAttributes & FILE_ATTRIBUTE_READONLY))
	{
		auto writableAttributes = copiedAttributes;
		writableAttributes.FileAttributes &= ~FILE_ATTRIBUTE_READONLY;
		if (!writableAttributes.FileAttributes) writableAttributes.FileAttributes = FILE_ATTRIBUTE_NORMAL;
		restoreReadOnly = ::SetFileInformationByHandle(context.destinationHandle, FileBasicInfo,
			&writableAttributes, sizeof(writableAttributes)) != 0;
	}
	if (context.destinationHandle != INVALID_HANDLE_VALUE)
	{
		::CloseHandle(context.destinationHandle);
		context.destinationHandle = INVALID_HANDLE_VALUE;
	}
	auto attach = [](OpFile &target, std::unique_ptr<OpFile> opened)
	{
		if (!opened) return false;
		const int fd = ::_dup(opened->io().handle());
		if (fd < 0) return false;
		if (!target.m_file.open(fd, opened->io().openMode(), QFileDevice::AutoCloseHandle))
		{ ::_close(fd); return false; }
		target.m_protected = opened->m_protected;
		return true;
	};
	QString sourceError, destinationError;
	const bool sourceOpened = attach(source, OpFile::open(source.path(), false, sourceError));
	auto openedDestination = OpFile::openWritableExisting(destination.path(), destinationError);
	const bool ownedDestination = openedDestination && context.destination.valid() &&
		context.destination.sameObject(openedDestination->stamp());
	const bool destinationOpened = ownedDestination && attach(destination, std::move(openedDestination));
	const bool attributesRestored = !restoreReadOnly || (destinationOpened &&
		::SetFileInformationByHandle(reinterpret_cast<HANDLE>(::_get_osfhandle(destination.io().handle())),
			FileBasicInfo, &copiedAttributes, sizeof(copiedAttributes)) != 0);
	if (context.exception)
		std::rethrow_exception(context.exception);
	if (!copied || context.changed || !sourceOpened || !destinationOpened || !attributesRestored ||
		!context.destination.valid() || !context.destination.sameObject(destination.stamp()))
	{
		out.outcome = cancel.load() ? Outcome::Cancelled : Outcome::Failed;
		out.error = context.changed ? QStringLiteral("A native copying handle referred to a changed file.") :
			QStringLiteral("Native copying could not be completed and protected (Windows %1). %2 %3")
				.arg(copyError).arg(sourceError, destinationError);
		// GetLastError is meaningful only after CopyFileEx failed. A successful copy
		// followed by a protection/identity failure must never use a stale error to retry.
		bool destinationAbsent = false;
		if (!destinationOpened &&
			::GetFileAttributesW(reinterpret_cast<LPCWSTR>(destinationPath.utf16())) == INVALID_FILE_ATTRIBUTES)
		{
			const DWORD absenceError = ::GetLastError();
			destinationAbsent = absenceError == ERROR_FILE_NOT_FOUND || absenceError == ERROR_PATH_NOT_FOUND;
		}
		out.retryable = !copied && out.outcome == Outcome::Failed && !context.changed &&
			sourceOpened && source.stillAt(source.path(), before) && attributesRestored &&
			(destinationOpened || destinationAbsent) &&
			isRetryableNativeError(copyError);
		return out;
	}
#else
	out.error = "Native copying is unavailable on this platform.";
	return out;
#endif
	if (cancel.load())
	{
		out.outcome = Outcome::Cancelled;
		return out;
	}
	if (!source.stillAt(source.path(), before))
	{
		out.error = "The source changed during copying; it has been retained.";
		return out;
	}
	QString metadataError;
#ifdef Q_OS_WIN
	// CopyFileEx has already copied the streams and supported Windows metadata.
	// Do not replace that result with the old byte-copier's timestamp-only policy.
	out.metadataComplete = true;
#else
	out.metadataComplete = destination.preserveMetadataFrom(source, metadataError);
#endif
	const auto sync = destination.sync();
	if (sync == NativeFile::SyncResult::Failed)
	{
		out.error = "The destination could not confirm its writes.";
		return out;
	}
	out.durable = sync == NativeFile::SyncResult::Ok;
	if (destination.stamp().size != before.size)
	{
		out.error = "The destination length differs from the source.";
		return out;
	}
	if (verify)
	{
		auto verificationProgress = [&](qint64 bytes, qint64 size, bool destinationPass)
		{
			if (!progress) return;
			if (size <= (std::numeric_limits<qint64>::max)() / 2)
				progress((destinationPass ? size : 0) + bytes, size * 2, true);
			else
				progress((destinationPass ? size / 2 : 0) + bytes / 2, size, true);
		};
		const auto expected = OpCopier::hash(source, cancel,
			[&](qint64 bytes, qint64 size, bool) { verificationProgress(bytes, size, false); });
		if (expected.outcome != Outcome::Succeeded)
		{
			out.outcome = expected.outcome;
			out.error = expected.error;
			return out;
		}
		if (beforeReadback)
			beforeReadback();
		const auto actual = OpCopier::hash(destination, cancel,
			[&](qint64 bytes, qint64 size, bool) { verificationProgress(bytes, size, true); });
		if (actual.outcome != Outcome::Succeeded)
		{
			out.outcome = actual.outcome;
			out.error = actual.error;
			return out;
		}
		if (actual.hash != expected.hash)
		{
			out.error = "Checksum verification failed; the source has been retained.";
			return out;
		}
		out.hash = actual.hash;
	}
	if (!source.stillAt(source.path(), before))
	{
		out.error = "The source changed before verification finished; it has been retained.";
		return out;
	}
	out.error = metadataError;
	out.outcome = Outcome::Succeeded;
	return out;
}
