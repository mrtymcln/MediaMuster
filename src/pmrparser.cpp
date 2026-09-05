#include "pmrparser.h"
#include "avidtext.h"
#include "logcategories.h"
#include "mobid.h"
#include "omfuid.h"
#include "pmrkey.h"

#include <QDebug>
#include <QFile>
#include <QtEndian>

#include <algorithm>
#include <utility>

// Recovered from MC 26.8 arm64 libame: LoadPMR (0x4443f0), ReadPmrRec
// (0x4474e0), Read_OMFMobID (0x2359b4), Read_AAFMobID (0x235bf0).
// The magic selects byte order. LoadPMR accepts signed versions < 9;
// ReadPmrRec uses 8-byte OMF IDs for versions <= 7 and 32-byte AAF IDs
// for version 8. Version 1 omits project/master: Avid gets those from its
// loaded MOB database, not from additional bytes in the PMR.
//
// Every record contains file ID, counted filename, then (except v1)
// counted MBCS project and master ID, followed by a full uint32 DTM.
// An optional second section is version 16 + its OWN count. It uses AAF
// IDs and UTF-8 filenames framed by two reserved bytes counted in the
// string length. It replaces the complete record set, including identities,
// projects and timestamps. DumpCache (0x4506f8) can omit unrepresentable
// names from MBCS while retaining them in Unicode, so counts need not agree.
namespace
{
	constexpr quint32 kPmrMagic = 0x000007a9;
	constexpr qint32 kUnicodeVersion = 16;
	constexpr qsizetype kHeaderSize = 12;
	// ReadPmrRec's actual buffer capacities, including the terminator.
	constexpr quint16 kMbcsNameCapacity = 2048;
	constexpr quint16 kProjectCapacity = 64;
	constexpr quint16 kUtf8NameCapacity = 1024;
	constexpr qsizetype kMaxFileNameUnits = 255;

	class Cursor
	{
	public:
		Cursor(const QByteArray &data, bool bigEndian) : m_data(data), m_bigEndian(bigEndian) {}

		[[nodiscard]] qsizetype remaining() const { return m_data.size() - m_pos; }
		[[nodiscard]] qsizetype position() const { return m_pos; }
		[[nodiscard]] bool bigEndian() const { return m_bigEndian; }

		template <typename T>
		bool integer(T &value)
		{
			if (remaining() < qsizetype(sizeof(T)))
				return false;
			const char *bytes = m_data.constData() + m_pos;
			value = m_bigEndian ? qFromBigEndian<T>(bytes) : qFromLittleEndian<T>(bytes);
			m_pos += sizeof(T);
			return true;
		}

		bool bytes(qsizetype length, QByteArrayView &value)
		{
			if (length < 0 || length > remaining())
				return false;
			value = QByteArrayView(m_data.constData() + m_pos, length);
			m_pos += length;
			return true;
		}

	private:
		const QByteArray &m_data;
		qsizetype m_pos = 0;
		bool m_bigEndian;
	};

	QByteArrayView cString(QByteArrayView bytes)
	{
		const auto end = std::find(bytes.begin(), bytes.end(), '\0');
		return bytes.first(end - bytes.begin());
	}

	bool readMbcs(Cursor &cursor, quint16 capacity, QString &value)
	{
		quint16 length = 0;
		if (!cursor.integer(length))
			return false;
		// AStream::ReadString (AvidCore 0x27770) uses -1 for a null string.
		if (length == 0xffff)
		{
			value.clear();
			return true;
		}
		QByteArrayView bytes;
		if (length >= capacity || !cursor.bytes(length, bytes))
			return false;
		bytes = cString(bytes);
		value = AvidText::decode(bytes.data(), bytes.size());
		return true;
	}

	bool readUnicodeName(Cursor &cursor, QString &value)
	{
		quint16 length = 0;
		if (!cursor.integer(length))
			return false;
		if (length == 0xffff)
		{
			value.clear();
			return true;
		}
		QByteArrayView bytes;
		if (length < 2 || length - 2 >= kUtf8NameCapacity || !cursor.bytes(length, bytes))
			return false;
		// AStream::ReadUTF8StringAndConvertToUTF16 (AvidCore 0x27d9c)
		// checks the first reserved byte. The writer emits two zero bytes.
		if (bytes[0] != '\0')
			return false;
		QStringDecoder utf8(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
		value = utf8.decode(cString(bytes.sliced(2)));
		return !utf8.hasError();
	}

	bool readMob(Cursor &cursor, qint32 version, bool master, QString &value)
	{
		const bool omf = version <= 7;
		QByteArrayView bytes;
		if (!cursor.bytes(omf ? OmfUid::kPmrSize : MobId::kRawSize, bytes))
			return false;
		const bool nullId = std::all_of(bytes.begin(), bytes.end(), [](char b)
										{ return b == 0; });
		if (nullId)
		{
			value.clear();
			return master;
		}
		// Read_AAFMobID imposes no SMPTE-prefix test. A complete identity
		// is not a record delimiter; framing comes from counts and lengths.

		QByteArray normalized(bytes.data(), bytes.size());
		if (cursor.bigEndian())
		{
			// AAF's material number is uint32 + uint16 + uint16 + byte[8].
			// OMF's shortened ID is two uint32 words. Normalize those fields
			// to the little-endian spelling already used by PMR/MDB keys.
			if (omf)
			{
				std::reverse(normalized.begin(), normalized.begin() + 4);
				std::reverse(normalized.begin() + 4, normalized.end());
			}
			else
			{
				std::reverse(normalized.begin() + 16, normalized.begin() + 20);
				std::reverse(normalized.begin() + 20, normalized.begin() + 22);
				std::reverse(normalized.begin() + 22, normalized.begin() + 24);
			}
		}
		const auto *raw = reinterpret_cast<const unsigned char *>(normalized.constData());
		value = omf ? OmfUid::canonicalFromPmr8(raw) : MobId::format(raw);
		return true;
	}

	bool readSet(Cursor &cursor, qint32 version, quint32 count, QVector<PmrEntry> &entries)
	{
		// Do not allocate from an untrusted count alone, or multiply it by
		// two. A truncated set can still return the records decoded so far.
		const qsizetype minimumRecord = version == 1 ? 14 : (version <= 7 ? 24 : 72);
		entries.reserve(qMin(qsizetype(count), cursor.remaining() / minimumRecord));
		for (quint32 i = 0; i < count; ++i)
		{
			PmrEntry entry;
			if (!readMob(cursor, version, false, entry.mobId))
				return false;
			const bool nameRead = version == kUnicodeVersion
									  ? readUnicodeName(cursor, entry.fileName)
									  : readMbcs(cursor, kMbcsNameCapacity, entry.fileName);
			if (!nameRead || (version != 1 && !readMbcs(cursor, kProjectCapacity, entry.project)))
				return false;

			// ReadPmrRec consumes invalid/empty names but excludes those rows.
			const bool validName = !entry.fileName.isEmpty() && entry.fileName.size() <= kMaxFileNameUnits;
			QString master;
			quint32 modified = 0;
			if ((version != 1 && !readMob(cursor, version, true, master)) || !cursor.integer(modified))
			{
				// Preserve the historical partial-result API, but never attach a
				// master or an invented zero timestamp before the record completes.
				if (validName)
					entries.append(std::move(entry));
				return false;
			}
			if (validName)
			{
				entry.masterMobId = std::move(master);
				entry.fileModifiedSecs = modified;
				entries.append(std::move(entry));
			}
		}
		return true;
	}
} // namespace

bool PmrParser::trailerMatchesModified(quint32 trailer, const QDateTime &onDisk)
{
	if (trailer == 0 || !onDisk.isValid())
		return false;
	const qint64 mtime = onDisk.toSecsSinceEpoch();
	// Keep the filesystem-granularity tolerance. Avid additionally accepts
	// EXACTLY one hour in either direction (CompareDirectory 0x4537a8–c0,
	// ScanDirectoryCache 0x44c738–4c), not an hour-sized tolerance window.
	// Bounded candidate comparisons also avoid subtraction/abs overflow for
	// extreme QDateTimes. Do not infer any other writing-machine UTC offset.
	const auto matches = [mtime](qint64 candidate)
	{
		return (mtime >= candidate - 2 && mtime <= candidate + 2) ||
			   mtime == candidate - 3600 || mtime == candidate + 3600;
	};
	if (matches(trailer))
		return true;
	constexpr qint64 kMacToUnix = 2082844800;
	const qint64 local = QDateTime::fromSecsSinceEpoch(mtime).offsetFromUtc();
	return matches(qint64(trailer) - kMacToUnix - local);
}

QVector<PmrEntry> PmrParser::parse(const QString &pmrFilePath, bool *ok)
{
	if (ok)
		*ok = false;
	QFile file(pmrFilePath);
	if (!file.open(QIODevice::ReadOnly))
	{
		qCWarning(lcPmr) << "cannot open" << pmrFilePath << file.errorString();
		return {};
	}
	const QByteArray data = file.readAll();
	if (file.error() != QFileDevice::NoError || data.size() < kHeaderSize)
	{
		qCWarning(lcPmr) << "incomplete PMR header" << pmrFilePath;
		return {};
	}
	const quint32 magic = qFromLittleEndian<quint32>(data.constData());
	const bool bigEndian = qFromBigEndian<quint32>(data.constData()) == kPmrMagic;
	if (magic != kPmrMagic && !bigEndian)
	{
		qCWarning(lcPmr) << "not a PMR (bad magic)" << pmrFilePath;
		return {};
	}
	Cursor cursor(data, bigEndian);
	quint32 headerMagic = 0;
	qint32 version = 0;
	quint32 count = 0;
	if (!cursor.integer(headerMagic) || !cursor.integer(version) || !cursor.integer(count) || version >= 9)
	{
		qCWarning(lcPmr) << "unsupported PMR version" << version << pmrFilePath;
		return {};
	}

	QVector<PmrEntry> entries;
	if (!readSet(cursor, version, count, entries))
	{
		qCWarning(lcPmr) << "incomplete or invalid PMR record at byte" << cursor.position() << pmrFilePath;
		return entries;
	}
	if (cursor.remaining() > 0)
	{
		qint32 unicodeVersion = 0;
		quint32 unicodeCount = 0;
		if (!cursor.integer(unicodeVersion) || !cursor.integer(unicodeCount))
		{
			qCWarning(lcPmr) << "incomplete PMR extension header" << pmrFilePath;
			return entries;
		}
		if (unicodeVersion == kUnicodeVersion)
		{
			QVector<PmrEntry> unicodeEntries;
			if (!readSet(cursor, unicodeVersion, unicodeCount, unicodeEntries))
			{
				qCWarning(lcPmr) << "incomplete or invalid PMR Unicode set at byte" << cursor.position() << pmrFilePath;
				return entries;
			}
			// LoadPMR prefers a nonempty Unicode vector in its entirety.
			if (!unicodeEntries.isEmpty())
				entries = std::move(unicodeEntries);
		}
	}
	if (ok)
		*ok = true;
	return entries;
}

PmrIndex PmrParser::buildFileMap(const QString &pmrFilePath, bool *ok)
{
	PmrIndex map;
	for (const auto &entry : parse(pmrFilePath, ok))
		map[PmrKey::primary(entry.fileName)].append(entry);
	return map;
}
