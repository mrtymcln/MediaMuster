#pragma once
#include <QString>
#include <functional>
class QFile;

// Platform durability requests.
// OpFile owns handles and no-overwrite relocation. All byte copying goes
// through OpCopier; its job policy determines whether checksum verification runs.
namespace NativeFile
{
enum class SyncResult
{
	Ok,
	OkDegraded,
	Failed
};
// Flush Qt and OS buffers, requesting the device cache barrier where available.
// Unsupported full durability is reported separately from an I/O failure.
SyncResult syncFile(QFile &file);
// Requests persistence of directory changes. A failure must remain visible
// to the caller; success cannot establish remote hardware's behaviour.
// Optional error reports the failed native call, path and OS error code.
SyncResult syncDirectory(const QString &path, QString *error = nullptr);
// Classifies the result of the flush itself, never a failed handle open.
SyncResult directoryFlushResult(int nativeCode);
using DirectorySync = std::function<SyncResult(const QString &, QString *)>;
} // namespace NativeFile
