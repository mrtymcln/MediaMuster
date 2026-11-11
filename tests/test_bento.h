#pragma once

// Typed Bento fixtures shared by the independent MediaEngine source-reader tests.
// Embedded fixtures use absolute file offsets, as Avid's native audio does.

#include <QByteArray>
#include <QMap>
#include <QPair>
#include <QVector>
#include <QtEndian>

// MARK: - Bento fixture builder

namespace TestMediaEngineBento
{
	template <typename T>
	QByteArray number(T value, bool big = false)
	{
		QByteArray bytes(sizeof(T), '\0');
		if (big)
			qToBigEndian(value, bytes.data());
		else
			qToLittleEndian(value, bytes.data());
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
		};

		bool metadataBig = false;
		QVector<Entry> entries;
		QMap<QByteArray, quint32> properties, types;

		quint32 property(const QByteArray &name)
		{
			if (!properties.contains(name))
				properties.insert(name, 8000 + quint32(properties.size()));
			return properties.value(name);
		}
		quint32 type(const QByteArray &name)
		{
			if (!types.contains(name))
				types.insert(name, 7000 + quint32(types.size()));
			return types.value(name);
		}
		void add(quint32 object, const QByteArray &name, const QByteArray &typeName,
				 const QByteArray &bytes, bool immediate = false, bool continued = false)
		{
			entries.append({object, property(name), type(typeName), bytes, immediate, continued});
		}
		void head()
		{
			add(1, "OMFI:ObjID", "omfi:ObjectTag", "HEAD", true);
			add(1, "OMFI:Version", "omfi:VersionType", QByteArray::fromHex("0100"), true);
			add(1, "OMFI:ByteOrder", "omfi:Short", metadataBig ? "MM" : "II", true);
		}
		QByteArray reference(quint32 target) const
		{
			return number(target) + QByteArray(4, '\0');
		}
		void referenceArray(quint32 owner, const QByteArray &name, const QVector<quint32> &targets)
		{
			QByteArray bytes = number(quint16(targets.size()), metadataBig);
			for (const auto target : targets)
				bytes += reference(target);
			add(owner, name, "omfi:ObjRefArray", bytes);
		}
		void mobIndex(quint32 owner, const QByteArray &name, const QVector<QPair<quint32, QByteArray>> &targets)
		{
			QByteArray bytes = number(quint16(targets.size()), metadataBig);
			for (const auto &target : targets)
			{
				Q_ASSERT(target.second.size() == 12);
				bytes += target.second;
				bytes += reference(target.first);
			}
			add(owner, name, "omfi:MobIndex", bytes);
		}
		QByteArray build(quint32 baseOffset = 0) const
		{
			QVector<Entry> rows = entries;
			for (auto it = properties.cbegin(); it != properties.cend(); ++it)
				rows.append({it.value(), 24, 21, it.key() + '\0'});
			for (auto it = types.cbegin(); it != types.cend(); ++it)
				rows.append({it.value(), 23, 21, it.key() + '\0'});
			QByteArray payload, toc;
			for (const auto &entry : rows)
			{
				const quint32 offset = baseOffset + quint32(payload.size());
				QByteArray immediate = entry.bytes;
				immediate.resize(4);
				if (!entry.immediate)
					payload += entry.bytes;
				toc += number(entry.object) + number(entry.property) + number(entry.type);
				toc += entry.immediate ? immediate : number(offset);
				toc += number(quint32(entry.bytes.size())) + number<quint16>(7);
				toc += number<quint16>((entry.immediate ? 1 : 0) | (entry.continued ? 2 : 0));
			}
			QByteArray label = QByteArray::fromHex("a4434da5486472d7");
			label += number<quint16>(0) + QByteArray(metadataBig ? "MM" : "II");
			label += number<quint16>(1) + number<quint16>(0);
			label += number(baseOffset + quint32(payload.size())) + number(quint32(toc.size()));
			return payload + toc + label;
		}
	};

	inline QByteArray withBento2Version(QByteArray bytes)
	{
		constexpr qsizetype labelSize = 24;
		constexpr qsizetype majorVersionOffset = 12;
		bytes.replace(bytes.size() - labelSize + majorVersionOffset, 2, number<quint16>(2));
		return bytes;
	}

}
