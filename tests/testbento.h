#pragma once

// A tiny in-memory Bento container writer for tests — the inverse of
// src/bentofile.h, built to the same layout Avid writes msmMMOB.mdb in:
// values first, then the property-name dictionary entries, then the TOC,
// then the 24-byte label. Shared by tst_bentofile, tst_mdbparser and
// tst_scanner, which is what earns it a header of its own.

#include <QByteArray>
#include <QHash>
#include <QVector>
#include <QtEndian>

class BentoBuilder
{
public:
	/// A fresh object carrying OMFI:ObjID = `fourcc` (4 bytes, immediate).
	quint32 addObject(const char *fourcc)
	{
		const quint32 id = m_nextObject++;
		setImmediate(id, "OMFI:ObjID", QByteArray(fourcc, 4));
		return id;
	}

	/// `bytes` stored in the value area. Strings must include their NUL.
	void set(quint32 object, const char *property, const QByteArray &bytes)
	{
		m_entries.append({object, propertyId(property), bytes, false});
	}

	/// `bytes` (≤ 4) stored in the TOC entry's value field, file order.
	void setImmediate(quint32 object, const char *property, const QByteArray &bytes)
	{
		Q_ASSERT(bytes.size() <= 4);
		m_entries.append({object, propertyId(property), bytes, true});
	}

	void setU32(quint32 object, const char *property, quint32 v) { setImmediate(object, property, le32(v)); }
	void setU16(quint32 object, const char *property, quint16 v)
	{
		setImmediate(object, property, le32(v).left(2));
	}
	void setString(quint32 object, const char *property, const QByteArray &text)
	{
		set(object, property, text + '\0');
	}
	void setRational(quint32 object, const char *property, qint32 num, qint32 den)
	{
		set(object, property, le32(quint32(num)) + le32(quint32(den)));
	}
	void setHandle(quint32 object, const char *property, quint32 target)
	{
		set(object, property, le32(target) + le32(0));
	}
	void setHandles(quint32 object, const char *property, const QVector<quint32> &targets)
	{
		QByteArray b = le32(quint32(targets.size())).left(2);
		for (quint32 t : targets)
			b += le32(t) + le32(0);
		set(object, property, b);
	}

	/// Serialise. `tocTrailer` appends raw bytes after the TOC but before the
	/// label — never valid, used only to break the arithmetic on purpose.
	QByteArray build(const QByteArray &tocTrailer = {}) const
	{
		QByteArray values;
		struct Toc
		{
			quint32 obj, prop, value, len;
			bool imm;
		};
		QVector<Toc> toc;

		// Value area, in entry order (file order = first-wins order).
		for (const auto &e : m_entries)
		{
			if (e.immediate)
			{
				QByteArray v = e.bytes;
				v.resize(4); // zero-pad the value field
				toc.append({e.object, e.property, qFromLittleEndian<quint32>(
														reinterpret_cast<const uchar *>(v.constData())),
							quint32(e.bytes.size()), true});
			}
			else
			{
				toc.append({e.object, e.property, quint32(values.size()), quint32(e.bytes.size()), false});
				values += e.bytes;
			}
		}
		// The dictionary: one name entry per property, objectID = the id.
		for (auto it = m_propIds.cbegin(); it != m_propIds.cend(); ++it)
		{
			toc.append({it.value(), 24, quint32(values.size()), quint32(it.key().size() + 1), false});
			values += it.key() + '\0';
		}

		QByteArray out = values;
		const quint32 tocOff = quint32(out.size());
		for (const Toc &t : toc)
		{
			out += le32(t.obj) + le32(t.prop) + le32(0) + le32(t.value) + le32(t.len);
			out += le32(1).left(2);						  // generation
			out += le32(t.imm ? 1u : 0u).left(2);		  // flags
		}
		const quint32 tocLen = quint32(out.size()) - tocOff;
		out += tocTrailer;
		out += label(tocOff, tocLen);
		return out;
	}

	static QByteArray label(quint32 tocOff, quint32 tocLen)
	{
		QByteArray l = QByteArray::fromHex("a4434da5486472d7");
		l += le32(0).left(2); // flags
		l += "II";
		l += le32(1);		  // version
		l += le32(tocOff) + le32(tocLen);
		return l;
	}

	static QByteArray le32(quint32 v)
	{
		QByteArray b(4, '\0');
		qToLittleEndian<quint32>(v, reinterpret_cast<uchar *>(b.data()));
		return b;
	}

private:
	struct Pending
	{
		quint32 object;
		quint32 property;
		QByteArray bytes;
		bool immediate;
	};

	quint32 propertyId(const char *name)
	{
		auto it = m_propIds.find(name);
		if (it == m_propIds.end())
			it = m_propIds.insert(name, m_nextProperty++);
		return it.value();
	}

	QVector<Pending> m_entries;
	QHash<QByteArray, quint32> m_propIds;
	quint32 m_nextObject = 68000;	///< Avid's objects start around here.
	quint32 m_nextProperty = 66000; ///< ...and its property ids around here.
};
