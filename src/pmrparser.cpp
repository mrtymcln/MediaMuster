#include "pmrparser.h"
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
//   Header (12 bytes):
//     uint32  sizeHint
//     uint32  version (= 8)
//     uint32  pairCount
//
//   Body (pairCount × 2 records, alternating FILE then COMP):
//     FILE: 32-byte MOB | uint16 nameLen | name (UTF-8)
//                       | uint16 projLen | project (UTF-8)
//     COMP: 32-byte MOB | 4 bytes (flags/refs — purpose unknown)
//
// The COMP MOB is the master clip. Relative tracks (V01/A01/A02 etc.)
// all reference the same COMP, which is how we group relatives in the
// rebalancer and bin filter.

namespace
{
constexpr qint64 kHeaderSize = 12;
constexpr qint64 kMobIdSize = MobId::kRawSize;
constexpr qint64 kCompTrailerSize = 4;
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

/// Read a UTF-8 string of `length` bytes starting at `offset`.
/// Skips leading NUL padding (Avid sometimes pads name records)
/// and stops at the first internal NUL terminator if present.
QString readString(const QByteArray &data, qint64 offset, qint64 length)
{
	if (length <= 0 || offset < 0 || offset + length > data.size())
		return {};
	const char *begin = data.constData() + offset;
	const char *end = begin + length;
	while (begin < end && *begin == '\0')
		++begin;
	const char *terminator = std::find(begin, end, '\0');
	return QString::fromUtf8(begin, terminator - begin);
}
} // namespace

// MARK: - Parse

QVector<PmrEntry> PmrParser::parse(const QString &pmrFilePath)
{
	QFile file(pmrFilePath);
	if (!file.open(QIODevice::ReadOnly))
	{
		qWarning() << "PMR: cannot open" << pmrFilePath;
		return {};
	}
	const QByteArray data = file.readAll();

	if (data.size() < kHeaderSize + kMobIdSize)
	{
		qWarning() << "PMR: file too small" << data.size() << pmrFilePath;
		return {};
	}

	const quint32 pairCount = readLE<quint32>(data, 8);
	if (pairCount == 0 || pairCount > kMaxPairCount)
	{
		// Corrupt or unfamiliar version — bail rather than walk into
		// gigabytes of garbage interpretation.
		qWarning() << "PMR: implausible pair count" << pairCount << pmrFilePath;
		return {};
	}

	QVector<PmrEntry> entries;
	entries.reserve(pairCount);

	qint64 pos = kHeaderSize;
	int lastFileIdx = -1;
	const quint32 totalRecords = pairCount * 2;

	for (quint32 i = 0; i < totalRecords; ++i)
	{
		if (pos + kMobIdSize + 2 > data.size())
			break;

		const QString mobHex =
		    MobId::format(reinterpret_cast<const unsigned char *>(data.constData() + pos));
		pos += kMobIdSize;

		const quint16 firstWord = readLE<quint16>(data, pos);

		// FILE records lead with a 1..1023-byte filename length; COMP
		// records lead with flag bytes outside that range. Discriminate
		// by the value of the first word.
		const bool isFile = firstWord > 0 && firstWord < kMaxStringLen;

		if (isFile)
		{
			pos += 2;
			const quint16 nameLen = firstWord;
			if (pos + nameLen + 2 > data.size())
				break;

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
			// COMP record — attach master clip MOB to the last FILE
			// record, then drop the index to prevent double-attach.
			pos += kCompTrailerSize;
			if (lastFileIdx >= 0)
			{
				entries[lastFileIdx].compositionMobId = mobHex;
				lastFileIdx = -1;
			}
		}
	}

	return entries;
}

// MARK: - Lookup table builder

PmrParser::ProjectMaps PmrParser::buildFileMapWithFallback(const QString &pmrFilePath)
{
	ProjectMaps maps;
	for (const auto &entry : parse(pmrFilePath))
	{
		const QString key = PmrKey::primary(entry.fileName);
		maps.primary[key].append(entry);
		maps.fallback[PmrKey::fallback(key)].append(entry);
	}
	return maps;
}