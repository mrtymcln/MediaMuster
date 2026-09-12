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
#include <cstring>
#endif

QString OpTrashPlatform::receipt(const QString &provider, const QString &path,
	const OpStamp &landed, const QByteArray &nativeId)
{
	return QString::fromLatin1(QJsonDocument(QJsonObject{{"provider", provider}, {"path", path},
		{"identity", landed.json()}, {"nativeId", QString::fromLatin1(nativeId.toBase64())}})
		.toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding));
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
	if (!resolvedSource.isEmpty()) saved["resolvedPath"] = resolvedSource;
#if defined(Q_OS_MAC) || defined(Q_OS_WIN)
	return OpTrashPlatform::restore(saved, destination, expected, cancel);
#else
	return {Outcome::Unavailable, {}, {}, "System Trash restoration is unavailable.", {}};
#endif
}

#ifdef Q_OS_WIN
namespace
{
template<class T> struct ComPtr
{
	T *value = nullptr;
	~ComPtr() { if (value) value->Release(); }
	T **put() { return &value; }
	T *operator->() const { return value; }
};
struct ComApartment
{
	HRESULT result = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	~ComApartment() { if (SUCCEEDED(result)) ::CoUninitialize(); }
};
QString shellName(IShellItem *item, SIGDN kind)
{
	PWSTR value = nullptr;
	if (!item || FAILED(item->GetDisplayName(kind, &value))) return {};
	const auto out = QString::fromWCharArray(value);
	::CoTaskMemFree(value);
	return QDir::fromNativeSeparators(out);
}
QByteArray shellId(IShellItem *item)
{
	PIDLIST_ABSOLUTE id = nullptr;
	if (!item || FAILED(::SHGetIDListFromObject(item, &id))) return {};
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
		if (!length) return offset + 2 == id.size();
		if (length < 2 || length > id.size() - offset) return false;
		offset += length;
	}
	return false;
}
class TrashSink final : public IFileOperationProgressSink
{
	LONG references = 1;
  public:
	const std::atomic<bool> &cancel;
	OpStamp expected;
	QString source, destination;
	bool deleting;
	bool rejectedPermanent = false;
	OpTrash::Result result;
	TrashSink(const std::atomic<bool> &flag, const OpStamp &stamp, const QString &src,
		const QString &dst, bool remove) : cancel(flag), expected(stamp), source(src), destination(dst), deleting(remove) {}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override
	{
		if (!out) return E_POINTER;
		*out = nullptr;
		if (id == IID_IUnknown || id == IID_IFileOperationProgressSink)
		{ *out = static_cast<IFileOperationProgressSink *>(this); AddRef(); return S_OK; }
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(::InterlockedIncrement(&references)); }
	ULONG STDMETHODCALLTYPE Release() override
	{ const LONG count = ::InterlockedDecrement(&references); if (!count) delete this; return ULONG(count); }
	HRESULT check() const { return cancel.load() ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : S_OK; }
	HRESULT STDMETHODCALLTYPE StartOperations() override { return check(); }
	HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD, IShellItem *, LPCWSTR) override { return E_ABORT; }
	HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD, IShellItem *, LPCWSTR, HRESULT, IShellItem *) override { return E_ABORT; }
	HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD flags, IShellItem *item, IShellItem *, LPCWSTR) override
	{
		if (deleting || FAILED(check()) || OpFile::occupied(destination) ||
			(flags & (TSF_OVERWRITE_EXIST | TSF_MOVE_AS_COPY_DELETE))) return E_ABORT;
		const auto physical = shellName(item, SIGDN_FILESYSPATH);
		return expected.unchanged(OpFile::inspect(physical)) ? S_OK : E_ABORT;
	}
	HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD, IShellItem *, IShellItem *, LPCWSTR,
		HRESULT status, IShellItem *created) override { return capture(status, created); }
	HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD, IShellItem *, IShellItem *, LPCWSTR) override { return E_ABORT; }
	HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD, IShellItem *, IShellItem *, LPCWSTR, HRESULT, IShellItem *) override { return E_ABORT; }
	HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD flags, IShellItem *item) override
	{
		if (!(flags & TSF_DELETE_RECYCLE_IF_POSSIBLE))
		{ rejectedPermanent = true; return E_ABORT; }
		if (!deleting || FAILED(check())) return E_ABORT;
		return expected.unchanged(OpFile::inspect(shellName(item, SIGDN_FILESYSPATH))) ? S_OK : E_ABORT;
	}
	HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD, IShellItem *, HRESULT status, IShellItem *created) override
	{ return capture(status, created); }
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
		const auto id = deleting ? shellId(created) : QByteArray{};
		if (deleting)
			result.receipt = OpTrashPlatform::receipt("windows", result.path, result.landed, id);
		if (!expected.unchanged(result.landed) || OpFile::occupied(source) ||
			(deleting && !validId(id)) ||
			(!deleting && result.path.compare(destination, Qt::CaseInsensitive) != 0))
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
OpTrash::Result runShell(IShellItem *item, const QString &source, const QString &destination,
	const OpStamp &expected, const std::atomic<bool> &cancel, bool deleting)
{
	ComPtr<IFileOperation> operation;
	HRESULT status = ::CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(operation.put()));
	if (FAILED(status)) return {OpTrash::Outcome::Unavailable, {}, {}, "Windows Shell operations are unavailable.", {}};
	// FOFX_RECYCLEONDELETE is the Windows 8+ API contract to send the item to
	// Recycle Bin rather than permanently delete it. PreDeleteItem also refuses
	// a Shell operation that drops recycle mode; PostDeleteItem requires the
	// actual recoverable item. TSF's "if possible" flag alone is not proof of
	// recycling. Disabled/full/oversize-bin behavior needs Windows field tests.
	// Never answer Yes to All and never provide a permanent-delete fallback.
	DWORD flags = FOF_SILENT | FOF_NOERRORUI | FOFX_EARLYFAILURE | FOF_NO_CONNECTED_ELEMENTS;
	if (deleting) flags |= FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE | FOF_WANTNUKEWARNING;
	else flags |= FOF_RENAMEONCOLLISION; // A late conflict may fail reconciliation, never overwrite.
	status = operation->SetOperationFlags(flags);
	ComPtr<TrashSink> sink;
	sink.value = new TrashSink(cancel, expected, source, destination, deleting);
	ComPtr<IShellItem> folder;
	if (SUCCEEDED(status) && deleting)
		status = operation->DeleteItem(item, sink.value);
	else if (SUCCEEDED(status))
	{
		const QFileInfo target(destination);
		const auto parent = QDir::toNativeSeparators(target.absolutePath());
		status = ::SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(parent.utf16()), nullptr,
			IID_PPV_ARGS(folder.put()));
		if (SUCCEEDED(status)) status = operation->MoveItem(item, folder.value,
			reinterpret_cast<LPCWSTR>(target.fileName().utf16()), sink.value);
	}
	if (SUCCEEDED(status)) status = operation->PerformOperations();
	BOOL aborted = FALSE;
	operation->GetAnyOperationsAborted(&aborted);
	auto result = sink->result;
	if (result.outcome == OpTrash::Outcome::Succeeded)
		return result; // A late Cancel cannot undo an already completed item.
	if (cancel.load()) result.outcome = OpTrash::Outcome::Cancelled;
	else if (sink->rejectedPermanent && expected.unchanged(OpFile::inspect(source)))
		result.outcome = OpTrash::Outcome::Unavailable;
	if (result.error.isEmpty()) result.error = QStringLiteral("System Trash operation failed (HRESULT %1%2).")
		.arg(quint32(status), 8, 16, QLatin1Char('0')).arg(aborted ? "; aborted" : "");
	return result;
}
}

OpTrash::Result OpTrashPlatform::move(const QString &path, const OpStamp &expected,
	const std::atomic<bool> &cancel)
{
	ComApartment apartment;
	if (FAILED(apartment.result)) return {OpTrash::Outcome::Unavailable, {}, {}, "A Shell STA thread could not be initialized.", {}};
	ComPtr<IShellItem> item;
	const auto native = QDir::toNativeSeparators(path);
	if (FAILED(::SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(native.utf16()), nullptr,
		IID_PPV_ARGS(item.put()))))
		return {OpTrash::Outcome::Failed, {}, {}, "Cannot identify the system Trash source.", {}};
	return runShell(item.value, path, {}, expected, cancel, true);
}

OpTrash::Result OpTrashPlatform::restore(const QJsonObject &saved, const QString &destination,
	const OpStamp &expected, const std::atomic<bool> &cancel)
{
	if (saved["provider"].toString() != "windows")
		return {OpTrash::Outcome::Failed, {}, {}, "This is not a Windows Trash receipt.", {}};
	const auto id = QByteArray::fromBase64(saved["nativeId"].toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
	if (!validId(id)) return {OpTrash::Outcome::Failed, {}, {}, "The Shell Trash identifier is invalid.", {}};
	ComApartment apartment;
	if (FAILED(apartment.result)) return {OpTrash::Outcome::Unavailable, {}, {}, "A Shell STA thread could not be initialized.", {}};
	ComPtr<IShellItem> item;
	::SHCreateItemFromIDList(reinterpret_cast<PCIDLIST_ABSOLUTE>(id.constData()), IID_PPV_ARGS(item.put()));
	QString physical = shellName(item.value, SIGDN_FILESYSPATH);
	const QString resolved = saved["resolvedPath"].toString();
	if ((!item.value || !expected.unchanged(OpFile::inspect(physical)) ||
		(!resolved.isEmpty() && physical.compare(resolved, Qt::CaseInsensitive) != 0)) && !resolved.isEmpty())
	{
		if (item.value) { item.value->Release(); item.value = nullptr; }
		// Resolve through the Recycle Bin namespace, not a raw $R pathname:
		// moving the latter could strand the Shell's companion restore metadata.
		ComPtr<IShellItem> recycleBin;
		ComPtr<IEnumShellItems> items;
		if (SUCCEEDED(::SHGetKnownFolderItem(FOLDERID_RecycleBinFolder, KF_FLAG_DEFAULT, nullptr,
			IID_PPV_ARGS(recycleBin.put()))) &&
			SUCCEEDED(recycleBin->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(items.put()))))
		{
			for (;;)
			{
				if (cancel.load()) return {OpTrash::Outcome::Cancelled, {}, {}, "Cancelled while locating the Trash item.", {}};
				ComPtr<IShellItem> candidate;
				ULONG fetched = 0;
				if (items->Next(1, candidate.put(), &fetched) != S_OK || !fetched) break;
				const auto path = shellName(candidate.value, SIGDN_FILESYSPATH);
				if (path.compare(resolved, Qt::CaseInsensitive) != 0 ||
					!expected.unchanged(OpFile::inspect(path))) continue;
				if (item.value) return {OpTrash::Outcome::Failed, {}, {}, "More than one Trash item matches the saved identity.", {}};
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
	return runShell(item.value, physical, destination, expected, cancel, false);
}
#endif
