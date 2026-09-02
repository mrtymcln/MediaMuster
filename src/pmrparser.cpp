#include "pmrparser.h"
#include "avidtext.h"
#include "logcategories.h"
#include "mobid.h"
#include "omfuid.h" // OMF-era: wraps a version-2 record's 8-byte MOB to the 32-byte form.
#include "pmrkey.h"

#include <QFile>
#include <QDebug>
#include <QHash>
#include <QtEndian>

// `msmFMID.pmr` is the Persistent Media Record Avid writes into every
// `Avid MediaFiles/MXF/<n>/` folder: a flat filename-to-MOB index, and the
// ONLY place an essence file's NAME is tied to its MOBs (msmMMOB.mdb holds
// no filenames). All integers are little-endian. Layout verified to the
// last byte on four real PMRs (0 unaccounted bytes each), and matching
// MDVx's published pmrtool.cpp field for field:
//
//   Header (12 bytes) — Avid's own field names, from AMSM::LoadPMR:
//     uint32  MAGIC   (= 0x000007A9)
//     uint32  VERSION (= 8 in every MXF-era folder; = 2 in an OMF-era one)
//     uint32  numMobs (pair count)
//
//   MBCS record set (numMobs × FILE then MASTER):
//     FILE:   MOB | uint16 nameLen | name (MacRoman)
//                 | uint16 projLen | project (MacRoman)
//     MASTER: MOB | uint32 = the essence file's modification time.
//
//     The MOB is 32 bytes in version 8. OMF-era: in version 2 it is 8 bytes
//     with arbitrary leading bytes — bytes [4:12] of the file's 12-byte
//     omfi:UID — and is widened through OmfUid::wrap8 so an OMF row's id is
//     the same 32-byte string Avid itself writes into the Unicode set below
//     and into its bins. Verified to the last byte on the two real version-2
//     files under tests/fixtures/omf (0 unaccounted bytes each).
//
//     The MASTER trailer is the same 4 bytes in both versions: MC 2025 and
//     MC 2026 write Unix seconds UTC (equal to st_mtime on 360/360 real MXF
//     files and on both OMF-era audio files at capture); a 2018–19 folder held Mac
//     1904-epoch seconds in the writer's LOCAL time (every trailer = mtime +
//     2,082,844,800 + the AEST/AEDT offset). Per FILE, not per clip —
//     relatives of one clip differ by a second. Compare via
//     trailerMatchesModified, never by subtraction.
//
//   Unicode record set (every MC 2025 file; older files stop at the MBCS set):
//     uint32  VERSION_UNICODE (= 16)
//     uint32  numUnicodeMobs  (== numMobs)
//     the same records again — the FILE name now UTF-8, prefixed by two NUL
//     bytes that nameLen counts; MOBs, project (still MacRoman) and trailer
//     identical to the MBCS set (verified 360/360). OMF-era: a version-2 file
//     written by MC 2026 carries this set too, and its MOBs are ALREADY the
//     32-byte wrapped form (82/82 verified), which is why the set is read by
//     one unchanged routine for both versions.
//
// Filenames are taken from the Unicode set when it is present and pairs
// up: a name Avid wrote with a character MacRoman cannot hold (ę) survives
// only there. The project is MacRoman everywhere Avid stores it, including
// the MDB — it writes '?' for what MacRoman can't hold, and that is what
// the table shows.
//
// A MASTER record's MOB is the master clip's Master MOB. Relative tracks
// (V01/A01/A02 etc.) all share it, which is how we group relatives in the
// rebalancer and bin filter. A FILE record's MOB is the essence file's own
// (the MXF's file SourcePackage UID, after MobId::toPmrForm).

namespace
{
	constexpr qint64 kHeaderSize = 12;
	constexpr qint64 kMagicOffset = 0;
	/// Avid's own constant; every PMR in the wild starts with it.
	constexpr quint32 kPmrMagic = 0x000007A9;
	constexpr qint64 kVersionOffset = 4;
	constexpr qint64 kPairCountOffset = 8;
	/// The MXF-era layout: every PMR under `Avid MediaFiles/MXF/<n>/`.
	constexpr quint32 kPmrVersionMxf = 8;
	/// OMF-era: the version-2 layout under `OMFI MediaFiles`, identical to
	/// version 8 except that the MBCS record set's MOBs are 8 bytes.
	constexpr quint32 kPmrVersionOmf = 2;
	/// Marks the second record set (UTF-8 filenames), when present.
	constexpr quint32 kPmrVersionUnicode = 16;
	constexpr qint64 kMobIdSize = MobId::kRawSize;
	constexpr qint64 kMasterTrailerSize = 4;
	constexpr quint16 kMaxStringLen = 1024;
	constexpr quint32 kMaxPairCount = 5'000'000;

	// MARK: - Byte-level helpers

	/// Read a little-endian T at `offset`, bounds-checked. Returns a
	/// default-constructed T if the read would run past the buffer.
	template <typename T>
	T readLE(const QByteArray &data, qint64 offset)
	{
		if (offset < 0 || offset + qint64(sizeof(T)) > data.size())
			return T{};
		return qFromLittleEndian<T>(data.constData() + offset);
	}

	/// Read a string of `length` bytes starting at `offset`. Skips leading
	/// NUL padding (Avid sometimes pads name records) and stops at the
	/// first internal NUL terminator if present. Decoded via AvidText:
	/// PMR strings are MacRoman (accented project names arrived as
	/// replacement characters when this assumed UTF-8), with valid UTF-8
	/// still honoured as UTF-8.
	QString readString(const QByteArray &data, qint64 offset, qint64 length)
	{
		if (length <= 0 || offset < 0 || offset + length > data.size())
			return {};
		const char *begin = data.constData() + offset;
		const char *end = begin + length;
		while (begin < end && *begin == '\0')
			++begin;
		const char *terminator = std::find(begin, end, '\0');
		return AvidText::decode(begin, terminator - begin);
	}

	// MARK: - Unicode record set

	/// The second record set, when the file has one: the same grammar as the
	/// MBCS set, FILE names in UTF-8 behind two NUL bytes. Each entry's
	/// fileName is replaced by FILE-MOB match. Any shape problem — wrong
	/// count, a record without the MOB prefix, a length past the end — stops
	/// the walk and leaves the MBCS names in place, which decode correctly for
	/// everything MacRoman can hold; `ok` is not affected either way.
	void readUnicodeNames(const QByteArray &data, qint64 pos, quint32 pairCount, QVector<PmrEntry> &entries,
						  const QString &pmrFilePath)
	{
		if (pos + 8 > data.size() || readLE<quint32>(data, pos) != kPmrVersionUnicode)
			return;
		const quint32 count = readLE<quint32>(data, pos + 4);
		pos += 8;
		if (count != pairCount)
		{
			qCDebug(lcPmr) << "unicode record set claims" << count << "pairs, MBCS set has" << pairCount
						   << "— ignoring it" << pmrFilePath;
			return;
		}

		QHash<QString, int> byFileMob;
		byFileMob.reserve(entries.size());
		for (int i = 0; i < entries.size(); ++i)
			byFileMob.insert(entries[i].mobId, i);

		int replaced = 0;
		const quint32 totalRecords = count * 2;
		for (quint32 i = 0; i < totalRecords; ++i)
		{
			if (pos + kMobIdSize + 2 > data.size())
				break;
			const auto *rec = reinterpret_cast<const unsigned char *>(data.constData()) + pos;
			if (rec[0] != 0x06 || rec[1] != 0x0a || rec[2] != 0x2b || rec[3] != 0x34)
				break;
			const QString mobHex = MobId::format(rec);
			pos += kMobIdSize;

			if ((i % 2) == 0)
			{
				const quint16 nameLen = readLE<quint16>(data, pos);
				pos += 2;
				if (nameLen == 0 || nameLen >= kMaxStringLen || pos + nameLen + 2 > data.size())
					break;
				const QString name = readString(data, pos, nameLen); // skips the NUL prefix
				pos += nameLen;
				const quint16 projLen = readLE<quint16>(data, pos);
				pos += 2;
				if (projLen >= kMaxStringLen || pos + projLen > data.size())
					break;
				pos += projLen;

				const auto it = byFileMob.constFind(mobHex);
				if (it != byFileMob.constEnd() && !name.isEmpty() && entries[it.value()].fileName != name)
				{
					entries[it.value()].fileName = name;
					++replaced;
				}
			}
			else
			{
				pos += kMasterTrailerSize;
			}
		}
		if (replaced > 0)
			qCDebug(lcPmr) << replaced << "filename(s) taken from the unicode record set" << pmrFilePath;
	}
} // namespace

// MARK: - Trailer vs modification time

bool PmrParser::trailerMatchesModified(quint32 trailer, const QDateTime &onDisk)
{
	if (trailer == 0 || !onDisk.isValid())
		return false;
	const qint64 mtime = onDisk.toSecsSinceEpoch();
	// Unix seconds, UTC — what MC 2025 writes.
	if (qAbs(qint64(trailer) - mtime) <= 2)
		return true;
	// Mac 1904-epoch seconds in local time — what older MC wrote. Undo
	// this machine's UTC offset at that instant, then the epoch shift.
	constexpr qint64 kMacToUnix = 2082844800; // seconds from 1904-01-01 to 1970-01-01
	const qint64 local = QDateTime::fromSecsSinceEpoch(mtime).offsetFromUtc();
	return qAbs((qint64(trailer) - kMacToUnix - local) - mtime) <= 2;
}

// MARK: - Parse

QVector<PmrEntry> PmrParser::parse(const QString &pmrFilePath, bool *ok)
{
	// Pessimistic default: every early return below is a failure. Flipped to
	// true only by a clean end-to-end walk — a body truncation reaches the
	// bottom of this function with partial entries but still reports false.
	if (ok)
		*ok = false;

	QFile file(pmrFilePath);
	if (!file.open(QIODevice::ReadOnly))
	{
		qCWarning(lcPmr) << "cannot open" << pmrFilePath << file.errorString();
		return {};
	}
	const QByteArray data = file.readAll();

	// OMF-era: the version is peeked (bounds-checked; 0 on a short file)
	// before the size guard only so the guard can use the 8-byte MOB width
	// of a version-2 file — a real one-pair version-2 file is 37 bytes, under
	// the 44 the MXF-era width demands. Every other version keeps the
	// 32-byte width and so takes exactly the checks it always did.
	const quint32 version = readLE<quint32>(data, kVersionOffset);
	const qint64 mobSize = version == kPmrVersionOmf ? OmfUid::kPmrSize : kMobIdSize;

	if (data.size() < kHeaderSize + mobSize)
	{
		qCWarning(lcPmr) << "file too small" << data.size() << pmrFilePath;
		return {};
	}

	// An unknown layout version walked with this record layout returns
	// partial garbage as good data — the FILE desync check only trips
	// probabilistically, after damage is committed. Every PMR surveyed in
	// the wild is version 8 (MXF-era) or version 2 (OMF-era); anything else
	// is refused outright. Failure mode is safe: the folder's files surface
	// as "No database", and Stage-3 UMID/MDB recovery can still claim them.
	// Avid checks a magic word before anything else, and so do we: it
	// rejects a mislabelled or truncated file before the version gate has
	// to interpret arbitrary bytes as a version number.
	const quint32 magic = readLE<quint32>(data, kMagicOffset);
	if (magic != kPmrMagic)
	{
		qCWarning(lcPmr) << "not a PMR (bad magic)" << Qt::hex << magic << pmrFilePath;
		return {};
	}

	// OMF-era: version 2 is accepted beside 8; the only layout difference
	// is the MOB width chosen above.
	if (version != kPmrVersionMxf && version != kPmrVersionOmf)
	{
		qCWarning(lcPmr) << "unsupported PMR version" << version << pmrFilePath;
		return {};
	}

	const quint32 pairCount = readLE<quint32>(data, kPairCountOffset);
	if (pairCount == 0 || pairCount > kMaxPairCount)
	{
		// Corrupt or unfamiliar version; bail rather than walk into
		// gigabytes of garbage interpretation.
		qCWarning(lcPmr) << "implausible pair count" << pairCount << pmrFilePath;
		return {};
	}

	QVector<PmrEntry> entries;
	// Reserve for what the header claims, but never beyond what the file
	// could physically hold: each record needs at least mobSize + 2
	// bytes (the MOB plus its leading u16), so a truncated or hostile file
	// claiming a near-max pairCount can't make us speculatively allocate
	// hundreds of MB for entries that will never materialise.
	const qint64 bodyBytes = data.size() - kHeaderSize;
	const qint64 maxRecordsBySize = bodyBytes > 0 ? bodyBytes / (mobSize + 2) : 0;
	entries.reserve(static_cast<qsizetype>(qMin<qint64>(pairCount, maxRecordsBySize)));

	qint64 pos = kHeaderSize;
	int lastFileIdx = -1;
	const quint32 totalRecords = pairCount * 2;

	// The header's pairCount is a promise about how many records the body
	// holds. Running out of bytes before honouring it means the tail of the
	// file is missing — a partial write, or a read that raced Media
	// Composer's own frequent PMR rewrites. The entries parsed so far are
	// real, but the database can no longer vouch for what it doesn't
	// contain, so this must report ok=false exactly like a desync bail.
	// (Formerly these breaks fell through to ok=true, and every file whose
	// entry lived in the lost tail was mislabelled a verified "No
	// reference" instead of "No database".)
	bool truncated = false;

	for (quint32 i = 0; i < totalRecords; ++i)
	{
		if (pos + mobSize + 2 > data.size())
		{
			truncated = true;
			break;
		}

		// Every real record starts with the Avid MOB prefix 06 0a 2b 34
		// (792/792 records across the surveyed real-world PMRs). Anything
		// else at a record boundary means the stream has desynchronised;
		// bail with what we have rather than commit garbage. MASTER slots
		// previously had no check at all and would silently attach random
		// bytes to the last FILE entry as its masterMobId.
		// OMF-era: a version-2 MOB is 8 bytes of raw identity with no fixed
		// prefix (164/164 fixture records start with arbitrary bytes), so
		// the guard applies to version 8 only; the FILE record's nameLen
		// check below stays the desync detector for both.
		const auto *rec = reinterpret_cast<const unsigned char *>(data.constData()) + pos;
		if (version == kPmrVersionMxf &&
			(rec[0] != 0x06 || rec[1] != 0x0a || rec[2] != 0x2b || rec[3] != 0x34))
		{
			qCWarning(lcPmr) << "record" << i << "lacks the Avid MOB prefix; bailing on"
							 << pmrFilePath;
			return entries;
		}

		// OMF-era: the 8-byte MOB is widened to the same 32-byte form Avid
		// writes into this file's own Unicode set, so the set's FILE-MOB
		// match and every downstream consumer see one id spelling.
		const QString mobHex = version == kPmrVersionOmf ? OmfUid::canonicalFromPmr8(rec) : MobId::format(rec);
		pos += mobSize;

		const quint16 firstWord = readLE<quint16>(data, pos);

		const bool isFile = (i % 2) == 0;

		if (isFile)
		{
			// Sanity check: a real FILE record's first u16 is the
			// filename length, which must be in (0, kMaxStringLen).
			// If it isn't, the stream has desynchronised; bail with
			// what we have rather than walk into garbage.
			if (firstWord == 0 || firstWord >= kMaxStringLen)
			{
				qCWarning(lcPmr) << "expected FILE record at index" << i
								 << "but first u16 =" << firstWord
								 << "looks like MASTER flags; bailing on" << pmrFilePath;
				return entries;
			}

			pos += 2;
			const quint16 nameLen = firstWord;
			if (pos + nameLen + 2 > data.size())
			{
				truncated = true;
				break;
			}

			QString fileName = readString(data, pos, nameLen);
			pos += nameLen;

			const quint16 projLen = readLE<quint16>(data, pos);
			pos += 2;

			QString project;
			if (projLen > 0 && projLen < kMaxStringLen && pos + projLen <= data.size())
			{
				project = readString(data, pos, projLen);
				pos += projLen;
			}

			if (fileName.isEmpty())
				continue;

			PmrEntry entry;
			entry.mobId = mobHex;
			entry.fileName = std::move(fileName);
			entry.project = std::move(project);
			entries.append(std::move(entry));
			lastFileIdx = entries.size() - 1;
		}
		else
		{
			// MASTER record: attach the master clip MOB and the file's modified
			// time to the last FILE record, then drop the index to prevent
			// double-attach.
			const quint32 modified = readLE<quint32>(data, pos);
			pos += kMasterTrailerSize;
			if (lastFileIdx >= 0)
			{
				entries[lastFileIdx].masterMobId = mobHex;
				entries[lastFileIdx].fileModifiedSecs = modified;
				lastFileIdx = -1;
			}
		}
	}

	if (truncated)
		qCWarning(lcPmr) << "body truncated after" << entries.size() << "entries (header claims"
						 << pairCount << "pairs)" << pmrFilePath;
	else
		readUnicodeNames(data, pos, pairCount, entries, pmrFilePath);

	if (ok)
		*ok = !truncated;
	return entries;
}

// MARK: - Lookup table builder

PmrIndex PmrParser::buildFileMap(const QString &pmrFilePath, bool *ok)
{
	PmrIndex map;
	for (const auto &entry : parse(pmrFilePath, ok))
		map[PmrKey::primary(entry.fileName)].append(entry);
	return map;
}