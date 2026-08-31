#include "fileidentity.h"

#include "conventions.h"
#include "formatutil.h"
#include "mxfparser.h"
#include "nativefile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>

#if defined(Q_OS_WIN)
#include <windows.h>
#else
#include <sys/stat.h>
#endif
#ifdef Q_OS_MAC
#include <sys/attr.h>
#include <unistd.h>
#include <uuid/uuid.h>
#endif

// MARK: - FileIdentity capture

FileIdentity FileIdentity::capture(const QString &path, bool readContent)
{
	FileIdentity id;

#if defined(Q_OS_WIN)
	// One handle, opened for attributes only, with full sharing so we never
	// block an editor that has the file open. Everything below reads from
	// this handle, so size / time / IDs are one coherent snapshot rather
	// than three racing stat calls.
	const QString native = QDir::toNativeSeparators(path);
	const HANDLE h =
		::CreateFileW(reinterpret_cast<const wchar_t *>(native.utf16()), FILE_READ_ATTRIBUTES,
					  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
					  OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return id; // strength stays None: "couldn't examine it"

	BY_HANDLE_FILE_INFORMATION info{};
	if (!::GetFileInformationByHandle(h, &info))
	{
		::CloseHandle(h);
		return id;
	}
	id.size = (qint64(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
	// Raw FILETIME (100 ns units since 1601). The units and epoch differ
	// from the POSIX branch below, which is fine: an identity is only ever
	// compared against another capture of the same file on the same
	// machine, so equality is all that matters, not the epoch.
	ULARGE_INTEGER t;
	t.LowPart = info.ftLastWriteTime.dwLowDateTime;
	t.HighPart = info.ftLastWriteTime.dwHighDateTime;
	id.mtimeNs = qint64(t.QuadPart);
	id.volumeId = info.dwVolumeSerialNumber;
	id.fileId = (quint64(info.nFileIndexHigh) << 32) | info.nFileIndexLow;

	// Prefer the extended query where it works: a 64-bit volume serial and
	// a 128-bit file ID (ReFS). We keep the low 64 bits of the ID — on
	// NTFS the high 64 are zero, and on ReFS the low 64 alone still
	// distinguish files far beyond what this check needs.
	FILE_ID_INFO idInfo{};
	if (::GetFileInformationByHandleEx(h, FileIdInfo, &idInfo, sizeof(idInfo)))
	{
		id.volumeId = idInfo.VolumeSerialNumber;
		quint64 low = 0;
		memcpy(&low, idInfo.FileId.Identifier, sizeof(low));
		if (low != 0)
			id.fileId = low;
	}
	::CloseHandle(h);
#else
	struct stat st
	{
	};
	if (::stat(QFile::encodeName(path).constData(), &st) != 0)
		return id; // strength stays None: "couldn't examine it"
	id.size = qint64(st.st_size);
	// Full-precision mtime: QFileInfo::lastModified() rounds to
	// milliseconds, which would blunt the SizeTime tier for no reason.
#ifdef Q_OS_MAC
	id.mtimeNs = qint64(st.st_mtimespec.tv_sec) * Q_INT64_C(1000000000) + st.st_mtimespec.tv_nsec;
#else
	id.mtimeNs = qint64(st.st_mtim.tv_sec) * Q_INT64_C(1000000000) + st.st_mtim.tv_nsec;
#endif
	id.fileId = quint64(st.st_ino);
	id.volumeId = quint64(st.st_dev);
#endif

	// The strength rule: file IDs are only trusted on volumes proven to
	// keep them stable (the allowlist). On everything else — network,
	// FAT, unknown — the IDs may be synthesized per-mount, and trusting
	// them would produce false "different file!" refusals after every
	// remount. SizeTime is the honest tier there; the content half below
	// is what actually carries the weight on those volumes.
	const bool idsOk = id.fileId != 0;
	id.strength = (idsOk && NativeFile::isProvenLocalVolume(path)) ? Strength::Full
																   : Strength::SizeTime;

	// Content half: the Avid UMID baked into the MXF header — a bounded
	// read of a few hundred KB, never the essence. Only attempted for
	// .mxf names; a .wav/.aif/.omf simply has no UMID to offer, and a
	// corrupt MXF header comes back empty. Empty is recorded honestly
	// (the journal's strength field plus the empty umid say exactly what
	// was checkable).
	if (readContent && Conventions::hasMxfExtension(path))
		id.contentUmid = MxfParser::parseHeader(path).umid;

	return id;
}

// MARK: - FileIdentity verify

FileIdentity::Verdict FileIdentity::verify(const QString &path, const FileIdentity &expected,
										   FileIdentity *actualOut)
{
	// Skip the MXF header re-parse when the expectation carries no UMID —
	// there would be nothing to compare it against.
	const bool needContent = !expected.contentUmid.isEmpty();
	const FileIdentity now = capture(path, needContent);
	if (actualOut)
		*actualOut = now;

	if (now.strength == Strength::None)
		return QFileInfo::exists(path) ? Verdict::Unreadable : Verdict::Missing;

	// Size always has to agree, at every strength: a size change means the
	// bytes are not the bytes that were recorded, whatever else matches.
	if (expected.size >= 0 && now.size != expected.size)
		return Verdict::Changed;

	if (expected.strength == Strength::Full)
	{
		// The expectation was recorded at full strength; verifying it at
		// anything less would be claiming a check we didn't make. (This
		// happens if the volume somehow stopped being proven-local — a
		// state odd enough that refusing with "couldn't re-check" is the
		// only honest answer.)
		if (now.strength != Strength::Full)
			return Verdict::Unreadable;
		if (now.fileId != expected.fileId || now.volumeId != expected.volumeId)
			return Verdict::Changed;
		// mtime is informational at Full: an in-place edit keeps the file
		// ID (same object), and a media swap is caught by the UMID below.
	}
	else if (expected.strength == Strength::SizeTime)
	{
		// Both captures read the same filesystem, so any server-side
		// truncation of timestamps cancels out and exact equality is the
		// right comparison.
		if (expected.mtimeNs != 0 && now.mtimeNs != expected.mtimeNs)
			return Verdict::Changed;
	}

	// The content half: same media, or not? A now-empty UMID where one
	// was readable before counts as Changed — a header that stopped
	// parsing is not the file we recorded, whatever the stat says.
	if (needContent && now.contentUmid != expected.contentUmid)
		return Verdict::Changed;

	return Verdict::Match;
}

FileIdentity::Verdict FileIdentity::verifyRelocated(const QString &path,
													const FileIdentity &expected,
													FileIdentity *actualOut)
{
	const bool needContent = !expected.contentUmid.isEmpty();
	const FileIdentity now = capture(path, needContent);
	if (actualOut)
		*actualOut = now;

	if (now.strength == Strength::None)
		return QFileInfo::exists(path) ? Verdict::Unreadable : Verdict::Missing;

	// Only what survives a move: the byte count and the media's own id.
	if (expected.size >= 0 && now.size != expected.size)
		return Verdict::Changed;
	if (needContent && now.contentUmid != expected.contentUmid)
		return Verdict::Changed;

	return Verdict::Match;
}

QString FileIdentity::explainDifference(const FileIdentity &expected, const FileIdentity &actual)
{
	if (expected.size >= 0 && actual.size >= 0 && actual.size != expected.size)
		return QStringLiteral("its size changed from %1 to %2")
			.arg(Format::bytes(expected.size), Format::bytes(actual.size));
	if (expected.strength == Strength::Full && actual.strength == Strength::Full &&
		(actual.fileId != expected.fileId || actual.volumeId != expected.volumeId))
		return QStringLiteral("the disk reports it is a different file than the one recorded");
	if (!expected.contentUmid.isEmpty() && actual.contentUmid.isEmpty())
		return QStringLiteral("the Avid media ID inside it can no longer be read");
	if (!expected.contentUmid.isEmpty() && actual.contentUmid != expected.contentUmid)
		return QStringLiteral("the Avid media ID inside it is different");
	if (expected.mtimeNs != 0 && actual.mtimeNs != 0 && actual.mtimeNs != expected.mtimeNs)
		return QStringLiteral("its modification date changed");
	return QStringLiteral("it no longer matches what was recorded");
}

// MARK: - FileIdentity journal round-trip

QJsonObject FileIdentity::toJson() const
{
	QJsonObject o;
	if (size >= 0)
		o.insert(QStringLiteral("size"), size);
	if (mtimeNs != 0)
		o.insert(QStringLiteral("mtime"), mtimeNs);
	// Hex strings, not JSON numbers: these are unsigned 64-bit values and
	// a JSON number can't round-trip the full range exactly.
	if (strength == Strength::Full)
	{
		o.insert(QStringLiteral("fileId"), QString::number(fileId, 16));
		o.insert(QStringLiteral("volId"), QString::number(volumeId, 16));
	}
	if (!contentUmid.isEmpty())
		o.insert(QStringLiteral("umid"), contentUmid);
	o.insert(QStringLiteral("str"), int(strength));
	return o;
}

FileIdentity FileIdentity::fromJson(const QJsonObject &o)
{
	FileIdentity id;
	id.size = o.value(QStringLiteral("size")).toInteger(-1);
	id.mtimeNs = o.value(QStringLiteral("mtime")).toInteger(0);
	id.fileId = o.value(QStringLiteral("fileId")).toString().toULongLong(nullptr, 16);
	id.volumeId = o.value(QStringLiteral("volId")).toString().toULongLong(nullptr, 16);
	id.contentUmid = o.value(QStringLiteral("umid")).toString();
	const int s = o.value(QStringLiteral("str")).toInt(0);
	id.strength = (s >= 0 && s <= 2) ? Strength(s) : Strength::None;
	return id;
}

// MARK: - VolumeIdentity capture

VolumeIdentity VolumeIdentity::capture(const QString &anyPathOnVolume)
{
	VolumeIdentity v;
	const QStorageInfo info(anyPathOnVolume);
	if (!info.isValid() || !info.isReady())
		return v; // strength None: nothing mounted there to identify

	v.rootPath = info.rootPath();
	v.label = info.name();
	v.fsType = QString::fromLatin1(info.fileSystemType());
	v.capacityBytes = info.bytesTotal();
	v.strength = Strength::Weak;

#ifdef Q_OS_MAC
	// The volume's own UUID, read via getattrlist on the mount point.
	// Read-only — nothing is ever written onto the user's drives; the OS
	// minted this identity when the volume was formatted.
	struct attrlist al
	{
	};
	al.bitmapcount = ATTR_BIT_MAP_COUNT;
	al.volattr = ATTR_VOL_INFO | ATTR_VOL_UUID;
	// getattrlist's reply layout: a u32 total length, then the requested
	// attributes in bitmap order. uuid_t is char[16], so the struct is
	// naturally packed; no padding to worry about.
	struct VolUuidReply
	{
		u_int32_t length;
		uuid_t uuid;
	} reply{};
	if (::getattrlist(QFile::encodeName(v.rootPath).constData(), &al, &reply, sizeof(reply), 0) ==
			0 &&
		reply.length >= sizeof(reply))
	{
		// An all-zero UUID is "this filesystem has none" (some network
		// mounts) — Weak, honestly, rather than a fake Full.
		uuid_t zero{};
		if (uuid_compare(reply.uuid, zero) != 0)
		{
			uuid_string_t s;
			uuid_unparse_upper(reply.uuid, s);
			v.uuid = QString::fromLatin1(s);
			v.strength = Strength::Full;
		}
	}
#elif defined(Q_OS_WIN)
	QString root = QDir::toNativeSeparators(v.rootPath);
	if (!root.endsWith(QLatin1Char('\\')))
		root += QLatin1Char('\\');

	// The \\?\Volume{GUID}\ path is the volume's permanent address — it
	// survives drive-letter changes, which is the whole point. Network
	// shares have none and the call fails, leaving strength at Weak.
	wchar_t guidPath[64] = {};
	if (::GetVolumeNameForVolumeMountPointW(reinterpret_cast<const wchar_t *>(root.utf16()),
											guidPath, 64))
		v.uuid = QString::fromWCharArray(guidPath);

	DWORD serial = 0;
	if (::GetVolumeInformationW(reinterpret_cast<const wchar_t *>(root.utf16()), nullptr, 0,
								&serial, nullptr, nullptr, nullptr, 0))
		v.serial = serial;

	if (!v.uuid.isEmpty() || v.serial != 0)
		v.strength = Strength::Full;
#endif

	return v;
}

// MARK: - VolumeIdentity matching

bool VolumeIdentity::matches(const VolumeIdentity &other) const
{
	// Nothing captured is nothing to compare — never a match. Callers
	// treat "can't tell" as "don't touch".
	if (strength == Strength::None || other.strength == Strength::None)
		return false;

	// OS identities decide when both sides have one. Case-insensitive:
	// uuid_unparse casing and the GUID-path casing are formatting
	// choices, not identity.
	if (!uuid.isEmpty() && !other.uuid.isEmpty())
		return uuid.compare(other.uuid, Qt::CaseInsensitive) == 0;
	if (serial != 0 && other.serial != 0)
		return serial == other.serial;

	// Weak fingerprint: label + filesystem type + capacity. Honest best
	// for volumes with no OS identity; the journal's strength field lets
	// recovery narrate that the match was weak.
	return label == other.label && fsType.compare(other.fsType, Qt::CaseInsensitive) == 0 &&
		   capacityBytes == other.capacityBytes;
}

// MARK: - VolumeIdentity journal round-trip

QJsonObject VolumeIdentity::toJson() const
{
	QJsonObject o;
	if (!uuid.isEmpty())
		o.insert(QStringLiteral("uuid"), uuid);
	if (serial != 0)
		o.insert(QStringLiteral("serial"), QString::number(serial, 16));
	if (!label.isEmpty())
		o.insert(QStringLiteral("label"), label);
	if (!fsType.isEmpty())
		o.insert(QStringLiteral("fs"), fsType);
	if (capacityBytes > 0)
		o.insert(QStringLiteral("bytes"), capacityBytes);
	if (!rootPath.isEmpty())
		o.insert(QStringLiteral("root"), rootPath);
	o.insert(QStringLiteral("str"), int(strength));
	return o;
}

VolumeIdentity VolumeIdentity::fromJson(const QJsonObject &o)
{
	VolumeIdentity v;
	v.uuid = o.value(QStringLiteral("uuid")).toString();
	v.serial = o.value(QStringLiteral("serial")).toString().toUInt(nullptr, 16);
	v.label = o.value(QStringLiteral("label")).toString();
	v.fsType = o.value(QStringLiteral("fs")).toString();
	v.capacityBytes = o.value(QStringLiteral("bytes")).toInteger(0);
	v.rootPath = o.value(QStringLiteral("root")).toString();
	const int s = o.value(QStringLiteral("str")).toInt(0);
	v.strength = (s >= 0 && s <= 2) ? Strength(s) : Strength::None;
	return v;
}
