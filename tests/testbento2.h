#pragma once

#include <QByteArray>
#include <QMap>
#include <QVector>
#include <QtEndian>

/// Authored compact-TOC fixtures. Numeric TOC words follow the container
/// order; immediate bytes are opaque. This writer does not use BentoFile.
class Bento2Builder
{
public:
	explicit Bento2Builder(bool bigEndian = false) : big(bigEndian) {}
	quint32 addObject(const char *fourcc)
	{
		const quint32 id = nextObject++;
		set(id, "OMFI:OOBJ:ObjClass", QByteArray(fourcc, 4), true);
		return id;
	}
	quint32 propertyId(const char *name)
	{
		if (!names.contains(name)) names.insert(name, nextProperty++);
		return names.value(name);
	}
	void set(quint32 object, const char *property, const QByteArray &data,
		bool immediate = false, bool continued = false, quint32 references = 0)
	{
		setRaw(object, propertyId(property), data, immediate, continued, references);
	}
	void setRaw(quint32 object, quint32 property, const QByteArray &data,
		bool immediate = false, bool continued = false, quint32 references = 0)
	{
		rows.append({object, property, data, immediate, continued, references});
	}
	QByteArray word(quint32 v) const
	{
		QByteArray b(4, '\0');
		if (big) qToBigEndian(v, b.data()); else qToLittleEndian(v, b.data());
		return b;
	}
	QByteArray half(quint16 v) const
	{
		QByteArray b(2, '\0');
		if (big) qToBigEndian(v, b.data()); else qToLittleEndian(v, b.data());
		return b;
	}
	QByteArray build() const
	{
		QByteArray values, toc;
		bool continuation = false;
		auto append = [&](const Row &r)
		{
			if (!continuation)
			{
				toc += char(1); toc += word(r.object) + word(r.property) + word(r.property == 31 ? 32 : 0);
				toc += char(4); toc += word(1);
				if (r.references) { toc += char(15); toc += word(r.references); }
			}
			if (r.immediate)
			{
				Q_ASSERT(r.data.size() <= 4 && (!r.continued || r.data.size() == 4));
				toc += char(r.continued ? 14 : 9 + r.data.size());
				if (!r.data.isEmpty()) { QByteArray raw = r.data; raw.resize(4); toc += raw; }
			}
			else
			{
				toc += char(r.continued ? 6 : 5);
				toc += word(quint32(values.size())) + word(quint32(r.data.size()));
				values += r.data;
			}
			continuation = r.continued;
		};
		for (const Row &r : rows) append(r);
		for (auto it = names.cbegin(); it != names.cend(); ++it) append({it.value(), 24, it.key() + '\0', false, false, 0});
		QByteArray label = QByteArray::fromHex("a4434da5486472d7");
		label += half(big ? 0 : 0x0101) + half(64) + half(2) + half(0);
		label += word(quint32(values.size())) + word(quint32(toc.size()));
		return values + toc + label;
	}
private:
	struct Row { quint32 object, property; QByteArray data; bool immediate, continued; quint32 references; };
	bool big;
	QMap<QByteArray, quint32> names;
	QVector<Row> rows;
	quint32 nextObject = 100, nextProperty = 1000;
};
