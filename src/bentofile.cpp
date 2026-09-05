#include "bentofile.h"
#include "avidtext.h"
#include "mobid.h"

#include <QtEndian>
#include <algorithm>
#include <cstring>
#include <limits>

namespace
{
	constexpr quint64 kMaxTailBytes = 64 * 1024 * 1024;
	constexpr qsizetype kMaxEntries = qsizetype(kMaxTailBytes / 24);
	constexpr unsigned char kMagic[] = {0xA4, 0x43, 0x4D, 0xA5, 0x48, 0x64, 0x72, 0xD7};
	constexpr qsizetype kMobIndexUidSize = 12, kMobIndexRowSize = 20;
	quint32 u32At(const char *p) { return qFromLittleEndian<quint32>(p); }
	quint16 u16At(const char *p) { return qFromLittleEndian<quint16>(p); }
	quint32 word(const char *p, bool big) { return big ? qFromBigEndian<quint32>(p) : u32At(p); }
	quint16 half(const char *p, bool big) { return big ? qFromBigEndian<quint16>(p) : u16At(p); }
	QByteArrayView untilNul(QByteArrayView v)
	{
		const qsizetype end = v.indexOf('\0');
		return end < 0 ? v : v.first(end);
	}
}

void BentoFile::reset()
{
	m_data.clear();
	m_toc.clear();
	m_dict.clear();
	m_entries.clear();
	m_views.clear();
	m_propIdByName.clear();
	m_tocOffset = m_tocLength = m_dictOffset = 0;
	m_tocBlockSize = m_major = 0;
	m_objIdProperty = m_objClassProperty = -1;
	m_containerBigEndian = m_metadataBigEndian = m_omf2References = m_tailFirst = false;
	if (m_file.isOpen())
		m_file.close();
}

bool BentoFile::checkLabel(QByteArrayView label, qint64 fileSize, QString &reason)
{
	if (label.size() != 24 || std::memcmp(label.data(), kMagic, 8) != 0)
	{
		reason = QStringLiteral("no Bento label at end of file");
		return false;
	}
	const char *p = label.data();
	// Bento1 TOCs are little-endian, including Macintosh OMF payloads. For
	// Bento2 the label's symmetric 0x0101 flag selects little-endian.
	const quint16 leMajor = u16At(p + 12);
	m_containerBigEndian = leMajor != 1 && (quint8(p[8]) & 1) == 0 && (quint8(p[9]) & 1) == 0;
	m_major = half(p + 12, m_containerBigEndian);
	const quint16 minor = half(p + 14, m_containerBigEndian);
	if ((m_major != 1 && m_major != 2) || minor != 0)
	{
		reason = QStringLiteral("unsupported Bento label version %1.%2").arg(m_major).arg(minor);
		return false;
	}
	m_metadataBigEndian = m_major == 1 ? label.sliced(10, 2) == QByteArrayView("MM") : m_containerBigEndian;
	m_tocBlockSize = m_major == 2 ? quint32(half(p + 10, m_containerBigEndian)) * 1024 : 0;
	m_tocOffset = word(p + 16, m_containerBigEndian);
	m_tocLength = word(p + 20, m_containerBigEndian);
	if (m_tocOffset + m_tocLength != quint64(fileSize - 24))
	{
		reason = QStringLiteral("TOC arithmetic does not close (off %1 + len %2 != size %3 - 24)")
					 .arg(m_tocOffset)
					 .arg(m_tocLength)
					 .arg(fileSize);
		return false;
	}
	if (m_tocLength > kMaxTailBytes)
	{
		reason = QStringLiteral("TOC exceeds the 64 MiB metadata limit");
		return false;
	}
	if (m_major == 1 && m_tocLength % 24 != 0)
	{
		reason = QStringLiteral("Bento1 TOC length is not a multiple of 24");
		return false;
	}
	// The toolkit treats a zero block size as one buffer covering this TOC.
	if (m_major == 2 && m_tocBlockSize == 0)
		m_tocBlockSize = quint32((m_tocLength / 1024 + 1) * 1024);
	return true;
}

bool BentoFile::indexToc(QByteArrayView toc, QString &reason)
{
	auto fail = [&](qsizetype pos, const QString &message)
	{
		reason = QStringLiteral("invalid Bento%1 TOC at byte %2: %3").arg(m_major).arg(pos).arg(message);
		return false;
	};
	if (m_major == 1)
	{
		m_entries.reserve(toc.size() / 24);
		for (qsizetype pos = 0; pos < toc.size(); pos += 24)
		{
			const char *p = toc.data() + pos;
			Entry e;
			e.object = u32At(p);
			e.property = u32At(p + 4);
			e.type = u32At(p + 8);
			e.value = u32At(p + 12);
			e.length = u32At(p + 16);
			e.tocPos = m_tocOffset + pos;
			const quint16 flags = u16At(p + 22);
			e.immediate = flags & 1;
			e.continued = flags & 2;
			if (e.immediate)
			{
				if (e.length > 4)
					return fail(pos, QStringLiteral("immediate value longer than four bytes"));
				e.immediateData = QByteArray(p + 12, qsizetype(e.length));
			}
			m_entries.append(e);
		}
	}
	else
	{
		qsizetype pos = 0;
		quint32 object = 0, property = 0, type = 0, refs = 0;
		bool haveObject = false, awaitingValue = false, allowGeneration = false, allowReference = false;
		while (pos < toc.size())
		{
			const qsizetype start = pos;
			const quint8 code = quint8(toc[pos++]);
			if (code == 255)
				continue;
			if (code == 24)
			{
				pos = qMin<qsizetype>(toc.size(), ((start / m_tocBlockSize) + 1) * m_tocBlockSize);
				continue;
			}
			int size = 0;
			switch (code)
			{
			case 1:
				size = 12;
				break;
			case 2:
			case 5:
			case 6:
				size = 8;
				break;
			case 3:
			case 4:
			case 10:
			case 11:
			case 12:
			case 13:
			case 14:
			case 15:
				size = 4;
				break;
			case 7:
			case 8:
				size = 12;
				break;
			case 9:
				break;
			case 25:
			case 26:
				size = 16;
				break;
			default:
				return fail(start, QStringLiteral("unsupported opcode %1").arg(code));
			}
			const qsizetype blockEnd = qMin<qsizetype>(toc.size(), ((start / m_tocBlockSize) + 1) * m_tocBlockSize);
			if (size > blockEnd - pos)
				return fail(start, QStringLiteral("truncated opcode payload or buffer crossing"));
			const char *p = toc.data() + pos;
			pos += size;
			auto w = [&](int n)
			{ return word(p + n * 4, m_containerBigEndian); };
			if (code >= 1 && code <= 3)
			{
				if (awaitingValue)
					return fail(start, QStringLiteral("object/property/type without a value"));
				if (code != 1 && !haveObject)
					return fail(start, QStringLiteral("first entry is not NewObject"));
				if (code == 1)
				{
					object = w(0);
					property = w(1);
					type = w(2);
					haveObject = true;
				}
				if (code == 2)
				{
					property = w(0);
					type = w(1);
				}
				if (code == 3)
					type = w(0);
				refs = 0;
				awaitingValue = allowGeneration = allowReference = true;
				continue;
			}
			if (!haveObject)
				return fail(start, QStringLiteral("value without object/property/type"));
			if (code == 4)
			{
				if (!allowGeneration)
					return fail(start, QStringLiteral("misplaced generation"));
				allowGeneration = false;
				continue;
			}
			if (code == 15)
			{
				if (!allowReference)
					return fail(start, QStringLiteral("misplaced reference list"));
				refs = w(0);
				allowReference = allowGeneration = false;
				continue;
			}
			Entry e;
			e.object = object;
			e.property = property;
			e.type = type;
			e.referenceList = refs;
			e.tocPos = m_tocOffset + start;
			e.immediate = code >= 9 && code <= 14;
			e.continued = code == 6 || code == 8 || code == 14 || code == 26;
			if (e.immediate)
			{
				e.length = code == 14 ? 4 : code - 9;
				e.immediateData = QByteArray(p, qsizetype(e.length));
				e.value = size ? w(0) : 0;
			}
			else if (code == 5 || code == 6)
			{
				e.value = w(0);
				e.length = w(1);
			}
			else
			{
				// Wide TOC numbers are a high word followed by a low word;
				// each word separately uses the container's byte order.
				e.value = (quint64(w(0)) << 32) | w(1);
				e.length = code >= 25 ? (quint64(w(2)) << 32) | w(3) : w(2);
			}
			if (m_entries.size() >= kMaxEntries)
				return fail(start, QStringLiteral("too many TOC value segments"));
			m_entries.append(e);
			awaitingValue = allowGeneration = allowReference = false;
		}
		if (awaitingValue)
			return fail(pos, QStringLiteral("missing final value"));
	}
	// Continued means another segment of THIS value follows, not a missing
	// property. Validate before sorting, while physical adjacency is known.
	for (qsizetype i = 0; i < m_entries.size(); ++i)
	{
		Entry &e = m_entries[i];
		if (!e.continued)
			continue;
		if (i + 1 >= m_entries.size())
			return fail(e.tocPos - m_tocOffset, QStringLiteral("unterminated continued value"));
		const Entry &next = m_entries[i + 1];
		if (e.object != next.object || e.property != next.property || e.type != next.type)
			return fail(e.tocPos - m_tocOffset, QStringLiteral("continued value changes object/property/type"));
	}
	std::stable_sort(m_entries.begin(), m_entries.end(), [](const Entry &a, const Entry &b)
					 { return a.object != b.object ? a.object < b.object : a.property < b.property; });
	for (qsizetype i = 0; i < m_entries.size(); ++i)
		if (m_entries[i].continued)
			m_entries[i].nextSegment = int(i + 1);
	return true;
}

bool BentoFile::fetch(quint64 at, quint64 length, QByteArray &out) const
{
	if (at > quint64(std::numeric_limits<qint64>::max()) || length > quint64(std::numeric_limits<qint64>::max()))
		return false;
	if (!m_file.seek(qint64(at)))
		return false;
	out = m_file.read(qint64(length));
	m_bytesRead += out.size();
	return quint64(out.size()) == length;
}

bool BentoFile::locateLabel(qint64 fileSize, QByteArray &label, quint64 &labelEnd, QString &reason)
{
	auto at = [&](quint64 offset, quint64 length, QByteArray &out)
	{
		if (offset > quint64(fileSize) || length > quint64(fileSize) - offset)
			return false;
		if (m_tailFirst)
			return fetch(offset, length, out);
		out = m_data.sliced(qsizetype(offset), qsizetype(length));
		return true;
	};
	if (!at(fileSize - 24, 24, label))
	{
		reason = QStringLiteral("short read of Bento label");
		return false;
	}
	labelEnd = quint64(fileSize);
	if (std::memcmp(label.constData(), kMagic, 8) == 0)
		return true;
	// Avid's IsOMFIFile also checks an omfi chunk in RIFF/RF64 WAVE.
	// Its stream remains file-relative: the chunk does not rebase offsets.
	QByteArray header;
	if (!at(0, 12, header) || (header.first(4) != "RIFF" && header.first(4) != "RF64") || header.sliced(8, 4) != "WAVE")
	{
		reason = QStringLiteral("no Bento label at end of file");
		return false;
	}
	const bool rf64 = header.first(4) == "RF64";
	quint64 end = quint64(fileSize), dataLength = 0;
	bool haveDataLength = false;
	if (!rf64)
	{
		end = quint64(u32At(header.constData() + 4)) + 8;
		if (end > quint64(fileSize) || end < 12)
		{
			reason = QStringLiteral("invalid RIFF extent");
			return false;
		}
	}
	quint64 pos = 12;
	constexpr int kMaxChunks = 65536;
	for (int chunks = 0; chunks < kMaxChunks && pos <= end && end - pos >= 8; ++chunks)
	{
		if (!at(pos, 8, header))
		{
			reason = QStringLiteral("short read of RIFF chunk");
			return false;
		}
		const QByteArray id = header.first(4);
		quint64 length = u32At(header.constData() + 4);
		const quint64 body = pos + 8;
		if (rf64 && id == "data" && length == 0xffffffffu)
		{
			if (!haveDataLength)
			{
				reason = QStringLiteral("RF64 data has no preceding ds64 length");
				return false;
			}
			length = dataLength;
		}
		if (length > end - body)
		{
			reason = QStringLiteral("RIFF chunk extends beyond container");
			return false;
		}
		const quint64 padded = length + (length & 1);
		if (padded > end - body)
		{
			reason = QStringLiteral("missing RIFF chunk padding");
			return false;
		}
		if (rf64 && id == "ds64")
		{
			QByteArray sizes;
			if (length < 28 || !at(body, 16, sizes))
			{
				reason = QStringLiteral("malformed RF64 ds64 chunk");
				return false;
			}
			const quint64 declared = qFromLittleEndian<quint64>(sizes.constData());
			if (declared > quint64(fileSize) - 8 || declared + 8 < body + padded)
			{
				reason = QStringLiteral("invalid RF64 extent");
				return false;
			}
			end = declared + 8;
			dataLength = qFromLittleEndian<quint64>(sizes.constData() + 8);
			haveDataLength = true;
		}
		if (id == "omfi" && padded >= 24)
		{
			labelEnd = body + padded;
			if (!at(labelEnd - 24, 24, label))
			{
				reason = QStringLiteral("short read of RIFF Bento label");
				return false;
			}
			if (std::memcmp(label.constData(), kMagic, 8) == 0)
				return true;
		}
		pos = body + padded;
	}
	reason = QStringLiteral("no valid Bento label in RIFF omfi chunks");
	return false;
}

bool BentoFile::load(const QByteArray &data, QString *why)
{
	reset();
	m_bytesRead = data.size();
	QString reason;
	if (data.size() < 24)
		reason = QStringLiteral("too small for a Bento label");
	else
	{
		m_data = data;
		QByteArray label;
		quint64 labelEnd = 0;
		if (!locateLabel(data.size(), label, labelEnd, reason) || !checkLabel(label, qint64(labelEnd), reason))
		{
			if (why)
				*why = reason;
			reset();
			return false;
		}
		if (indexToc(QByteArrayView(data).sliced(m_tocOffset, m_tocLength), reason) && indexNames(reason))
		{
			if (why)
				why->clear();
			return true;
		}
	}
	if (why)
		*why = reason;
	reset();
	return false;
}

bool BentoFile::open(const QString &path, QString *why)
{
	reset();
	m_bytesRead = 0;
	QString reason;
	m_file.setFileName(path);
	auto fail = [&]()
	{ if (why) *why = reason; reset(); return false; };
	if (!m_file.open(QIODevice::ReadOnly))
	{
		reason = QStringLiteral("cannot open: %1").arg(m_file.errorString());
		return fail();
	}
	const qint64 size = m_file.size();
	if (size < 24)
	{
		reason = QStringLiteral("too small for a Bento label");
		return fail();
	}
	QByteArray label;
	quint64 labelEnd = 0;
	m_tailFirst = true;
	if (!locateLabel(size, label, labelEnd, reason) || !checkLabel(label, qint64(labelEnd), reason))
		return fail();
	if (!fetch(m_tocOffset, m_tocLength, m_toc))
	{
		reason = QStringLiteral("short read of TOC");
		return fail();
	}
	if (!indexToc(m_toc, reason))
		return fail();
	// Names normally occupy one small span. A scattered dictionary falls
	// back to individually bounded reads instead of copying the intervening essence.
	quint64 lo = m_tocOffset, hi = 0;
	for (const Entry &e : m_entries)
	{
		if ((e.property != 23 && e.property != 24) || e.immediate)
			continue;
		if (e.value > m_tocOffset || e.length > m_tocOffset - e.value)
			continue;
		lo = qMin(lo, e.value);
		hi = qMax(hi, e.value + e.length);
	}
	if (hi > lo && hi - lo <= kMaxTailBytes)
	{
		m_dictOffset = lo;
		if (!fetch(lo, hi - lo, m_dict))
		{
			reason = QStringLiteral("short read of name dictionary");
			return fail();
		}
	}
	if (!indexNames(reason))
		return fail();
	if (why)
		why->clear();
	return true;
}

bool BentoFile::indexNames(QString &reason)
{
	quint64 total = 0;
	for (const Entry &e : m_entries)
	{
		if (e.property != 24)
			continue;
		const ReadResult result = read(e.object, 24);
		if (!result.ok())
		{
			reason = QStringLiteral("unreadable property-name dictionary entry %1").arg(e.object);
			return false;
		}
		total += result.data.size();
		if (total > kMaxTailBytes)
		{
			reason = QStringLiteral("property-name dictionary exceeds 64 MiB");
			return false;
		}
		const QByteArray name = untilNul(result.data).toByteArray();
		if (!name.isEmpty() && !m_propIdByName.contains(name))
			m_propIdByName.insert(name, int(e.object));
	}
	m_objIdProperty = propertyId("OMFI:ObjID");
	m_objClassProperty = propertyId("OMFI:OOBJ:ObjClass");
	QByteArray version = bytes(1, propertyId("OMFI:HEAD:Version"));
	if (version.isEmpty())
		version = bytes(1, propertyId("OMFI:Version"));
	m_omf2References = version.size() == 2 ? quint8(version[0]) == 2 : m_objClassProperty >= 0 && m_objIdProperty < 0;
	QByteArray order = bytes(1, propertyId(m_omf2References ? "OMFI:HEAD:ByteOrder" : "OMFI:ByteOrder"));
	if (order == "MM")
		m_metadataBigEndian = true;
	else if (order == "II")
		m_metadataBigEndian = false;
	return true;
}

int BentoFile::propertyId(QByteArrayView name) const
{
	return m_propIdByName.value(name.toByteArray(), -1);
}

const BentoFile::Entry *BentoFile::find(quint32 object, quint32 property) const
{
	const auto it = std::lower_bound(m_entries.cbegin(), m_entries.cend(), std::make_pair(object, property),
									 [](const Entry &e, const std::pair<quint32, quint32> &key)
									 { return e.object != key.first ? e.object < key.first : e.property < key.second; });
	return it == m_entries.cend() || it->object != object || it->property != property ? nullptr : &*it;
}

bool BentoFile::hasProperty(quint32 object, int property) const
{
	return property >= 0 && find(object, quint32(property));
}

BentoFile::ReadResult BentoFile::read(quint32 object, int property, qint64 cap) const
{
	const Entry *first = property < 0 ? nullptr : find(object, quint32(property));
	if (!first)
		return {{}, ReadStatus::Missing};
	quint64 total = 0;
	for (const Entry *e = first; e; e = e->nextSegment < 0 ? nullptr : &m_entries[e->nextSegment])
	{
		if (e->immediate ? e->length > quint64(e->immediateData.size()) : (e->value > m_tocOffset || e->length > m_tocOffset - e->value))
			return {{}, ReadStatus::Malformed};
		if (cap < 0 || e->length > quint64(cap) - total)
			return {{}, ReadStatus::TooLarge};
		total += e->length;
	}
	QByteArray out;
	out.reserve(qsizetype(total));
	for (const Entry *e = first; e; e = e->nextSegment < 0 ? nullptr : &m_entries[e->nextSegment])
	{
		if (e->immediate)
			out += e->immediateData;
		else if (!m_tailFirst)
			out.append(m_data.constData() + e->value, qsizetype(e->length));
		else if (e->value >= m_dictOffset && e->value - m_dictOffset <= quint64(m_dict.size()) &&
				 e->length <= quint64(m_dict.size()) - (e->value - m_dictOffset))
			out.append(m_dict.constData() + (e->value - m_dictOffset), qsizetype(e->length));
		else
		{
			QByteArray part;
			if (!fetch(e->value, e->length, part))
				return {{}, ReadStatus::IoError};
			out += part;
		}
	}
	return {out, ReadStatus::Ok};
}

QByteArray BentoFile::bytes(quint32 object, int property, qint64 cap) const { return read(object, property, cap).data; }
QByteArrayView BentoFile::value(quint32 object, int property) const
{
	if (property < 0)
		return {};
	const quint64 key = (quint64(object) << 32) | quint32(property);
	auto it = m_views.constFind(key);
	if (it == m_views.cend())
		it = m_views.insert(key, bytes(object, property));
	return QByteArrayView(it.value());
}
QByteArray BentoFile::objectClass(quint32 object) const
{
	return bytes(object, hasProperty(object, m_objClassProperty) ? m_objClassProperty : m_objIdProperty);
}
QVector<quint32> BentoFile::objectsWithProperty(int property) const
{
	QVector<quint32> result;
	for (const Entry &e : m_entries)
		if (property >= 0 && e.property == quint32(property) && (result.isEmpty() || result.last() != e.object))
			result.append(e.object);
	return result;
}

quint32 BentoFile::uintValue(QByteArrayView v) const
{
	if (v.size() == 1)
		return quint8(v[0]);
	if (v.size() == 2)
		return half(v.data(), m_metadataBigEndian);
	if (v.size() == 4)
		return word(v.data(), m_metadataBigEndian);
	return 0;
}
quint64 BentoFile::uint64Value(QByteArrayView v) const
{
	if (v.size() != 8)
		return uintValue(v);
	return m_metadataBigEndian ? qFromBigEndian<quint64>(v.data()) : qFromLittleEndian<quint64>(v.data());
}
qint64 BentoFile::int64Value(QByteArrayView v) const
{
	if (v.size() == 4)
		return qint32(uintValue(v));
	if (v.size() == 2)
		return qint16(uintValue(v));
	return qint64(uint64Value(v));
}
bool BentoFile::rationalValue(QByteArrayView v, qint32 &num, qint32 &den) const
{
	if (v.size() != 8)
		return false;
	num = qint32(uintValue(v.first(4)));
	den = qint32(uintValue(v.sliced(4)));
	return true;
}
quint32 BentoFile::handleValue(QByteArrayView v) const
{
	if (v.size() != (m_omf2References ? 4 : 8))
		return 0;
	// Reference keys are container-order data (OMF marks ObjRef never-swab).
	return word(v.data(), m_containerBigEndian);
}
QVector<quint32> BentoFile::handlesValue(QByteArrayView v) const
{
	QVector<quint32> out;
	if (v.size() < 2)
		return out;
	const quint32 count = half(v.data(), m_metadataBigEndian);
	const int stride = m_omf2References ? 4 : 8;
	if (v.size() != 2 + qsizetype(count) * stride)
		return out;
	for (quint32 i = 0; i < count; ++i)
	{
		const quint32 id = handleValue(v.sliced(2 + i * stride, stride));
		if (id)
			out.append(id);
	}
	return out;
}
quint32 BentoFile::mappedReference(const Entry &e, QByteArrayView raw, ReadStatus &status) const
{
	if (raw.size() != 4)
	{
		status = ReadStatus::Malformed;
		return 0;
	}
	const quint32 key = word(raw.data(), m_containerBigEndian);
	if (!key)
		return 0;
	auto existingObject = [&](quint32 id)
	{
		const auto it = std::lower_bound(m_entries.cbegin(), m_entries.cend(), id,
										 [](const Entry &candidate, quint32 target)
										 { return candidate.object < target; });
		if (it == m_entries.cend() || it->object != id)
		{
			status = ReadStatus::Malformed;
			return quint32(0);
		}
		return id;
	};
	if (m_major == 1 || e.referenceList == 0)
		return existingObject(key);
	const Entry *recording = find(e.referenceList, 31);
	if (!recording || recording->type != 32)
	{
		status = ReadStatus::Malformed;
		return 0;
	}
	const ReadResult table = read(e.referenceList, 31);
	if (!table.ok())
	{
		status = table.status == ReadStatus::Missing ? ReadStatus::Malformed : table.status;
		return 0;
	}
	if (table.data.size() % 8 != 0)
	{
		status = ReadStatus::Malformed;
		return 0;
	}
	for (qsizetype pos = 0; pos < table.data.size(); pos += 8)
		if (word(table.data.constData() + pos, m_containerBigEndian) == key)
			return existingObject(word(table.data.constData() + pos + 4, m_containerBigEndian));
	status = ReadStatus::Malformed;
	return 0;
}
quint32 BentoFile::ref(quint32 object, int property, ReadStatus *status) const
{
	const ReadResult r = read(object, property);
	ReadStatus s = r.status;
	quint32 result = 0;
	if (r.ok())
	{
		if (r.data.size() != (m_omf2References ? 4 : 8))
			s = ReadStatus::Malformed;
		else
			result = mappedReference(*find(object, quint32(property)), QByteArrayView(r.data).first(4), s);
	}
	if (status)
		*status = s;
	return result;
}
QVector<quint32> BentoFile::refs(quint32 object, int property, ReadStatus *status) const
{
	const ReadResult r = read(object, property);
	ReadStatus s = r.status;
	QVector<quint32> result;
	if (r.ok())
	{
		const int stride = m_omf2References ? 4 : 8;
		const quint32 count = r.data.size() >= 2 ? half(r.data.constData(), m_metadataBigEndian) : 0;
		if (r.data.size() != 2 + qsizetype(count) * stride)
			s = ReadStatus::Malformed;
		else
			for (quint32 i = 0; i < count; ++i)
			{
				const quint32 id = mappedReference(*find(object, quint32(property)), QByteArrayView(r.data).sliced(2 + i * stride, 4), s);
				if (s != ReadStatus::Ok)
				{
					result.clear();
					break;
				}
				if (id)
					result.append(id);
			}
	}
	if (status)
		*status = s;
	return result;
}
quint32 BentoFile::uint(QByteArrayView v)
{
	quint32 n = 0;
	const qsizetype len = qMin<qsizetype>(v.size(), 4);
	for (qsizetype i = 0; i < len; ++i)
		n |= quint32(static_cast<unsigned char>(v[i])) << (8 * i);
	return n;
}

bool BentoFile::rational(QByteArrayView v, qint32 &num, qint32 &den)
{
	if (v.size() != 8)
		return false;
	num = qint32(u32At(v.data()));
	den = qint32(u32At(v.data() + 4));
	return true;
}

quint32 BentoFile::handle(QByteArrayView v)
{
	if (v.size() != 8)
		return 0;
	if (u32At(v.data() + 4) != 0)
		return 0;
	return u32At(v.data());
}

QVector<quint32> BentoFile::handles(QByteArrayView v)
{
	QVector<quint32> out;
	if (v.size() < 2)
		return out;
	const quint32 count = u16At(v.data());
	if (qint64(v.size()) < 2 + qint64(count) * 8)
		return out; // declared more than it holds: not a handle array
	out.reserve(int(count));
	for (quint32 i = 0; i < count; ++i)
	{
		const quint32 id = handle(v.sliced(2 + qsizetype(i) * 8, 8));
		if (id != 0)
			out.append(id);
	}
	return out;
}

QString BentoFile::string(QByteArrayView v)
{
	const QByteArrayView t = untilNul(v);
	return AvidText::decode(t.data(), t.size());
}

QString BentoFile::utf8String(QByteArrayView v)
{
	const QByteArrayView t = untilNul(v);
	return QString::fromUtf8(t.data(), t.size());
}

QString BentoFile::mobIdHex(QByteArrayView v)
{
	if (v.size() < MobId::kRawSize)
		return {};
	return MobId::format(reinterpret_cast<const unsigned char *>(v.data()));
}

// MARK: - OMF-era: mob index

QVector<BentoFile::MobIndexEntry> BentoFile::mobIndex(QByteArrayView v)
{
	QVector<MobIndexEntry> out;
	if (v.size() < 2)
		return out;
	const quint32 count = u16At(v.data());
	if (qint64(v.size()) < 2 + qint64(count) * kMobIndexRowSize)
		return out; // declared more than it holds: not a mob index
	out.reserve(int(count));
	for (quint32 i = 0; i < count; ++i)
	{
		const char *row = v.data() + 2 + qsizetype(i) * kMobIndexRowSize;
		MobIndexEntry e;
		e.uid = QByteArray(row, kMobIndexUidSize);
		e.object = u32At(row + kMobIndexUidSize);
		if (e.object != 0)
			out.append(e);
	}
	return out;
}
