#include "avbparser.h"
#include "mobid.h"

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <cstring>
#include <utility>

// MARK: - Bin file structure
// An Avid bin is a Bento container. The header looks like:
//
//   06 00 "DomainDJBO"  07 00 "AObjDoc"  04 13 00  <YYYY/MM/DD HH:MM:SS>
//
// MOB IDs appear inside the container in two forms:
//   1. Binary — 32-byte runs in the byte stream:
//        SMPTE UMID   06 0E 2B 34 04 01 ...   (32 bytes)
//        Avid MOB     06 0A 2B 34 01 01 0F ... (32 bytes)
//
//   2. ASCII hex string — same underlying 32 bytes, serialised with
//      dashes at field boundaries, e.g.:
//        "060a2b340101010001010f0013-000000-6b0fb74dc6fa687b-..."
//      Total 64 hex chars after stripping dashes.
//
// MARK: - Endian mismatch
// Bytes 16..23 hold a (uint32, uint16, uint16) triple. AVB stores
// them little-endian; PMR stores them big-endian. The same logical
// MOB therefore has two distinct 32-byte representations:
//
//   Bin : 060a2b3401010105 01010f1013000000 78563412 bbaa ddcc 0123456789abcdef
//   PMR : 060a2b3401010105 01010f1013000000 12345678 aabb ccdd 0123456789abcdef
//
// Both variants are inserted into `mobIds`, so lookups against
// `MediaFile::mobId` and `MediaFile::compositionMobId` hit regardless
// of which source the bytes originated from.
//
// MARK: - Sentinel filtering
// Bento sentinels share the `06 0? 2B 34` prefix (e.g. `06 0E 2B 34
// 7F 7F 2A 80`) and are filtered out by the byte-4/12/20 checks in
// the validators below.

namespace
{
constexpr int kMobIdLen = 32;

// MARK: - Pattern validators

/// True if the bytes at `p` look like a real SMPTE UMID.
/// Caller has already matched the `06 0E 2B 34` prefix; we check
/// bytes 4 and 5 to rule out the Bento sentinels.
bool isValidSmpteUmid(const uchar *p)
{
	return p[4] == 0x04 && p[5] == 0x01;
}

/// True if the bytes at `p` look like a real Avid MOB. Caller has
/// matched `06 0A 2B 34`; we check bytes 4, 12, and 20 for the
/// signature pattern Avid writes (and Bento sentinels don't).
bool isValidAvidMob(const uchar *p)
{
	return p[4] == 0x01 && p[12] == 0x44 && p[20] == 0x48;
}

// MARK: - Endian conversion

/// Reverses the u32 at bytes 16..19 and the two u16s at 20..21 and
/// 22..23. Converts between AVB (little-endian) and PMR (big-endian)
/// representations of the same logical MOB.
void swapMobMiddleFields(const uchar *in, uchar *out)
{
	std::memcpy(out, in, kMobIdLen);
	std::swap(out[16], out[19]);
	std::swap(out[17], out[18]);
	std::swap(out[20], out[21]);
	std::swap(out[22], out[23]);
}

/// Inserts both endian variants of a 32-byte MOB into the result
/// set, so the same logical MOB matches whether it arrived from a
/// PMR (big-endian) or an AVB (little-endian) source.
void insertBothForms(QSet<QString> &out, const uchar *raw32)
{
	out.insert(MobId::format(raw32));
	uchar swapped[kMobIdLen];
	swapMobMiddleFields(raw32, swapped);
	out.insert(MobId::format(swapped));
}

// MARK: - Hex-string decoding

/// Strips dashes and decodes a hex-string MOB like
///   `"060a2b340101010001010f0013-000000-6b0fb74dc6fa687b-..."`
/// into 32 raw bytes. Returns false if the input doesn't have
/// exactly 64 hex chars after dashes are removed, or if the
/// decoded buffer comes back the wrong size.
bool decodeHexStringMob(const QByteArray &hexWithDashes, uchar (&out)[kMobIdLen])
{
	QByteArray clean;
	clean.reserve(hexWithDashes.size());
	for (char c : hexWithDashes)
	{
		if (c != '-')
			clean.append(c);
	}
	if (clean.size() != 64)
		return false;
	const QByteArray decoded = QByteArray::fromHex(clean);
	if (decoded.size() != kMobIdLen)
		return false;
	std::memcpy(out, decoded.constData(), kMobIdLen);
	return true;
}
} // namespace

// MARK: - Parse

AvbBin AvbParser::parse(const QString &avbFilePath)
{
	AvbBin result;
	result.filePath = avbFilePath;

	// `completeBaseName()` preserves embedded fullstops, so
	// `director.cuts.avb` becomes `director.cuts`
	QFileInfo fi(avbFilePath);
	result.displayName = fi.completeBaseName();

	QFile file(avbFilePath);
	if (!file.open(QIODevice::ReadOnly))
	{
		qWarning() << "AVB: cannot open" << avbFilePath << file.errorString();
		return result;
	}

	// Bins are typically small; 64 MB is a sanity cap.
	static constexpr qint64 kMaxBinBytes = 64LL * 1024 * 1024;
	if (file.size() > kMaxBinBytes)
	{
		qWarning() << "AVB: larger than 64 MB." << avbFilePath
		           << file.size() << "bytes";
		return result;
	}

	const QByteArray buf = file.readAll();
	file.close();

	// Header check first — without it, we may hunt MOB patterns in
	// random files that produce nonsense matches.
	static const QByteArray kAvbHeader =
	    QByteArray::fromRawData("\x06\x00"
	                            "DomainDJBO",
	                            12);
	if (!buf.startsWith(kAvbHeader))
	{
		qWarning() << "AVB: not an Avid bin (header mismatch)" << avbFilePath;
		return result;
	}

	const auto *data = reinterpret_cast<const uchar *>(buf.constData());
	const qint64 size = buf.size();

	// MARK: Pass 1 — binary 32-byte MOB IDs

	// `memchr` uses SIMD on modern processors to skip to the next
	// 0x06 byte.
	if (size >= kMobIdLen)
	{
		const uchar *const end = data + (size - kMobIdLen);
		const uchar *p = data;
		while (p <= end)
		{
			p = static_cast<const uchar *>(std::memchr(p, 0x06, end - p + 1));
			if (!p)
				break;
			if (p[2] != 0x2B || p[3] != 0x34)
			{
				++p;
				continue;
			}
			if ((p[1] == 0x0E && isValidSmpteUmid(p)) ||
			    (p[1] == 0x0A && isValidAvidMob(p)))
			{
				insertBothForms(result.mobIds, p);
				p += kMobIdLen;
			}
			else
			{
				++p;
			}
		}
	}

	// MARK: Pass 2 — ASCII hex-string MOB IDs

	// Match `06(?:0a|0e)2b34[0-9a-f-]{56,68}` — 8-char prefix
	// followed by 56-68 chars from `[0-9a-f-]`.
	// `decodeHexStringMob` validates the decoded length.
	const char *bytes = buf.constData();
	auto isHexDash = [](char c)
	{
		return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '-';
	};
	for (qint64 i = 0; i + 8 <= size; ++i)
	{
		if (bytes[i] != '0' || bytes[i + 1] != '6')
			continue;
		if (bytes[i + 2] != '0')
			continue;
		if (bytes[i + 3] != 'a' && bytes[i + 3] != 'e')
			continue;
		if (bytes[i + 4] != '2' || bytes[i + 5] != 'b')
			continue;
		if (bytes[i + 6] != '3' || bytes[i + 7] != '4')
			continue;

		qint64 j = i + 8;
		const qint64 maxEnd = qMin(i + 8 + 68, size);
		while (j < maxEnd && isHexDash(bytes[j]))
			++j;

		if (j - i >= 8 + 56)
		{
			uchar raw[kMobIdLen];
			if (decodeHexStringMob(QByteArray::fromRawData(bytes + i, int(j - i)), raw))
				insertBothForms(result.mobIds, raw);
			i = j - 1; // ++i next iteration lands at j
		}
	}

	result.valid = true;
	return result;
}