#include "optrash.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStorageInfo>
#ifdef Q_OS_MAC
#include <sys/mount.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <shlguid.h>
#include <shellapi.h>
#include <sherrors.h>
#include <cstring>
#endif

QString OpTrashPlatform::receipt(const QString &provider, const QString &path,
								 const OpStamp &landed, const QByteArray &nativeId)
{
	return QString::fromLatin1(QJsonDocument(QJsonObject{{"provider", provider}, {"path", path}, {"identity", landed.json()}, {"nativeId", QString::fromLatin1(nativeId.toBase64())}})
								   .toJson(QJsonDocument::Compact)
								   .toBase64(QByteArray::Base64UrlEncoding));
}

bool OpTrash::isNetwork(const QString &path)
{
	const QStorageInfo storage(path);
	const auto type = QString::fromLatin1(storage.fileSystemType()).toLower();
	if (type.contains("avid") || type.contains("nexis") || type.contains("isis"))
		return true;
#ifdef Q_OS_MAC
	struct statfs info{};
	if (::statfs(QFile::encodeName(path).constData(), &info) != 0)
		return true; // Unknown storage must not authorize a system-trash action.
	const auto nativeType = QByteArray(info.f_fstypename).toLower();
	return !(info.f_flags & MNT_LOCAL) || nativeType.contains("avid") ||
		   nativeType.contains("nexis") || nativeType.contains("isis");
#elif defined(Q_OS_WIN)
	const QString native = QDir::toNativeSeparators(path);
	if (native.startsWith(QStringLiteral("\\\\?\\UNC\\"), Qt::CaseInsensitive) ||
		(native.startsWith(QStringLiteral("\\\\")) && !native.startsWith(QStringLiteral("\\\\?\\"))))
		return true;
	wchar_t root[32768]{};
	if (!::GetVolumePathNameW(reinterpret_cast<LPCWSTR>(native.utf16()), root, 32768))
		return true;
	const UINT drive = ::GetDriveTypeW(root);
	return drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE && drive != DRIVE_RAMDISK;
#else
	Q_UNUSED(path);
	return true;
#endif
}

OpTrash::Result OpTrash::move(const QString &path, const OpStamp &expected,
							  const std::atomic<bool> &cancel)
{
	if (cancel.load())
		return {Outcome::Cancelled, {}, {}, "Cancelled before system Trash.", {}};
	if (!expected.valid() || !OpFile::safePath(path) || !expected.unchanged(OpFile::inspect(path)))
		return {Outcome::Failed, {}, {}, "The original changed before system Trash.", {}};
	if (isNetwork(path))
		return {Outcome::Unavailable, {}, {}, "Network storage uses MediaMuster Trash.", {}};
#if defined(Q_OS_MAC) || defined(Q_OS_WIN)
	return OpTrashPlatform::move(path, expected, cancel);
#else
	return {Outcome::Unavailable, {}, {}, "System Trash is unavailable on this platform.", {}};
#endif
}

OpTrash::Result OpTrash::restore(const QString &receipt, const QString &destination,
								 const OpStamp &expected, const std::atomic<bool> &cancel, const QString &resolvedSource)
{
	if (cancel.load())
		return {Outcome::Cancelled, {}, {}, "Cancelled before Trash restoration.", {}};
	const auto document = QJsonDocument::fromJson(QByteArray::fromBase64(receipt.toLatin1(),
																		 QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors));
	auto saved = document.object();
	const auto original = OpStamp::fromJson(saved["identity"].toObject());
	const bool sameReceipt = original.valid() && expected.fileId == original.fileId &&
							 expected.size == original.size && expected.modified == original.modified &&
							 (expected.volumeId == original.volumeId || !resolvedSource.isEmpty());
	if (!document.isObject() || !expected.valid() || !sameReceipt ||
		(!resolvedSource.isEmpty() && !OpFile::safePath(resolvedSource)) ||
		!OpFile::safePath(destination) || OpFile::occupied(destination))
		return {Outcome::Failed, {}, {}, "The Trash receipt or restore destination cannot be confirmed.", {}};
	// A qualified journal-volume resolution can change a mount path/device id.
	// Providers must validate this location against the current full stamp.
	if (!resolvedSource.isEmpty())
		saved["resolvedPath"] = resolvedSource;
#if defined(Q_OS_MAC) || defined(Q_OS_WIN)
	return OpTrashPlatform::restore(saved, destination, expected, cancel);
#else
	return {Outcome::Unavailable, {}, {}, "System Trash restoration is unavailable.", {}};
#endif
}

#ifdef Q_OS_WIN
namespace
{
	template <class T>
	struct ComPtr
	{
		T *value = nullptr;
		~ComPtr()
		{
			if (value)
				value->Release();
		}
		T **put() { return &value; }
		T *operator->() const { return value; }
	};
	struct ComApartment
	{
		HRESULT result = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		~ComApartment()
		{
			if (SUCCEEDED(result))
				::CoUninitialize();
		}
	};
	QString shellName(IShellItem *item, SIGDN kind)
	{
		PWSTR value = nullptr;
		if (!item || FAILED(item->GetDisplayName(kind, &value)))
			return {};
		const auto out = QString::fromWCharArray(value);
		::CoTaskMemFree(value);
		return QDir::fromNativeSeparators(out);
	}
	QByteArray shellId(IShellItem *item)
	{
		PIDLIST_ABSOLUTE id = nullptr;
		if (!item || FAILED(::SHGetIDListFromObject(item, &id)))
			return {};
		const auto out = QByteArray(reinterpret_cast<const char *>(id), int(::ILGetSize(id)));
		::CoTaskMemFree(id);
		return out;
	}
	bool validId(const QByteArray &id)
	{
		int offset = 0;
		while (offset + 2 <= id.size())
		{
			USHORT length = 0;
			std::memcpy(&length, id.constData() + offset, sizeof(length));
			if (!length)
				return offset + 2 == id.size();
			if (length < 2 || length > id.size() - offset)
				return false;
			offset += length;
		}
		return false;
	}
	QString nativeError(const QString &step, HRESULT status)
	{
		return QStringLiteral("%1 (Windows HRESULT 0x%2).")
			.arg(step).arg(quint32(status), 8, 16, QLatin1Char('0'));
	}
	bool sourceUnchanged(const QString &source, const OpStamp &expected)
	{
		return OpFile::safePath(source) && expected.unchanged(OpFile::inspect(source));
	}
	bool cancelled(HRESULT status)
	{
		return status == HRESULT_FROM_WIN32(ERROR_CANCELLED) ||
			status == HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED) ||
			status == COPYENGINE_E_USER_CANCELLED;
	}
	OpTrash::Result refused(const QString &source, const OpStamp &expected,
		const std::atomic<bool> &cancel, const QString &step, HRESULT status)
	{
		// This helper is only for a failure with no returned/moved item. A
		// native error alone never authorizes moving the source a second time.
		const auto outcome = !sourceUnchanged(source, expected) ? OpTrash::Outcome::Failed :
			(cancel.load() || cancelled(status)) ? OpTrash::Outcome::Cancelled : OpTrash::Outcome::Unavailable;
		return {outcome, {}, {}, nativeError(step, status), {}};
	}
	HRESULT recycleBinItem(IShellItem **item)
	{
		// The bin is a virtual folder; SHGetKnownFolderItem does not support
		// virtual known folders. Resolve its PIDL, including for old receipts.
		PIDLIST_ABSOLUTE id = nullptr;
		HRESULT status = ::SHGetKnownFolderIDList(FOLDERID_RecycleBinFolder, KF_FLAG_DEFAULT, nullptr, &id);
		if (SUCCEEDED(status))
			status = ::SHCreateItemFromIDList(id, IID_PPV_ARGS(item));
		::CoTaskMemFree(id);
		return status;
	}
	class RecycleAdvice final : public ITransferAdviseSink
	{
		LONG references = 1;
		const std::atomic<bool> &cancel;
		HRESULT check() const { return cancel.load() ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : S_OK; }
		HRESULT refuse(HRESULT error)
		{
			lastError = FAILED(error) ? error : E_ABORT;
			return FAILED(check()) ? check() : lastError;
		}
	public:
		HRESULT lastError = S_OK;
		explicit RecycleAdvice(const std::atomic<bool> &flag) : cancel(flag) {}
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override
		{
			if (!out) return E_POINTER;
			*out = nullptr;
			if (id != IID_IUnknown && id != IID_ITransferAdviseSink) return E_NOINTERFACE;
			*out = static_cast<ITransferAdviseSink *>(this);
			AddRef();
			return S_OK;
		}
		ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(::InterlockedIncrement(&references)); }
		ULONG STDMETHODCALLTYPE Release() override
		{
			const LONG count = ::InterlockedDecrement(&references);
			if (!count) delete this;
			return ULONG(count);
		}
		HRESULT STDMETHODCALLTYPE UpdateProgress(ULONGLONG, ULONGLONG, int, int, int, int) override { return check(); }
		HRESULT STDMETHODCALLTYPE UpdateTransferState(TRANSFER_ADVISE_STATE) override { return check(); }
		HRESULT STDMETHODCALLTYPE ConfirmOverwrite(IShellItem *, IShellItem *, LPCWSTR) override { return refuse(E_ABORT); }
		HRESULT STDMETHODCALLTYPE ConfirmEncryptionLoss(IShellItem *) override { return refuse(E_ABORT); }
		HRESULT STDMETHODCALLTYPE FileFailure(IShellItem *, LPCWSTR, HRESULT error, LPWSTR, ULONG) override { return refuse(error); }
		HRESULT STDMETHODCALLTYPE SubStreamFailure(IShellItem *, LPCWSTR, HRESULT error) override { return refuse(error); }
		HRESULT STDMETHODCALLTYPE PropertyFailure(IShellItem *, const PROPERTYKEY *, HRESULT error) override { return refuse(error); }
	};
	class TrashSink final : public IFileOperationProgressSink
	{
		LONG references = 1;

	public:
		const std::atomic<bool> &cancel;
		OpStamp expected;
		QString source, destination;
		OpTrash::Result result;
		TrashSink(const std::atomic<bool> &flag, const OpStamp &stamp, const QString &src,
				  const QString &dst) : cancel(flag), expected(stamp), source(src), destination(dst) {}
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override
		{
			if (!out)
				return E_POINTER;
			*out = nullptr;
			if (id == IID_IUnknown || id == IID_IFileOperationProgressSink)
			{
				*out = static_cast<IFileOperationProgressSink *>(this);
				AddRef();
				return S_OK;
			}
			return E_NOINTERFACE;
		}
		ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(::InterlockedIncrement(&references)); }
		ULONG STDMETHODCALLTYPE Release() override
		{
			const LONG count = ::InterlockedDecrement(&references);
			if (!count)
				delete this;
			return ULONG(count);
		}
		HRESULT check() const { return cancel.load() ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : S_OK; }
		HRESULT STDMETHODCALLTYPE StartOperations() override { return check(); }
		HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT) override { return S_OK; }
		HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD, IShellItem *, LPCWSTR) override { return E_ABORT; }
		HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD, IShellItem *, LPCWSTR, HRESULT, IShellItem *) override { return E_ABORT; }
		HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD flags, IShellItem *item, IShellItem *, LPCWSTR) override
		{
			if (FAILED(check()) || OpFile::occupied(destination) ||
				(flags & (TSF_OVERWRITE_EXIST | TSF_MOVE_AS_COPY_DELETE)))
				return E_ABORT;
			const auto physical = shellName(item, SIGDN_FILESYSPATH);
			return expected.unchanged(OpFile::inspect(physical)) ? S_OK : E_ABORT;
		}
		HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD, IShellItem *, IShellItem *, LPCWSTR,
											   HRESULT status, IShellItem *created) override { return capture(status, created); }
		HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD, IShellItem *, IShellItem *, LPCWSTR) override { return E_ABORT; }
		HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD, IShellItem *, IShellItem *, LPCWSTR, HRESULT, IShellItem *) override { return E_ABORT; }
		HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD, IShellItem *) override { return E_ABORT; }
		HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD, IShellItem *, HRESULT, IShellItem *) override { return E_ABORT; }
		HRESULT STDMETHODCALLTYPE PreNewItem(DWORD, IShellItem *, LPCWSTR) override { return E_ABORT; }
		HRESULT STDMETHODCALLTYPE PostNewItem(DWORD, IShellItem *, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem *) override { return E_ABORT; }
		HRESULT STDMETHODCALLTYPE UpdateProgress(UINT, UINT) override { return check(); }
		HRESULT STDMETHODCALLTYPE ResetTimer() override { return S_OK; }
		HRESULT STDMETHODCALLTYPE PauseTimer() override { return S_OK; }
		HRESULT STDMETHODCALLTYPE ResumeTimer() override { return S_OK; }
		HRESULT capture(HRESULT status, IShellItem *created)
		{
			if (FAILED(status) || !created)
			{
				result.error = QStringLiteral("The Shell operation has no confirmed recoverable result (HRESULT %1).")
								   .arg(quint32(status), 8, 16, QLatin1Char('0'));
				return E_ABORT;
			}
			result.path = shellName(created, SIGDN_FILESYSPATH);
			result.landed = OpFile::inspect(result.path);
			if (!expected.unchanged(result.landed) || OpFile::occupied(source) ||
				result.path.compare(destination, Qt::CaseInsensitive) != 0)
			{
				result.error = "The Shell result needs identity or location reconciliation.";
				return E_ABORT;
			}
			QString error;
			if (NativeFile::syncDirectory(QFileInfo(source).absolutePath(), &error) != NativeFile::SyncResult::Ok ||
				NativeFile::syncDirectory(QFileInfo(result.path).absolutePath(), &error) != NativeFile::SyncResult::Ok)
			{
				result.error = "The Shell operation completed, but directory persistence needs confirmation. " + error;
				return E_ABORT;
			}
			result.outcome = OpTrash::Outcome::Succeeded;
			return S_OK;
		}
	};
	OpTrash::Result restoreWithShell(IShellItem *item, const QString &source, const QString &destination,
							 const OpStamp &expected, const std::atomic<bool> &cancel)
	{
		ComPtr<IFileOperation> operation;
		HRESULT status = ::CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER,
											IID_PPV_ARGS(operation.put()));
		if (FAILED(status))
			return {OpTrash::Outcome::Unavailable, {}, {}, "Windows Shell operations are unavailable.", {}};
		// This general Shell API is used only to restore existing bin receipts.
		// A late conflict may fail reconciliation, but must never overwrite.
		const DWORD flags = FOF_SILENT | FOF_NOERRORUI | FOFX_EARLYFAILURE |
			FOF_NO_CONNECTED_ELEMENTS | FOF_RENAMEONCOLLISION;
		status = operation->SetOperationFlags(flags);
		ComPtr<TrashSink> sink;
		sink.value = new TrashSink(cancel, expected, source, destination);
		ComPtr<IShellItem> folder;
		if (SUCCEEDED(status))
		{
			const QFileInfo target(destination);
			const auto parent = QDir::toNativeSeparators(target.absolutePath());
			status = ::SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(parent.utf16()), nullptr,
												   IID_PPV_ARGS(folder.put()));
			if (SUCCEEDED(status))
				status = operation->MoveItem(item, folder.value,
											 reinterpret_cast<LPCWSTR>(target.fileName().utf16()), sink.value);
		}
		if (SUCCEEDED(status))
			status = operation->PerformOperations();
		BOOL aborted = FALSE;
		operation->GetAnyOperationsAborted(&aborted);
		auto result = sink->result;
		if (result.outcome == OpTrash::Outcome::Succeeded)
			return result; // A late Cancel cannot undo an already completed item.
		if (cancel.load())
			result.outcome = OpTrash::Outcome::Cancelled;
		if (result.error.isEmpty())
			result.error = QStringLiteral("System Trash operation failed (HRESULT %1%2).")
							   .arg(quint32(status), 8, 16, QLatin1Char('0'))
							   .arg(aborted ? "; aborted" : "");
		return result;
	}
}

OpTrash::Result OpTrashPlatform::move(const QString &path, const OpStamp &expected,
									  const std::atomic<bool> &cancel)
{
	ComApartment apartment;
	if (FAILED(apartment.result))
		return refused(path, expected, cancel, "The system bin could not initialize its Shell thread", apartment.result);
	ComPtr<IShellItem> item, parent, bin, created;
	const auto native = QDir::toNativeSeparators(path);
	HRESULT status = ::SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(native.utf16()), nullptr,
		IID_PPV_ARGS(item.put()));
	if (FAILED(status))
		return refused(path, expected, cancel, "The system bin could not identify the source", status);
	status = item->GetParent(parent.put());
	if (FAILED(status))
		return refused(path, expected, cancel, "The system bin could not identify the source folder", status);
	ComPtr<ITransferSource> transfer;
	status = parent->BindToHandler(nullptr, BHID_Transfer, IID_PPV_ARGS(transfer.put()));
	if (FAILED(status))
		return refused(path, expected, cancel, "This folder does not provide native recycling", status);
	ComPtr<RecycleAdvice> advice;
	advice.value = new RecycleAdvice(cancel);
	DWORD cookie = 0;
	status = transfer->Advise(advice.value, &cookie);
	if (FAILED(status))
		return refused(path, expected, cancel, "The system bin could not install its failure handler", status);
	struct Unadvise
	{
		ITransferSource *transfer;
		DWORD cookie;
		~Unadvise() { transfer->Unadvise(cookie); }
	} unadvise{transfer.value, cookie};
	status = recycleBinItem(bin.put());
	if (FAILED(status))
		return refused(path, expected, cancel, "The system bin is unavailable", status);
	if (cancel.load())
		return refused(path, expected, cancel, "Cancelled before system bin recycling", HRESULT_FROM_WIN32(ERROR_CANCELLED));
	if (!sourceUnchanged(path, expected) ||
		shellName(item.value, SIGDN_FILESYSPATH).compare(path, Qt::CaseInsensitive) != 0)
		return {OpTrash::Outcome::Failed, {}, {}, "The source changed before system bin recycling.", {}};

	// RecycleItem is a dedicated recycle operation. The general DeleteItem API
	// can offer permanent deletion when a bin rejects the file, even with its
	// recycle flags enabled. Never invoke that API or approve a retry here.
	status = transfer->RecycleItem(item.value, bin.value, TSF_NORMAL, created.put());
	OpTrash::Result result;
	QByteArray id;
	if (created.value)
	{
		result.path = shellName(created.value, SIGDN_FILESYSPATH);
		result.landed = OpFile::inspect(result.path);
		id = shellId(created.value);
		result.receipt = receipt("windows", result.path, result.landed, id);
	}
	if (FAILED(status))
	{
		const QString detail = nativeError("The system bin could not recycle this file", status) +
			(FAILED(advice->lastError) && advice->lastError != status ?
				" " + nativeError("Native failure detail", advice->lastError) : QString{});
		if (!created.value)
		{
			result = refused(path, expected, cancel, "The system bin could not recycle this file", status);
			if (result.outcome == OpTrash::Outcome::Unavailable && cancelled(advice->lastError))
				result.outcome = OpTrash::Outcome::Cancelled;
		}
		result.error = detail;
		return result; // Keep any returned location/receipt for reconciliation.
	}
	// A successful HRESULT can also mean "ignored" or "pending". Only a
	// confirmed, recoverable landed file counts as a completed recycling.
	if (!created.value || !OpFile::safePath(result.path) || !expected.unchanged(result.landed) ||
		OpFile::occupied(path) || !validId(id))
	{
		result.error = nativeError("The system bin result needs identity or location reconciliation", status);
		return result;
	}
	QString syncError;
	if (NativeFile::syncDirectory(QFileInfo(path).absolutePath(), &syncError) != NativeFile::SyncResult::Ok ||
		NativeFile::syncDirectory(QFileInfo(result.path).absolutePath(), &syncError) != NativeFile::SyncResult::Ok)
	{
		result.error = "File is in the system bin, but directory persistence needs confirmation. " + syncError;
		return result;
	}
	result.outcome = OpTrash::Outcome::Succeeded;
	return result; // A late cancellation does not undo a completed recycling.
}

OpTrash::Result OpTrashPlatform::restore(const QJsonObject &saved, const QString &destination,
										 const OpStamp &expected, const std::atomic<bool> &cancel)
{
	if (saved["provider"].toString() != "windows")
		return {OpTrash::Outcome::Failed, {}, {}, "This is not a Windows Trash receipt.", {}};
	const auto id = QByteArray::fromBase64(saved["nativeId"].toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
	if (!validId(id))
		return {OpTrash::Outcome::Failed, {}, {}, "The Shell Trash identifier is invalid.", {}};
	ComApartment apartment;
	if (FAILED(apartment.result))
		return {OpTrash::Outcome::Unavailable, {}, {}, "A Shell STA thread could not be initialized.", {}};
	ComPtr<IShellItem> item;
	::SHCreateItemFromIDList(reinterpret_cast<PCIDLIST_ABSOLUTE>(id.constData()), IID_PPV_ARGS(item.put()));
	QString physical = shellName(item.value, SIGDN_FILESYSPATH);
	const QString resolved = saved["resolvedPath"].toString();
	if ((!item.value || !expected.unchanged(OpFile::inspect(physical)) ||
		 (!resolved.isEmpty() && physical.compare(resolved, Qt::CaseInsensitive) != 0)) &&
		!resolved.isEmpty())
	{
		if (item.value)
		{
			item.value->Release();
			item.value = nullptr;
		}
		// Resolve through the Recycle Bin namespace, not a raw $R pathname:
		// moving the latter could strand the Shell's companion restore metadata.
		ComPtr<IShellItem> recycleBin;
		ComPtr<IEnumShellItems> items;
		if (SUCCEEDED(recycleBinItem(recycleBin.put())) &&
			SUCCEEDED(recycleBin->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(items.put()))))
		{
			for (;;)
			{
				if (cancel.load())
					return {OpTrash::Outcome::Cancelled, {}, {}, "Cancelled while locating the Trash item.", {}};
				ComPtr<IShellItem> candidate;
				ULONG fetched = 0;
				if (items->Next(1, candidate.put(), &fetched) != S_OK || !fetched)
					break;
				const auto path = shellName(candidate.value, SIGDN_FILESYSPATH);
				if (path.compare(resolved, Qt::CaseInsensitive) != 0 ||
					!expected.unchanged(OpFile::inspect(path)))
					continue;
				if (item.value)
					return {OpTrash::Outcome::Failed, {}, {}, "More than one Trash item matches the saved identity.", {}};
				item.value = candidate.value;
				candidate.value = nullptr;
				physical = path;
			}
		}
	}
	if (!item.value || !expected.unchanged(OpFile::inspect(physical)))
		return {OpTrash::Outcome::Failed, {}, {}, "The recorded Recycle Bin item changed or is missing.", {}};
	const QStorageInfo from(physical), to(QFileInfo(destination).absolutePath());
	if (OpTrash::isNetwork(QFileInfo(destination).absolutePath()) || !from.isValid() || !to.isValid() ||
		from.device().isEmpty() || from.device() != to.device())
		return {OpTrash::Outcome::Failed, {}, {}, "Trash restoration requires its original local filesystem.", {}};
	return restoreWithShell(item.value, physical, destination, expected, cancel);
}
#endif
