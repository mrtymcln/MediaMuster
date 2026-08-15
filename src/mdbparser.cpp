#include "mdbparser.h"
#include "logging.h"
#include "mobid.h"
#include <QFile>
#include <QSet>
#include <QtEndian>
#include <QDebug>
#include <algorithm>
#include <cstring>

// MARK: - The record framing
//
// `msmMMOB.mdb` is not a Bento/AObjDoc container (that is what `.avb` bins
// and `.avr` files are) and not AAF: it is Avid's own flat object stream.
// Every named object ends with a fixed header, and a 32-byte MOB ID closes it:
//
//   <name>\0  <handle>  <u16 count>  <count x handle>  <MOB:32>
//
// A handle is eight bytes — a little-endian u32 whose high word is 1, then a
// u32 zero. Because `count` must equal the number of handles AND the run must
// land exactly on the MOB, a header is either recognised outright or rejected;
// there is no distance threshold to tune. That is the whole difference from
// the old parser, which mined printable strings out of a +/-1 KB window around
// each MOB and paired them by byte distance.
//
// The name is the TAIL of a record, not its head: an object's attributes are
// written before it. So a record owns the bytes from the previous record's MOB
// up to its own name, and within that span each attribute is written as
//
//   <value>\0 [<value-utf8>\0] <_KEY>\0 <handle>
//
// — value first, key second, and non-ASCII values twice: a MacRoman copy then
// a UTF-8 one. Taking the copy nearest the key gets UTF-8; the MacRoman twin
// fails to decode, which is how it stays out of the UI (a name with no clean
// copy shows blank rather than garbled).
//
// MARK: - How this was verified
//
// Against a 360-file Avid folder scanned with its own msmMMOB.mdb, using each
// MXF's MaterialPackage name and _SRCFILE attribute as ground truth (the MOB
// IDs join after MobId::swapMiddleFields — MXF writes the middle fields
// little-endian, the MDB big-endian):
//
//   clip name    360/360 exact   (the old parser got 0 — see below)
//   source path  354/354 exact   (every file that has one)
//   bin          360/360 present
//
// The old parser scored zero clip names on that folder because it looked for a
// literal "Name" marker next to the value. That marker only ever appears in
// the property dictionary at the very front of the file, so at most one clip
// in an entire database could match.

namespace
{
	// MARK: - Constants

	/// Anything smaller cannot hold a header plus a MOB.
	constexpr qint64 kMinFileSize = 64;

	constexpr qint64 kHandleSize = 8;

	/// A record with more referenced handles than this is not something Avid
	/// writes; the cap stops a corrupt file from walking backwards forever.
	constexpr int kMaxHandles = 256;

	/// Avid's placeholder MOB. Byte-identical in every database surveyed
	/// (508 copies in one, 272 in another, 38 in a third, across unrelated
	/// projects) and present in no MXF, so it names no media. Its label also
	/// differs from a real MOB's: `..01 01 01 00 | 01 01 0f 00` where real
	/// ones read `..01 01 01 05 | 01 01 0f 10`. The old parser emitted it as
	/// a record.
	const QByteArray kPlaceholderMob = QByteArray::fromHex(
		"060a2b340101010001010f0013000000bacc8a260647d528e5cd3f5b5f40443f");

	const QSet<QString> kContainerHints = {
		QStringLiteral("QTFF"), QStringLiteral("MXF"), QStringLiteral("MOV"), QStringLiteral("AVI"),
		QStringLiteral("MP4"), QStringLiteral("WAV"), QStringLiteral("AIFF")};

	const QByteArray kKeyOrgBin = QByteArrayLiteral("_ORG_BIN");
	const QByteArray kKeySrcFile = QByteArrayLiteral("_SRCFILE");
	const QByteArray kKeyImportSetting = QByteArrayLiteral("_IMPORTSETTING");

	// MARK: - Byte-level helpers

	/// A byte that can appear inside a name: printable ASCII, or any high byte
	/// (UTF-8 continuation, or a MacRoman accent in the twin copy). NUL is the
	/// terminator and therefore the one thing that ends a run.
	inline bool isNameByte(uchar c)
	{
		return (c >= 0x20 && c != 0x7F);
	}

	inline quint32 readU32(const uchar *p)
	{
		return qFromLittleEndian<quint32>(p);
	}

	/// True when `pos` starts an 8-byte object handle.
	bool isHandleAt(const uchar *base, qint64 size, qint64 pos)
	{
		if (pos < 0 || pos + kHandleSize > size)
			return false;
		return (readU32(base + pos) >> 16) == 1 && readU32(base + pos + 4) == 0;
	}

	/// Where a record's name starts and how long it is, for the record whose
	/// MOB sits at `mobOff`. `valid` is false when the bytes before the MOB
	/// aren't a header — most MOB occurrences are plain references, not
	/// records, so this is the common case and must stay cheap.
	struct RecordHead
	{
		qint64 nameStart = 0;
		qint64 nameLen = 0;
		bool valid = false;
	};

	[[nodiscard]] RecordHead recordHeadAt(const uchar *base, qint64 size, qint64 mobOff)
	{
		// Count the handles immediately before the MOB.
		int handles = 0;
		qint64 p = mobOff;
		while (handles < kMaxHandles && isHandleAt(base, size, p - kHandleSize))
		{
			p -= kHandleSize;
			++handles;
		}
		if (handles < 1)
			return {};

		// The count must name exactly the handles that follow it, and the run
		// must end exactly on the MOB. Try the longest run first: a shorter one
		// could coincide with a count-shaped pair of bytes inside the header.
		for (int take = handles; take >= 1; --take)
		{
			const qint64 countPos = mobOff - qint64(take) * kHandleSize - 2;
			if (countPos < 0)
				continue;
			if (qFromLittleEndian<quint16>(base + countPos) != quint16(take))
				continue;
			// The record's own handle sits just before its member count.
			if (!isHandleAt(base, size, countPos - kHandleSize))
				continue;

			const qint64 nulPos = countPos - kHandleSize - 1;
			if (nulPos < 0 || base[nulPos] != 0)
				continue;
			qint64 s = nulPos;
			while (s > 0 && isNameByte(base[s - 1]))
				--s;
			if (nulPos - s < 1)
				continue;
			return {s, nulPos - s, true};
		}
		return {};
	}

	// MARK: - Strings inside a record

	/// One NUL-terminated printable run. Offsets are file-absolute.
	struct SpanString
	{
		qint64 offset;
		QByteArray value;
	};

	[[nodiscard]] QVector<SpanString> stringsIn(const uchar *base, qint64 lo, qint64 hi)
	{
		QVector<SpanString> out;
		qint64 i = lo;
		while (i < hi)
		{
			if (!isNameByte(base[i]))
			{
				++i;
				continue;
			}
			const qint64 start = i;
			while (i < hi && isNameByte(base[i]))
				++i;
			// Only a NUL-terminated run is a value; a run cut off by the span
			// edge belongs to the neighbouring record.
			if (i < hi && base[i] == 0 && i > start)
				out.append({start, QByteArray(reinterpret_cast<const char *>(base + start),
											  int(i - start))});
			++i;
		}
		return out;
	}

	/// Decode a value written by Avid, rejecting the MacRoman twin.
	///
	/// Avid writes a non-ASCII value twice — MacRoman first, UTF-8 second — so
	/// the copy nearest its key is the UTF-8 one. A MacRoman run is almost
	/// never valid UTF-8, and QString::fromUtf8 marks the failure with
	/// replacement characters; treating that as "no value" lets the caller
	/// fall through to the clean copy, and leaves a blank rather than mojibake
	/// when no clean copy exists.
	[[nodiscard]] QString decodeUtf8(const QByteArray &raw)
	{
		const QString s = QString::fromUtf8(raw);
		if (s.contains(QChar(QChar::ReplacementCharacter)))
			return {};
		return s;
	}

	/// The value belonging to the key at index `k`: the nearest preceding run
	/// that decodes cleanly. Two back is far enough for the MacRoman/UTF-8 pair
	/// and stops a key with no value of its own from stealing the one before.
	[[nodiscard]] QString valueBefore(const QVector<SpanString> &strs, int k)
	{
		for (int j = k - 1; j >= 0 && j >= k - 2; --j)
		{
			const QString s = decodeUtf8(strs[j].value);
			if (!s.isEmpty())
				return s;
		}
		return {};
	}

	/// Basename component of a path, handling both `/` and `\`.
	QString pathBasename(const QString &path)
	{
		const int slash = qMax(path.lastIndexOf(QLatin1Char('/')), path.lastIndexOf(QLatin1Char('\\')));
		return (slash >= 0) ? path.mid(slash + 1) : path;
	}

	/// Read one record's attributes out of the bytes it owns. Walks backwards
	/// so the attribute nearest the record's own name wins: where a span holds
	/// two of the same key, the later one is this record's and the earlier
	/// belongs to an object whose header this parser didn't recognise.
	[[nodiscard]] MdbRecord attributesIn(const uchar *base, qint64 lo, qint64 hi)
	{
		MdbRecord rec;
		const QVector<SpanString> strs = stringsIn(base, lo, hi);
		for (int k = strs.size() - 1; k >= 0; --k)
		{
			const QByteArray &v = strs[k].value;
			if (v == kKeyImportSetting)
			{
				rec.isImported = true;
			}
			else if (v == kKeyOrgBin && rec.bin.isEmpty())
			{
				rec.bin = valueBefore(strs, k);
			}
			else if (v == kKeySrcFile && rec.sourceFilePath.isEmpty())
			{
				rec.sourceFilePath = valueBefore(strs, k);
				rec.sourceFileName = pathBasename(rec.sourceFilePath);
			}
			else if (rec.sourceContainer.isEmpty())
			{
				const QString token = QString::fromLatin1(v);
				if (kContainerHints.contains(token))
					rec.sourceContainer = token;
			}
		}
		return rec;
	}

	/// Fill blanks in `into` from `from`. The same MOB is written several times
	/// and only some copies carry attributes, so the copies are unioned rather
	/// than letting the last one win.
	void mergeInto(MdbRecord &into, const MdbRecord &from)
	{
		const auto fill = [](QString &dst, const QString &src)
		{
			if (dst.isEmpty())
				dst = src;
		};
		fill(into.clipName, from.clipName);
		fill(into.bin, from.bin);
		fill(into.sourceFilePath, from.sourceFilePath);
		fill(into.sourceFileName, from.sourceFileName);
		fill(into.sourceContainer, from.sourceContainer);
		into.isImported = into.isImported || from.isImported;
	}

	// MARK: - MOB offsets

	/// Find every byte position where a 32-byte MOB starts. Avid MDB
	/// always uses the `06 0a 2b 34` prefix (never SMPTE `06 0e 2b 34`),
	/// so only scan for the `0a` form.
	[[nodiscard]] QVector<qint64> findMobOffsets(const QByteArray &data)
	{
		QVector<qint64> out;
		const qint64 n = data.size();
		if (n < MobId::kRawSize)
			return out;

		const auto *ptr = reinterpret_cast<const uchar *>(data.constData());
		const uchar *const end = ptr + (n - MobId::kRawSize);
		const uchar *p = ptr;
		while (p <= end)
		{
			p = static_cast<const uchar *>(std::memchr(p, 0x06, end - p + 1));
			if (!p)
				break;
			if (p[1] == 0x0a && p[2] == 0x2b && p[3] == 0x34)
			{
				out.append(p - ptr);
				p += MobId::kRawSize;
			}
			else
			{
				++p;
			}
		}
		return out;
	}

} // namespace

// MARK: - Public API

QVector<MdbRecord> MdbParser::parse(const QString &mdbFilePath, bool *ok)
{
	// Pessimistic default: the early failure returns below leave it false;
	// reaching the record walk flips it. See the header for why ok=true is a
	// weaker guarantee here than in PmrParser.
	if (ok)
		*ok = false;

	QVector<MdbRecord> records;

	QFile file(mdbFilePath);
	if (!file.open(QIODevice::ReadOnly))
	{
		qCWarning(lcMdb) << "cannot open" << mdbFilePath << file.errorString();
		return records;
	}
	const QByteArray data = file.readAll();
	file.close();

	if (data.size() < kMinFileSize)
	{
		qCWarning(lcMdb) << "file too small" << data.size() << mdbFilePath;
		return records;
	}

	const QVector<qint64> mobOffsets = findMobOffsets(data);

	// Validity gate. Every real MDB surveyed opens with its property
	// dictionary near the front — "_USER...", or "block NNNN\0_USER..." on
	// some variants, so the fingerprint is scanned for, never demanded at a
	// fixed offset. A file with neither the fingerprint nor a single Avid
	// MOB anywhere is not something this parser can read: without this
	// check it "parsed" to zero records with ok=true, and the scanner then
	// stamped the folder's unmatched files a verified "No reference" when
	// the honest answer is "No database". The MOB escape hatch keeps any
	// readable file acceptable regardless of its prelude; the failure
	// direction of a wrong rejection is the safe label ("couldn't check").
	// Known limit: truncation that preserves the front still passes — a
	// stream with no length field has nothing to test completeness against.
	const QByteArray head = data.left(4096);
	const bool fingerprinted = head.contains("_USER") || head.contains("_VERSION");
	if (!fingerprinted && mobOffsets.isEmpty())
	{
		qCWarning(lcMdb) << "not a recognisable Avid MDB (no property dictionary, no MOB IDs)"
						 << mdbFilePath;
		return records;
	}

	if (ok)
		*ok = true;

	const auto *base = reinterpret_cast<const uchar *>(data.constData());
	const qint64 size = data.size();

	// Group occurrences by MOB. The same MOB is written many times over
	// (source / master / referenced), and one record is emitted per unique one.
	QHash<QString, QVector<qint64>> occurrences;
	QHash<QString, QByteArray> rawBytes;
	int placeholders = 0;
	for (qint64 off : mobOffsets)
	{
		const QByteArray mob = data.mid(qsizetype(off), MobId::kRawSize);
		if (mob == kPlaceholderMob)
		{
			++placeholders;
			continue;
		}
		const QString hex = MobId::format(mob);
		occurrences[hex].append(off);
		if (!rawBytes.contains(hex))
			rawBytes.insert(hex, mob);
	}

	// MARK: Record heads, in file order

	struct Head
	{
		qint64 nameStart;
		qint64 nameLen;
		qint64 mobOff;
		QString hex;
	};
	QVector<Head> heads;
	heads.reserve(mobOffsets.size() / 4);
	for (qint64 off : mobOffsets)
	{
		const RecordHead h = recordHeadAt(base, size, off);
		if (!h.valid)
			continue;
		const QByteArray mob = data.mid(qsizetype(off), MobId::kRawSize);
		if (mob == kPlaceholderMob)
			continue;
		heads.append({h.nameStart, h.nameLen, off, MobId::format(mob)});
	}
	// findMobOffsets walks the file forwards, so `heads` is already ascending.

	// MARK: One record per head, then merge duplicates

	QHash<QString, int> indexByHex;
	// A record owns the bytes from the previous record's MOB to its own name.
	struct Span
	{
		qint64 lo;
		qint64 hi;
		int recordIndex;
	};
	QVector<Span> spans;
	spans.reserve(heads.size());

	for (int i = 0; i < heads.size(); ++i)
	{
		const qint64 lo = i ? heads[i - 1].mobOff + MobId::kRawSize : 0;
		MdbRecord rec = attributesIn(base, lo, heads[i].nameStart);
		rec.clipName = decodeUtf8(QByteArray(
			reinterpret_cast<const char *>(base + heads[i].nameStart), int(heads[i].nameLen)));
		rec.mobIdHex = heads[i].hex;
		rec.mobId = rawBytes.value(heads[i].hex);

		int idx = indexByHex.value(heads[i].hex, -1);
		if (idx < 0)
		{
			idx = records.size();
			indexByHex.insert(heads[i].hex, idx);
			records.append(rec);
		}
		else
		{
			mergeInto(records[idx], rec);
		}
		spans.append({lo, heads[i].mobOff + MobId::kRawSize, idx});
	}

	// MARK: MOBs with no head of their own

	// A clip's file MOBs sit inside the master's record and carry no header,
	// but the scanner looks up both mobId and masterMobId — so they inherit
	// the record whose span they fall in. Anything left over still gets a bare
	// record: dropping a MOB from the map would read as "not in the database"
	// and could mislabel a file as unreferenced.
	for (auto it = occurrences.cbegin(); it != occurrences.cend(); ++it)
	{
		if (indexByHex.contains(it.key()))
			continue;

		int owner = -1;
		for (qint64 off : it.value())
		{
			// Spans are ascending and disjoint, so the first with lo > off ends
			// the search.
			const auto at = std::upper_bound(spans.cbegin(), spans.cend(), off,
											 [](qint64 value, const Span &s)
											 { return value < s.lo; });
			if (at != spans.cbegin())
			{
				const Span &s = *(at - 1);
				if (off < s.hi)
				{
					owner = s.recordIndex;
					break;
				}
			}
		}

		MdbRecord rec;
		if (owner >= 0)
			rec = records[owner];
		rec.mobIdHex = it.key();
		rec.mobId = rawBytes.value(it.key());
		indexByHex.insert(it.key(), records.size());
		records.append(rec);
	}

	qCDebug(lcMdb) << "MDB:" << mdbFilePath << "size:" << data.size()
				   << "mobs:" << occurrences.size() << "records with a header:" << heads.size()
				   << "placeholder MOBs skipped:" << placeholders;

	return records;
}

MdbParser::RecordMap MdbParser::buildMobMap(const QString &mdbFilePath, bool *ok)
{
	RecordMap map;
	const auto records = parse(mdbFilePath, ok);
	map.reserve(records.size());
	for (const auto &r : records)
	{
		if (!r.mobIdHex.isEmpty())
			map.insert(r.mobIdHex, r);
	}
	return map;
}