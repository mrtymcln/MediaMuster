#pragma once
#include <QString>
#include <functional>
class QFile;

// Platform durability requests and a conservative local-filesystem predicate.
// OpFile owns handles and no-overwrite relocation. All byte copying goes
// through OpCopier; its job policy determines whether checksum verification runs.
namespace NativeFile
{
// Recognises local APFS/HFS+ and NTFS/ReFS; this is not field certification.
bool isProvenLocalVolume(const QString &path);
enum class Durability
{
	Disk,
	Platter
};
enum class SyncResult
{
	Ok,
	OkDegraded,
	Failed
};
// Flush Qt's buffer, then the OS buffer. Platter also requests the device
// cache barrier where the OS offers one. Unsupported is not an I/O error.
SyncResult syncFile(QFile &file, Durability level);
// Requests persistence of directory changes. A failure must remain visible
// to the caller; success cannot establish remote hardware's behaviour.
// Optional error reports the failed native call, path and OS error code.
SyncResult syncDirectory(const QString &path, QString *error = nullptr);
// Classifies the result of the flush itself, never a failed handle open.
SyncResult directoryFlushResult(int nativeCode);
using DirectorySync = std::function<SyncResult(const QString &, QString *)>;
} // namespace NativeFile
