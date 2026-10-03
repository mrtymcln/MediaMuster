#pragma once

// Typed Bento fixtures shared by the independent Canon source-reader tests.
// Embedded fixtures use absolute file offsets, as Avid's native audio does.

#include <QByteArray>
#include <QMap>
#include <QVector>
#include <QtEndian>

namespace TestCanonBento
{
template <typename T> QByteArray number(T value, bool big = false)
{
	QByteArray bytes(sizeof(T), '\0');
	if (big) qToBigEndian(value, bytes.data());
	else qToLittleEndian(value, bytes.data());
	return bytes;
}

// A small typed fixture writer. Dictionary IDs deliberately differ from the
// real files, so a reader cannot pass by knowing Avid's usual numeric IDs.
class TypedBento
{
public:
	struct Entry
	{
		quint32 object, property, type;
		QByteArray bytes;
		bool immediate = false, continued = false;
		quint32 references = 0;
	};

	int major = 1;
	bool containerBig = false, metadataBig = false;
	QVector<Entry> entries;
	QMap<QByteArray, quint32> properties, types;

	quint32 property(const QByteArray &name)
	{
		if (!properties.contains(name)) properties.insert(name, 8000 + quint32(properties.size()));
		return properties.value(name);
	}
	quint32 type(const QByteArray &name)
	{
		if (!types.contains(name)) types.insert(name, 7000 + quint32(types.size()));
		return types.value(name);
	}
	void add(quint32 object, const QByteArray &name, const QByteArray &typeName,
			 const QByteArray &bytes, bool immediate = false, bool continued = false,
			 quint32 references = 0)
	{
		entries.append({object, property(name), type(typeName), bytes, immediate, continued, references});
	}
	void head(int omfVersion)
	{
		add(1, omfVersion == 1 ? "OMFI:ObjID" : "OMFI:OOBJ:ObjClass", "omfi:ObjectTag", "HEAD", true);
		add(1, omfVersion == 1 ? "OMFI:Version" : "OMFI:HEAD:Version", "omfi:VersionType",
			QByteArray(1, char(omfVersion)) + char(0), true);
		add(1, omfVersion == 1 ? "OMFI:ByteOrder" : "OMFI:HEAD:ByteOrder", "omfi:Short",
			metadataBig ? "MM" : "II", true);
	}
	QByteArray reference(quint32 target, int omfVersion) const
	{
		return number(target, containerBig) + (omfVersion == 1 ? QByteArray(4, '\0') : QByteArray());
	}
	QByteArray build(quint32 baseOffset = 0) const
	{
		QVector<Entry> rows = entries;
		for (auto it = properties.cbegin(); it != properties.cend(); ++it)
			rows.append({it.value(), 24, 21, it.key() + '\0'});
		for (auto it = types.cbegin(); it != types.cend(); ++it)
			rows.append({it.value(), 23, 21, it.key() + '\0'});
		QByteArray payload, toc;
		bool continued = false;
		for (const auto &entry : rows)
		{
			const quint32 offset = baseOffset + quint32(payload.size());
			QByteArray immediate = entry.bytes;
			immediate.resize(4);
			if (!entry.immediate) payload += entry.bytes;
			if (major == 1)
			{
				toc += number(entry.object) + number(entry.property) + number(entry.type);
				toc += entry.immediate ? immediate : number(offset);
				toc += number(quint32(entry.bytes.size())) + number<quint16>(7);
				toc += number<quint16>((entry.immediate ? 1 : 0) | (entry.continued ? 2 : 0));
			}
			else
			{
				if (!continued)
				{
					toc += char(1);
					toc += number(entry.object, containerBig) + number(entry.property, containerBig) + number(entry.type, containerBig);
					toc += char(4);
					toc += number<quint32>(7, containerBig);
					if (entry.references)
					{
						toc += char(15);
						toc += number(entry.references, containerBig);
					}
				}
				if (entry.immediate)
				{
					toc += char(entry.continued ? 14 : 9 + entry.bytes.size());
					if (!entry.bytes.isEmpty()) toc += immediate;
				}
				else
				{
					toc += char(entry.continued ? 6 : 5);
					toc += number(offset, containerBig) + number(quint32(entry.bytes.size()), containerBig);
				}
				continued = entry.continued;
			}
		}
		QByteArray label = QByteArray::fromHex("a4434da5486472d7");
		if (major == 1)
			label += number<quint16>(0) + QByteArray(metadataBig ? "MM" : "II");
		else
			label += number<quint16>(containerBig ? 0 : 0x0101, containerBig) + number<quint16>(64, containerBig);
		label += number<quint16>(quint16(major), containerBig) + number<quint16>(0, containerBig);
		label += number(baseOffset + quint32(payload.size()), containerBig) + number(quint32(toc.size()), containerBig);
		return payload + toc + label;
	}
};

}
