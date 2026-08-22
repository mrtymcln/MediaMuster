#include "bentofile.h"
#include "avidtext.h"
#include "mobid.h"

#include <QtEndian>
#include <algorithm>
#include <cstring>

// MARK: - Constants

namespace
{
	constexpr int kLabelSize = 24;
	constexpr int kEntrySize = 24;
	constexpr quint32 kPropertyNames = 24; ///< The property-name dictionary's own id.

	/// Bento's label magic, as the bytes appear in the file.
	constexpr unsigned char kMagic[8] = {0xA4, 0x43, 0x4D, 0xA5, 0x48, 0x64, 0x72, 0xD7};

	/// Little-endian readers over a raw pointer. Callers bounds-check.
	inline quint32 u32At(const char *p)
	{
		return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(p));
	}
	inline quint16 u16At(const char *p)
	{
		return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(p));
	}

	/// Text up to the first NUL (Bento strings count their terminator).
	inline QByteArrayView untilNul(QByteArrayView v)
	{
		const qsizetype nul = v.indexOf('\0');
		return nul < 0 ? v : v.first(nul);
	}
} // namespace

// MARK: - Load

bool BentoFile::load(const QByteArray &data, QString *why)
{
	auto fail = [&](const QString &reason)
	{
		if (why)
			*why = reason;
		m_data.clear();
		m_entries.clear();
		m_propIdByName.clear();
		m_objIdProperty = -1;
		m_tocOffset = 0;
		return false;
	};

	const qint64 size = data.size();
	if (size < kLabelSize)
		return fail(QStringLiteral("too small for a Bento label (%1 bytes)").arg(size));

	const char *label = data.constData() + size - kLabelSize;
	if (std::memcmp(label, kMagic, sizeof kMagic) != 0)
		return fail(QStringLiteral("no Bento label at end of file"));
	if (label[10] != 'I' || label[11] != 'I')
		return fail(QStringLiteral("unsupported byte order tag"));

	const quint32 tocOff = u32At(label + 16);
	const quint32 tocLen = u32At(label + 20);
	// The label itself proves the file is whole: the TOC runs up to the label.
	if (qint64(tocOff) + qint64(tocLen) != size - kLabelSize)
		return fail(QStringLiteral("TOC arithmetic does not close (off %1 + len %2 != size %3 - 24)")
						.arg(tocOff)
						.arg(tocLen)
						.arg(size));
	if (tocLen % kEntrySize != 0)
		return fail(QStringLiteral("TOC length %1 is not a multiple of 24").arg(tocLen));

	m_data = data;
	m_tocOffset = tocOff;

	const int count = int(tocLen / kEntrySize);
	m_entries.clear();
	m_entries.reserve(count);
	const char *toc = m_data.constData() + tocOff;
	for (int i = 0; i < count; ++i)
	{
		const char *e = toc + qint64(i) * kEntrySize;
		Entry ent;
		ent.object = u32At(e);
		ent.property = u32At(e + 4);
		ent.type = u32At(e + 8);
		ent.value = u32At(e + 12);
		ent.length = u32At(e + 16);
		ent.immediate = (u16At(e + 22) & 1) != 0;
		ent.tocPos = tocOff + quint32(i) * kEntrySize;
		m_entries.append(ent);
	}

	// Stable: duplicate (object, property) pairs keep file order, and
	// find() returns the first — the one Avid wrote first.
	std::stable_sort(m_entries.begin(), m_entries.end(),
					 [](const Entry &a, const Entry &b)
					 {
						 return a.object != b.object ? a.object < b.object : a.property < b.property;
					 });

	// The file's own dictionary: property-name entries name the property
	// whose id is their objectID.
	m_propIdByName.clear();
	for (const Entry &ent : m_entries)
	{
		if (ent.property != kPropertyNames || ent.immediate)
			continue;
		if (qint64(ent.value) + qint64(ent.length) > qint64(m_tocOffset))
			continue;
		const QByteArrayView name =
			untilNul(QByteArrayView(m_data.constData() + ent.value, qsizetype(ent.length)));
		if (!name.isEmpty() && !m_propIdByName.contains(name.toByteArray()))
			m_propIdByName.insert(name.toByteArray(), int(ent.object));
	}
	m_objIdProperty = propertyId("OMFI:ObjID");
	return true;
}

// MARK: - Lookup

int BentoFile::propertyId(QByteArrayView name) const
{
	const auto it = m_propIdByName.constFind(name.toByteArray());
	return it == m_propIdByName.constEnd() ? -1 : it.value();
}

const BentoFile::Entry *BentoFile::find(quint32 object, quint32 property) const
{
	const auto it = std::lower_bound(m_entries.cbegin(), m_entries.cend(), std::make_pair(object, property),
									 [](const Entry &e, const std::pair<quint32, quint32> &key)
									 {
										 return e.object != key.first ? e.object < key.first
																	  : e.property < key.second;
									 });
	if (it == m_entries.cend() || it->object != object || it->property != property)
		return nullptr;
	return &*it;
}

QByteArrayView BentoFile::value(quint32 object, int property) const
{
	if (property < 0)
		return {};
	const Entry *e = find(object, quint32(property));
	if (!e)
		return {};
	if (e->immediate)
	{
		// The four raw bytes of the `value` field, file order, as written.
		const qsizetype len = qMin<qsizetype>(e->length, 4);
		return QByteArrayView(m_data.constData() + e->tocPos + 12, len);
	}
	// Offset into the value area; clamp rather than trust. The container
	// describes itself with two entries that point past the values (the TOC,
	// the whole file) — those read empty, which is the right answer.
	if (e->value >= m_tocOffset)
		return {};
	const qint64 avail = qint64(m_tocOffset) - qint64(e->value);
	const qsizetype len = qsizetype(qMin<qint64>(e->length, avail));
	return QByteArrayView(m_data.constData() + e->value, len);
}

QByteArray BentoFile::objectClass(quint32 object) const
{
	return value(object, m_objIdProperty).toByteArray();
}

QVector<quint32> BentoFile::objectsWithProperty(int property) const
{
	QVector<quint32> out;
	if (property < 0)
		return out;
	quint32 last = 0;
	bool any = false;
	for (const Entry &e : m_entries) // ascending by object, so dedupe is a compare
	{
		if (e.property != quint32(property))
			continue;
		if (any && e.object == last)
			continue;
		out.append(e.object);
		last = e.object;
		any = true;
	}
	return out;
}

// MARK: - Typed readers

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
