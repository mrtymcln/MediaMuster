#include "bentofile.h"
#include "avidtext.h"
#include "logcategories.h"
#include "mobid.h"

#include <QDebug>
#include <QtEndian>
#include <algorithm>
#include <cstring>

// MARK: - Constants

namespace
{
	constexpr int kLabelSize = 24;
	constexpr int kEntrySize = 24;
	constexpr quint16 kLabelMajor = 1;		///< The only container major version ever seen (88/88 specimens, all 1.0).
	/// OMF-era: the type-name dictionary's id — open() reads its span
	/// together with the property names' (they sit side by side).
	constexpr quint32 kTypeNames = 23;
	constexpr quint32 kPropertyNames = 24;	///< The property-name dictionary's own id.
	constexpr quint16 kFlagImmediate = 1;
	constexpr quint16 kFlagContinued = 2;

	/// OMF-era: the biggest TOC or dictionary span open() will read. A
	/// 5,000-file MDB's TOC is ~5 MB and an OMF file's is ~12 KB; 64 MiB
	/// is far past either, so hitting it means a garbled label, not a big
	/// file — read nothing rather than a bogus 4 GB.
	constexpr qint64 kMaxTailBytes = 64 * 1024 * 1024;

	/// OMF-era: mob index row = 12-byte omfi:UID | u32 objectID | u32 junk.
	constexpr qsizetype kMobIndexUidSize = 12; ///< == OmfUid::kUidSize.
	constexpr qsizetype kMobIndexRowSize = 20;

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

// MARK: - Shared gates

void BentoFile::reset()
{
	m_data.clear();
	m_entries.clear();
	m_propIdByName.clear();
	m_objIdProperty = -1;
	m_tocOffset = 0;
	m_tailFirst = false;
	m_toc.clear();
	m_dict.clear();
	m_dictOffset = 0;
	if (m_file.isOpen())
		m_file.close();
}

bool BentoFile::checkLabel(const char *label, qint64 fileSize, quint32 &tocOff, quint32 &tocLen, quint16 &major,
						   QString &reason)
{
	if (std::memcmp(label, kMagic, sizeof kMagic) != 0)
	{
		reason = QStringLiteral("no Bento label at end of file");
		return false;
	}
	if (label[10] != 'I' || label[11] != 'I')
	{
		reason = QStringLiteral("unsupported byte order tag");
		return false;
	}
	// Major and minor are two u16s (every specimen: 01 00 00 00 = 1.0). A
	// later MAJOR could move the TOC or widen its entries, so the caller
	// gets it to judge; a minor bump keeps the layout and is only logged.
	major = u16At(label + 12);
	if (const quint16 minor = u16At(label + 14); minor != 0)
		qCDebug(lcBento) << "Bento label version" << major << "." << minor;

	tocOff = u32At(label + 16);
	tocLen = u32At(label + 20);
	// The label itself proves the file is whole: the TOC runs up to the label.
	if (qint64(tocOff) + qint64(tocLen) != fileSize - kLabelSize)
	{
		reason = QStringLiteral("TOC arithmetic does not close (off %1 + len %2 != size %3 - 24)")
					 .arg(tocOff)
					 .arg(tocLen)
					 .arg(fileSize);
		return false;
	}
	if (tocLen % kEntrySize != 0)
	{
		reason = QStringLiteral("TOC length %1 is not a multiple of 24").arg(tocLen);
		return false;
	}
	return true;
}

bool BentoFile::indexToc(const char *toc, quint32 tocOff, quint32 tocLen, bool refuseContinued, int &continuedCount,
						 QString &reason)
{
	const int count = int(tocLen / kEntrySize);
	m_entries.clear();
	m_entries.reserve(count);
	continuedCount = 0;
	for (int i = 0; i < count; ++i)
	{
		const char *e = toc + qint64(i) * kEntrySize;
		const quint16 flags = u16At(e + 22);
		Entry ent;
		ent.object = u32At(e);
		ent.property = u32At(e + 4);
		ent.type = u32At(e + 8);
		ent.value = u32At(e + 12);
		ent.length = u32At(e + 16);
		ent.immediate = (flags & kFlagImmediate) != 0;
		ent.tocPos = tocOff + quint32(i) * kEntrySize;
		// A continued value is spread across several entries; reading only
		// the first would hand back a silently truncated value. The entry
		// is kept so it reads EMPTY (value()/bytes() check the flag) and the
		// other entries are unaffected — unless the caller cannot live with
		// a gap and asked for the whole file to be refused instead.
		if (flags & kFlagContinued)
		{
			if (refuseContinued)
			{
				reason = QStringLiteral("TOC entry %1 is a continued value (unsupported)").arg(i);
				return false;
			}
			ent.continued = true;
			++continuedCount;
		}
		m_entries.append(ent);
	}

	// Stable: duplicate (object, property) pairs keep file order, and
	// find() returns the first — the one Avid wrote first.
	std::stable_sort(m_entries.begin(), m_entries.end(),
					 [](const Entry &a, const Entry &b)
					 {
						 return a.object != b.object ? a.object < b.object : a.property < b.property;
					 });
	return true;
}

void BentoFile::indexNames(const char *base, quint32 baseOffset, qint64 baseLen)
{
	// The file's own dictionary: property-name entries name the property
	// whose id is their objectID.
	m_propIdByName.clear();
	for (const Entry &ent : m_entries)
	{
		if (ent.property != kPropertyNames || ent.immediate)
			continue;
		if (qint64(ent.value) + qint64(ent.length) > qint64(m_tocOffset))
			continue;
		const qint64 local = qint64(ent.value) - qint64(baseOffset);
		if (local < 0 || local + qint64(ent.length) > baseLen)
			continue;
		const QByteArrayView name = untilNul(QByteArrayView(base + local, qsizetype(ent.length)));
		if (!name.isEmpty() && !m_propIdByName.contains(name.toByteArray()))
			m_propIdByName.insert(name.toByteArray(), int(ent.object));
	}
	m_objIdProperty = propertyId("OMFI:ObjID");
}

// MARK: - Load

bool BentoFile::load(const QByteArray &data, QString *why)
{
	reset();
	m_bytesRead = data.size();
	auto fail = [&](const QString &reason)
	{
		if (why)
			*why = reason;
		reset();
		return false;
	};

	const qint64 size = data.size();
	if (size < kLabelSize)
		return fail(QStringLiteral("too small for a Bento label (%1 bytes)").arg(size));

	QString reason;
	quint32 tocOff = 0, tocLen = 0;
	quint16 major = 0;
	if (!checkLabel(data.constData() + size - kLabelSize, size, tocOff, tocLen, major, reason))
		return fail(reason);
	// Tolerated, not refused: the arithmetic gates above already proved the
	// TOC is where the label says and is whole, which is all this reader
	// relies on, and the MDB path read unversioned files this way for months.
	if (major != kLabelMajor)
		qCWarning(lcBento) << "Bento label major version" << major << "(expected" << kLabelMajor
						   << ") — reading it as version 1";

	m_data = data;
	m_tocOffset = tocOff;
	int continued = 0;
	if (!indexToc(m_data.constData() + tocOff, tocOff, tocLen, /*refuseContinued=*/false, continued, reason))
		return fail(reason);
	if (continued > 0)
	{
		// One line per file, naming the first so a reader can find it.
		const auto first = std::find_if(m_entries.cbegin(), m_entries.cend(), [](const Entry &e) { return e.continued; });
		qCWarning(lcBento) << continued << "continued TOC entr(ies) read as empty; first is object" << first->object
						   << "property" << first->property;
	}
	indexNames(m_data.constData(), 0, qint64(m_tocOffset));
	return true;
}

// MARK: - OMF-era: tail-first open

bool BentoFile::open(const QString &path, QString *why)
{
	reset();
	m_bytesRead = 0;
	auto fail = [&](const QString &reason)
	{
		if (why)
			*why = reason;
		reset();
		return false;
	};
	// Every disk fetch goes through here so bytesRead() is exact.
	auto fetch = [&](qint64 at, qint64 len, QByteArray &out)
	{
		if (!m_file.seek(at))
			return false;
		out = m_file.read(len);
		m_bytesRead += out.size();
		return out.size() == len;
	};

	m_file.setFileName(path);
	if (!m_file.open(QIODevice::ReadOnly))
		return fail(QStringLiteral("cannot open: %1").arg(m_file.errorString()));

	const qint64 size = m_file.size();
	if (size < kLabelSize)
		return fail(QStringLiteral("too small for a Bento label (%1 bytes)").arg(size));

	QByteArray label;
	if (!fetch(size - kLabelSize, kLabelSize, label))
		return fail(QStringLiteral("short read of the Bento label"));

	QString reason;
	quint32 tocOff = 0, tocLen = 0;
	quint16 major = 0;
	if (!checkLabel(label.constData(), size, tocOff, tocLen, major, reason))
		return fail(reason);
	// OMF-era: a later major could move the TOC or widen its entries, and
	// this mode has no whole buffer to sanity-check values against, so
	// refusing is the honest answer until a specimen says otherwise.
	if (major != kLabelMajor)
		return fail(QStringLiteral("unsupported Bento label version %1").arg(major));
	if (qint64(tocLen) > kMaxTailBytes)
		return fail(QStringLiteral("TOC of %1 bytes is past the %2 MiB cap").arg(tocLen).arg(kMaxTailBytes >> 20));

	m_tocOffset = tocOff;
	m_tailFirst = true;
	if (!fetch(tocOff, tocLen, m_toc))
		return fail(QStringLiteral("short read of the TOC"));
	int continued = 0;
	if (!indexToc(m_toc.constData(), tocOff, tocLen, /*refuseContinued=*/true, continued, reason))
		return fail(reason);

	// The property- and type-name dictionaries are written together, just
	// below the TOC (2–16 KB on every specimen). One read covers the lot;
	// any name outside the span is simply unresolved, as load() does for a
	// name outside the value area.
	qint64 lo = -1, hi = 0;
	for (const Entry &ent : m_entries)
	{
		if ((ent.property != kPropertyNames && ent.property != kTypeNames) || ent.immediate)
			continue;
		if (ent.value >= m_tocOffset)
			continue;
		const qint64 end = qMin<qint64>(qint64(ent.value) + qint64(ent.length), qint64(m_tocOffset));
		lo = lo < 0 ? qint64(ent.value) : qMin(lo, qint64(ent.value));
		hi = qMax(hi, end);
	}
	if (lo >= 0 && hi > lo)
	{
		if (hi - lo > kMaxTailBytes)
			return fail(QStringLiteral("name dictionary of %1 bytes is past the %2 MiB cap")
							.arg(hi - lo)
							.arg(kMaxTailBytes >> 20));
		if (!fetch(lo, hi - lo, m_dict))
			return fail(QStringLiteral("short read of the name dictionary"));
		m_dictOffset = quint32(lo);
	}
	indexNames(m_dict.constData(), m_dictOffset, m_dict.size());
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
	if (property < 0 || m_tailFirst) // OMF-era: no buffer to view after open(); bytes() serves both modes.
		return {};
	const Entry *e = find(object, quint32(property));
	if (!e || e->continued) // a continued value would read truncated; empty is the honest answer
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

QByteArray BentoFile::bytes(quint32 object, int property, qint64 cap) const
{
	if (property < 0)
		return {};
	const Entry *e = find(object, quint32(property));
	if (!e || e->continued) // same rule as value(): never a truncated value
		return {};
	if (e->immediate)
	{
		// The TOC lives in m_data (load) or m_toc (open); tocPos is a file
		// offset in both, so only the base differs.
		const char *toc = m_tailFirst ? m_toc.constData() + (e->tocPos - m_tocOffset) : m_data.constData() + e->tocPos;
		return QByteArray(toc + 12, qMin<qsizetype>(e->length, 4));
	}
	// Same clamp as value(): the two self-describing entries read empty.
	if (e->value >= m_tocOffset)
		return {};
	const qint64 avail = qint64(m_tocOffset) - qint64(e->value);
	const qint64 len = qMin<qint64>(e->length, avail);
	if (len > cap)
		return {}; // the essence blob, or something equally not-metadata
	if (!m_tailFirst)
		return QByteArray(m_data.constData() + e->value, qsizetype(len));

	// OMF-era: serve from the dictionary span when it lies inside it,
	// otherwise one seek+read — a few dozen small reads per file in practice.
	const qint64 dictEnd = qint64(m_dictOffset) + m_dict.size();
	if (qint64(e->value) >= qint64(m_dictOffset) && qint64(e->value) + len <= dictEnd)
		return QByteArray(m_dict.constData() + (qint64(e->value) - qint64(m_dictOffset)), qsizetype(len));
	if (!m_file.isOpen() || !m_file.seek(qint64(e->value)))
		return {};
	QByteArray out = m_file.read(len);
	m_bytesRead += out.size();
	return out.size() == len ? out : QByteArray(); // a short read is "missing", never "partial"
}

QByteArray BentoFile::objectClass(quint32 object) const
{
	return bytes(object, m_objIdProperty);
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
