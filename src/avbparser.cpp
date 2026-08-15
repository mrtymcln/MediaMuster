#include "avbparser.h"
#include "logging.h"
#include "mobid.h"

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <cstring>
#include <utility>

// MARK: - Bin file structure
// An Avid bin is a Bento container. The header looks like:
//
//   06 00 "DomainDJBO"  07 00 "AObjDoc"  04 13 00  <YYYY/MM/DD HH:MM:SS>
//
// MOB IDs show up two ways inside the container:
//   1. Binary: 32-byte runs in the byte stream:
//        SMPTE UMID   06 0E 2B 34 04 01 ...   (32 bytes)
//        Avid MOB     06 0A 2B 34 01 01 0F ... (32 bytes)
//
//   2. ASCII hex string: same 32 bytes, written out with dashes at
//      the field boundaries, e.g.:
//        "060a2b340101010001010f0013-000000-6b0fb74dc6fa687b-..."
//      Ends up as 64 hex chars once you strip the dashes.
//
// MARK: - Endian mismatch
// Bytes 16..23 hold a (uint32, uint16, uint16) triple. AVB writes
// them little-endian, PMR writes them big-endian. So the same
// logical MOB has two different 32-byte representations:
//
//   Bin : 060a2b3401010105 01010f1013000000 78563412 bbaa ddcc 0123456789abcdef
//   PMR : 060a2b3401010105 01010f1013000000 12345678 aabb ccdd 0123456789abcdef
//
// Both variants go into `mobIds` so lookups against MediaFile::mobId
// and MediaFile::masterMobId hit no matter which source the
// bytes came from.
//
// MARK: - Sentinel filtering
// Bento sentinels share the `06 0? 2B 34` prefix (e.g. `06 0E 2B 34
// 7F 7F 2A 80`). The fingerprint-byte checks in the validators below
// (bytes 4-5 for SMPTE, bytes 4/12/20 for Avid) filter them out.

namespace
{
	constexpr int kMobIdLen = MobId::kRawSize;
	// A real Avid bin runs a few MB even with thousands of clips. This is a memory
	// guard against a mislabelled or corrupt file, not a real-bin limit — set
	// generously so big-but-legit bins still load.
	constexpr qint64 kMaxBinBytes = 256LL * 1024 * 1024;

	// MARK: - Pattern validators

	/// Caller already matched `06 0E 2B 34`; these last two bytes tell a
	/// real UMID from a Bento sentinel.
	bool isValidSmpteUmid(const uchar *p)
	{
		return p[4] == 0x04 && p[5] == 0x01;
	}

	/// Caller already matched `06 0A 2B 34`; bytes 4, 12, 20 are the
	/// fingerprint Avid writes and sentinels don't.
	bool isValidAvidMob(const uchar *p)
	{
		return p[4] == 0x01 && p[12] == 0x44 && p[20] == 0x48;
	}

	// MARK: - Endian conversion

	/// Drops both endian variants of the same MOB into `out` so the
	/// later lookup hits whether the bytes came from a PMR or an AVB.
	/// The AVB↔PMR middle-field swap lives in MobId (single source of
	/// truth shared with toPmrForm), so the byte layout can't drift.
	void insertBothForms(QSet<QString> &out, const uchar *raw32)
	{
		out.insert(MobId::format(raw32));
		uchar swapped[kMobIdLen];
		MobId::swapMiddleFields(raw32, swapped);
		out.insert(MobId::format(swapped));
	}

	// MARK: - Hex-string decoding

	/// Decodes a dash-formatted hex MOB like
	///   `"060a2b340101010001010f0013-000000-6b0fb74dc6fa687b-..."`
	/// into 32 raw bytes. False if it isn't 64 hex chars after stripping
	/// dashes, or if QByteArray::fromHex chokes.
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

	// `completeBaseName()` keeps dots inside the name, so
	// `director.cuts.avb` stays `director.cuts`.
	QFileInfo fi(avbFilePath);
	result.displayName = fi.completeBaseName();

	QFile file(avbFilePath);
	if (!file.open(QIODevice::ReadOnly))
	{
		qCWarning(lcAvb) << "cannot open" << avbFilePath << file.errorString();
		return result;
	}

	// Peek the Bento header on the first 12 bytes before committing to a full
	// readAll. A mislabelled multi-GB file never lands in memory — which is what
	// the size cap below is really there to guard against.
	//
	// Two genuine header spellings exist: modern MC writes little-endian
	// bins (byte-order mark 06 00, fourcc reversed to "DJBO"); pre-Intel-era
	// Mac MC wrote big-endian ones (00 06, fourcc unreversed "OBJD") —
	// confirmed independently by the pyavb and libavid reverse-engineering
	// projects. The MOB scans below are endian-agnostic (insertBothForms
	// records both middle-field byte orders for every MOB, and the ASCII
	// hex pass reads text), so this gate is the only place that needs to
	// know a bin's byte order.
	static const QByteArray kAvbHeaderLE = QByteArray::fromRawData("\x06\x00"
																   "DomainDJBO",
																   12);
	static const QByteArray kAvbHeaderBE = QByteArray::fromRawData("\x00\x06"
																   "DomainOBJD",
																   12);
	const QByteArray head = file.peek(12);
	if (head != kAvbHeaderLE && head != kAvbHeaderBE)
	{
		qCWarning(lcAvb) << "not an Avid bin (header mismatch)" << avbFilePath;
		return result;
	}

	// Header checks out, so this is a real bin. Even thousands of clips only run
	// a few MB; the cap just stops a pathologically large or corrupt bin from
	// blowing out memory on the read below.
	if (file.size() > kMaxBinBytes)
	{
		qCWarning(lcAvb) << "Avid bin exceeds cap" << avbFilePath << file.size() << "bytes";
		return result;
	}

	const QByteArray buf = file.readAll();
	file.close();

	const auto *data = reinterpret_cast<const uchar *>(buf.constData());
	const qint64 size = buf.size();

	// MARK: Pass 1 — binary 32-byte MOB IDs

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
			if ((p[1] == 0x0E && isValidSmpteUmid(p)) || (p[1] == 0x0A && isValidAvidMob(p)))
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

	// Match `06(?:0a|0e)2b34[0-9a-f-]{56,68}`, case-insensitively: hex is
	// hex whichever case a writer picks (every bin surveyed uses lowercase,
	// but an uppercase MOB string must not be invisible to this pass —
	// QByteArray::fromHex in the decoder already accepts either case).
	// 8-char prefix followed by 56-68 chars from `[0-9a-fA-F-]`.
	// decodeHexStringMob validates the decoded length.
	const char *bytes = buf.constData();
	auto lowerAscii = [](char c)
	{ return (c >= 'A' && c <= 'Z') ? char(c | 0x20) : c; };
	auto isHexDash = [lowerAscii](char c)
	{
		const char l = lowerAscii(c);
		return (l >= '0' && l <= '9') || (l >= 'a' && l <= 'f') || l == '-';
	};

	static const QByteArray kPrefix = QByteArrayLiteral("060");
	qint64 i = 0;
	while (i + 8 <= size)
	{
		i = buf.indexOf(kPrefix, i);
		if (i < 0 || i + 8 > size)
			break;
		if (const char b3 = lowerAscii(bytes[i + 3]); b3 != 'a' && b3 != 'e')
		{
			++i;
			continue;
		}
		if (bytes[i + 4] != '2' || lowerAscii(bytes[i + 5]) != 'b' || bytes[i + 6] != '3' ||
			bytes[i + 7] != '4')
		{
			++i;
			continue;
		}

		qint64 j = i + 8;
		const qint64 maxEnd = qMin(i + 8 + 68, size);
		while (j < maxEnd && isHexDash(bytes[j]))
			++j;

		if (j - i >= 8 + 56)
		{
			uchar raw[kMobIdLen];
			if (decodeHexStringMob(QByteArray::fromRawData(bytes + i, int(j - i)), raw))
				insertBothForms(result.mobIds, raw);
			i = j;
		}
		else
		{
			++i;
		}
	}

	result.valid = true;
	return result;
}