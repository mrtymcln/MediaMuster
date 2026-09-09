#include "fileidentity.h"

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

VolumeIdentity VolumeIdentity::capture(const QString &anyPathOnVolume)
{
	VolumeIdentity v;
	const QStorageInfo info(anyPathOnVolume);
	if (!info.isValid() || !info.isReady())
		return v; // confidence None: nothing mounted there to identify

	v.rootPath = info.rootPath();
	v.label = info.name();
	v.fsType = QString::fromLatin1(info.fileSystemType());
	v.capacityBytes = info.bytesTotal();
	v.confidence = Confidence::Med;
	// Network client identities are not qualified for automatic remount recovery.
	if (!NativeFile::isProvenLocalVolume(anyPathOnVolume))
		return v;

#ifdef Q_OS_MAC
	// The volume's own UUID, read via getattrlist on the mount point.
	// Read-only — nothing is ever written onto the user's drives; the OS
	// minted this identity when the volume was formatted.
	struct attrlist al{};
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
			v.confidence = Confidence::High;
		}
	}
#elif defined(Q_OS_WIN)
	QString root = QDir::toNativeSeparators(v.rootPath);
	if (!root.endsWith(QLatin1Char('\\')))
		root += QLatin1Char('\\');

	// The \\?\Volume{GUID}\ path is the volume's permanent address — it
	// survives drive-letter changes, which is the whole point. Network
	// shares have none and the call fails, leaving confidence at Weak.
	wchar_t guidPath[64] = {};
	if (::GetVolumeNameForVolumeMountPointW(reinterpret_cast<const wchar_t *>(root.utf16()),
											guidPath, 64))
		v.uuid = QString::fromWCharArray(guidPath);

	DWORD serial = 0;
	if (::GetVolumeInformationW(reinterpret_cast<const wchar_t *>(root.utf16()), nullptr, 0,
								&serial, nullptr, nullptr, nullptr, 0))
		v.serial = serial;

	if (!v.uuid.isEmpty())
		v.confidence = Confidence::High;
#endif

	return v;
}

// MARK: - VolumeIdentity matching

bool VolumeIdentity::matches(const VolumeIdentity &other) const
{
	if (confidence != Confidence::High || other.confidence != Confidence::High || uuid.isEmpty() ||
		other.uuid.isEmpty())
		return false;
	return uuid.compare(other.uuid, Qt::CaseInsensitive) == 0 &&
		   (serial == 0 || other.serial == 0 || serial == other.serial);
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
		o.insert(QStringLiteral("filesystem"), fsType);
	if (capacityBytes > 0)
		o.insert(QStringLiteral("bytes"), capacityBytes);
	if (!rootPath.isEmpty())
		o.insert(QStringLiteral("rootPath"), rootPath);
	o.insert(QStringLiteral("confidence"), int(confidence));
	return o;
}

VolumeIdentity VolumeIdentity::fromJson(const QJsonObject &o)
{
	VolumeIdentity v;
	v.uuid = o.value(QStringLiteral("uuid")).toString();
	v.serial = o.value(QStringLiteral("serial")).toString().toUInt(nullptr, 16);
	v.label = o.value(QStringLiteral("label")).toString();
	v.fsType = o.value(QStringLiteral("filesystem")).toString();
	v.capacityBytes = o.value(QStringLiteral("bytes")).toInteger(0);
	v.rootPath = o.value(QStringLiteral("rootPath")).toString();
	const int s = o.value(QStringLiteral("confidence")).toInt(0);
	v.confidence = (s >= 0 && s <= 2) ? Confidence(s) : Confidence::Low;
	return v;
}
