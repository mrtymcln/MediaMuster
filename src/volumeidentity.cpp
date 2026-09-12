#include "volumeidentity.h"

#include "nativefile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QUrl>
#include <QVector>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <QLibrary>
#else
#include <sys/stat.h>
#endif
#ifdef Q_OS_MAC
#include <sys/attr.h>
#include <sys/mount.h>
#include <unistd.h>
#include <uuid/uuid.h>
#endif

namespace
{
// Strip credentials from mount metadata. The endpoint includes the server and
// the complete mounted share/workspace path, never merely its displayed label.
QString networkEndpoint(QString source, const QString &filesystem)
{
	source.replace('\\', '/');
	if (source.startsWith("//?/UNC/", Qt::CaseInsensitive))
		source = "//" + source.mid(8);
	if (source.startsWith("//"))
	{
		const auto url = QUrl(QStringLiteral("smb:") + source);
		if (!url.isValid() || url.host().isEmpty() || url.path().size() <= 1)
			return {};
		QString authority = url.host().toCaseFolded();
		if (authority.contains(':'))
			authority = '[' + authority + ']';
		if (url.port() >= 0)
			authority += ':' + QString::number(url.port());
		const auto path = QDir::cleanPath(url.path(QUrl::FullyDecoded));
		return QStringLiteral("share://") + authority + path;
	}
	// NFS reports host:/export/path. Do not interpret an unknown client's
	// workspace label or a disconnected Windows drive letter as a server.
	const auto colon = source.indexOf(":/");
	if (colon > 0 && filesystem.contains("nfs", Qt::CaseInsensitive))
	{
		QString host = source.left(colon).section('@', -1).toCaseFolded();
		if (host.contains('/') || host.isEmpty())
			return {};
		return filesystem.toCaseFolded() + "://" + host + QDir::cleanPath(source.mid(colon + 1));
	}
	return {};
}
} // namespace

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

#ifdef Q_OS_MAC
	struct statfs mount{};
	if (::statfs(QFile::encodeName(v.rootPath).constData(), &mount) != 0)
		return v;
	if (!(mount.f_flags & MNT_LOCAL))
	{
		v.kind = QStringLiteral("network");
		v.networkId = networkEndpoint(QString::fromLocal8Bit(mount.f_mntfromname), v.fsType);
		struct stat root{};
		if (::stat(QFile::encodeName(v.rootPath).constData(), &root) == 0 && S_ISDIR(root.st_mode))
			v.rootObjectId = QString::number(qulonglong(root.st_ino));
		return v;
	}
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
	const bool unc = root.startsWith("\\\\") &&
		(!root.startsWith("\\\\?\\") || root.startsWith("\\\\?\\UNC\\", Qt::CaseInsensitive));
	if (unc || ::GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16())) == DRIVE_REMOTE)
	{
		v.kind = QStringLiteral("network");
		QString remote = unc ? root : QString();
		if (!unc)
		{
			QLibrary provider(QStringLiteral("mpr"));
			using GetConnection = DWORD(WINAPI *)(LPCWSTR, LPWSTR, LPDWORD);
			const auto getConnection = reinterpret_cast<GetConnection>(provider.resolve("WNetGetConnectionW"));
			if (getConnection)
			{
				const auto drive = root.left(2);
				QVector<wchar_t> buffer(512);
				DWORD length = DWORD(buffer.size());
				auto code = getConnection(reinterpret_cast<LPCWSTR>(drive.utf16()), buffer.data(), &length);
				if (code == ERROR_MORE_DATA && length > 0 && length < 32768)
				{
					buffer.resize(int(length));
					code = getConnection(reinterpret_cast<LPCWSTR>(drive.utf16()), buffer.data(), &length);
				}
				if (code == NO_ERROR)
					remote = QString::fromWCharArray(buffer.constData());
			}
		}
		v.networkId = networkEndpoint(remote, v.fsType);
		const auto handle = ::CreateFileW(reinterpret_cast<LPCWSTR>(root.utf16()), FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (handle != INVALID_HANDLE_VALUE)
		{
			BY_HANDLE_FILE_INFORMATION metadata{};
			if (::GetFileInformationByHandle(handle, &metadata))
				v.rootObjectId = QString::number((qulonglong(metadata.nFileIndexHigh) << 32) | metadata.nFileIndexLow);
			::CloseHandle(handle);
		}
		return v;
	}

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
	if (kind != other.kind)
		return false;
	if (kind == "network")
		return confidence >= Confidence::Med && other.confidence >= Confidence::Med &&
			!networkId.isEmpty() && networkId == other.networkId && !rootObjectId.isEmpty() &&
			rootObjectId != "0" && rootObjectId == other.rootObjectId;
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
	o.insert(QStringLiteral("kind"), kind);
	o.insert(QStringLiteral("networkId"), networkId);
	o.insert(QStringLiteral("rootObjectId"), rootObjectId);
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
	if (!o["kind"].isString() || !o["networkId"].isString() || !o["rootObjectId"].isString())
		return v;
	v.kind = o["kind"].toString();
	if (v.kind != "local" && v.kind != "network")
		return VolumeIdentity{};
	v.networkId = o["networkId"].toString();
	v.rootObjectId = o["rootObjectId"].toString();
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
