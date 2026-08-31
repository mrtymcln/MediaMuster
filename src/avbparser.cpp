#include "avbparser.h"
#include "logcategories.h"
#include "mobid.h"

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <cstring>
#include <utility>

// MARK: - Bin file structure
// A bin is Avid's own AVB object format: a header, then a flat front-to-back
// stream of chunks, each a four-character class code plus a u32 size plus its
// payload. There is no index and no random access, which is why nothing below
// opens a container - this parser scavenges MOB IDs out of the raw byte stream
// instead, so the chunk layout never has to be understood. Verified against 66
// real bins written by MC 2.6.7 through 25.12 (1997-2026), and independently by
// the pyavb and libavid reverse-engineering projects.
//
// The header is AVB's own, length-prefixed strings and a writer version:
//
//   06 00 "Domain" "DJBO"  07 00 "AObjDoc"  04 13 00  <YYYY/MM/DD HH:MM:SS>
//   ... "IIII" ... 1E 00 "Media Composer 18.9.0.4"
//
// A MOB ID shows up as an ASCII hex string: the 32 bytes written out with
// dashes at the field boundaries, e.g.:
//   "060a2b340101010001010f0013-000000-6b0fb74dc6fa687b-..."
// which is 64 hex chars once the dashes are stripped.
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

namespace
{
	constexpr int kMobIdLen = MobId::kRawSize;
	// A real Avid bin runs a few MB even with thousands of clips. This is a memory
	// guard against a mislabelled or corrupt file, not a real-bin limit — set
	// generously so big-but-legit bins still load.
	constexpr qint64 kMaxBinBytes = 256LL * 1024 * 1024;

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

	// Peek the bin header on the first 12 bytes before committing to a full
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

	const qint64 size = buf.size();

	// Avid writes a MOB into a bin as ASCII hex text. It does NOT write it as
	// 32 contiguous bytes: the binary form interleaves AVB type tags between
	// the fields (`44` every second byte, then `48`), so a 32-byte slice is the
	// id chopped up with framing mixed in and can never match a real MOB.
	//
	// A binary scan used to run here for exactly that reason and was removed on
	// 2026-08-28 after measuring it: across 117 bins from 12 Media Composer
	// versions (2.6.7 through 25.12.2), joined against 4,353 real MOB ids from
	// every PMR on the machine, it produced 81,200 ids and matched 0 real MOBs
	// - both branches, SMPTE 06 0E and Avid 06 0A - while the text pass below
	// supplied all 1,556 real matches. Removing it left that 1,556 unchanged.
	// Its "is this a MOB" check was fingerprinting the type tags, which is why
	// it accepted 100% of candidates and still found nothing.
	//
	// TODO — read bins properly, as an object graph rather than by scavenging.
	//
	// The prize is NOT the one unreadable bin (1 of 117, a writer version
	// outside the rest of the sample). It is that a bin holds the clip name
	// and bin name for media msmMMOB.mdb does not describe: measured on one
	// real folder, its MDB covered 112 of 389 files while the project's bins
	// accounted for all 277 of the rest, joined through the master MOB the PMR
	// already supplies. Those rows show a blank Bin today and need not.
	//
	// Cost, honestly: this is a feature, not a cleanup. Every other parser here
	// reads a known layout at known offsets; a bin is a serialised object graph
	// with a class hierarchy, so it is the largest single parser in the app.
	// Reference is pyavb (MIT, so usable with attribution) - ~9,800 lines of
	// Python, of which 482 are MOB ids alone. NOT the 2012 "Avb Spec" doing the
	// rounds: that is community reverse engineering of Media Composer 5, its
	// completeness claim covers settings (.avs) files rather than bins, and it
	// mentions MobID zero times. And never copy from media-decomposer's source
	// - the spec text there is Public Domain but the code carries no licence.
	//
	// Whatever is built, do NOT reinstate a 32-byte slice: that was the removed
	// mistake. Symptom that the work has become urgent: a real bin parsing with
	// valid=true and an empty mobIds set.

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