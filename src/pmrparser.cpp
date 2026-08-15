#include "pmrparser.h"
#include "avidtext.h"
#include "logging.h"
#include "mobid.h"
#include "pmrkey.h"

#include <QFile>
#include <QDebug>
#include <QtEndian>

// `msmFMID.pmr` is the Persistent Media Record Avid writes into every
// `Avid MediaFiles/MXF/<n>/` folder. It's a flat filename-to-MOB index
// and the fastest way to map an essence file to its source/master MOBs
// without walking the MXF headers. All integers are little-endian:
//
//   Header (12 bytes) — Avid's own field names, from AMSM::LoadPMR:
//     uint32  MAGIC   (= 0x000007A9)
//     uint32  VERSION (= 8)
//     uint32  numMobs
//
//   Body (pairCount × 2 records, alternating FILE then MASTER):
//     FILE: 32-byte MOB | uint16 nameLen | name (UTF-8)
//                       | uint16 projLen | project (UTF-8)
//     MASTER: 32-byte MOB | 4 bytes (flags/refs, purpose unknown)
//
// A MASTER record's MOB is the master clip's Master MOB. Relative
// tracks (V01/A01/A02 etc.) all share it, which is how we group
// relatives in the rebalancer and bin filter.

namespace
{
	constexpr qint64 kHeaderSize = 12;
	constexpr qint64 kMagicOffset = 0;
	/// Avid's own constant; every PMR in the wild starts with it.
	constexpr quint32 kPmrMagic = 0x000007A9;
	constexpr qint64 kVersionOffset = 4;
	constexpr qint64 kPairCountOffset = 8;
	constexpr quint32 kPmrVersion = 8;
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
} // namespace

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

	if (data.size() < kHeaderSize + kMobIdSize)
	{
		qCWarning(lcPmr) << "file too small" << data.size() << pmrFilePath;
		return {};
	}

	// An unknown layout version walked with this record layout returns
	// partial garbage as good data — the FILE desync check only trips
	// probabilistically, after damage is committed. Every PMR surveyed in
	// the wild is version 8; anything else is refused outright. Failure
	// mode is safe: the folder's files surface as "No database", and
	// Stage-3 UMID/MDB recovery can still claim them.
	// Avid checks a magic word before anything else, and so do we: it
	// rejects a mislabelled or truncated file before the version gate has
	// to interpret arbitrary bytes as a version number.
	const quint32 magic = readLE<quint32>(data, kMagicOffset);
	if (magic != kPmrMagic)
	{
		qCWarning(lcPmr) << "not a PMR (bad magic)" << Qt::hex << magic << pmrFilePath;
		return {};
	}

	const quint32 version = readLE<quint32>(data, kVersionOffset);
	if (version != kPmrVersion)
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
	// could physically hold: each record needs at least kMobIdSize + 2
	// bytes (the MOB plus its leading u16), so a truncated or hostile file
	// claiming a near-max pairCount can't make us speculatively allocate
	// hundreds of MB for entries that will never materialise.
	const qint64 bodyBytes = data.size() - kHeaderSize;
	const qint64 maxRecordsBySize = bodyBytes > 0 ? bodyBytes / (kMobIdSize + 2) : 0;
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
		if (pos + kMobIdSize + 2 > data.size())
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
		const auto *rec = reinterpret_cast<const unsigned char *>(data.constData()) + pos;
		if (rec[0] != 0x06 || rec[1] != 0x0a || rec[2] != 0x2b || rec[3] != 0x34)
		{
			qCWarning(lcPmr) << "record" << i << "lacks the Avid MOB prefix; bailing on"
							 << pmrFilePath;
			return entries;
		}

		const QString mobHex = MobId::format(rec);
		pos += kMobIdSize;

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
			// MASTER record: attach the master clip MOB to the last FILE
			// record, then drop the index to prevent double-attach.
			pos += kMasterTrailerSize;
			if (lastFileIdx >= 0)
			{
				entries[lastFileIdx].masterMobId = mobHex;
				lastFileIdx = -1;
			}
		}
	}

	if (truncated)
		qCWarning(lcPmr) << "body truncated after" << entries.size() << "entries (header claims"
						 << pairCount << "pairs)" << pmrFilePath;

	if (ok)
		*ok = !truncated;
	return entries;
}

// MARK: - Lookup table builder

PmrParser::ProjectMaps PmrParser::buildFileMapWithFallback(const QString &pmrFilePath, bool *ok)
{
	ProjectMaps maps;
	for (const auto &entry : parse(pmrFilePath, ok))
	{
		const QString key = PmrKey::primary(entry.fileName);
		maps.primary[key].append(entry);
		maps.fallback[PmrKey::fallback(key)].append(entry);
	}
	return maps;
}