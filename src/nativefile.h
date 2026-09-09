#pragma once
#include <QString>
class QFile;

// Platform durability requests and a conservative local-filesystem predicate.
// OpFile owns handles and no-overwrite relocation. All byte copying goes
// through OpCopier, on every filesystem; there are no unverified fast paths.
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
bool syncDirectory(const QString &path, QString *error = nullptr);
} // namespace NativeFile
