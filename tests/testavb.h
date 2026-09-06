#pragma once

// Deliberately small AVB fixture writer. It writes complete documents,
// not signature-plus-pattern buffers.
// No production parser routines are used to construct the bytes.

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QVector>
#include <algorithm>

namespace TestAvb
{

	inline const QByteArray Master = QByteArray::fromHex("060a2b340101010501010f10130000004433221166558877aabbccddeeff0123");
	inline const QByteArray Source = QByteArray::fromHex("060a2b340101010501010f101300000098badcfe321076540123456789abcdef");
	inline const QByteArray Other = QByteArray::fromHex("060a2b340101010501010f101300000004030201060508071122334455667788");

	struct Bytes
	{
		bool bigEndian;
		QByteArray data;
		explicit Bytes(bool big = false) : bigEndian(big) {}
		void u8(quint8 value) { data.append(char(value)); }
		void integer(quint64 value, int width)
		{
			Q_ASSERT(width > 0 && width <= 8);
			for (int i = 0; i < width; ++i)
				u8(quint8(value >> (8 * (bigEndian ? width - 1 - i : i))));
		}
		void u16(quint16 value) { integer(value, 2); }
		void u32(quint32 value) { integer(value, 4); }
		void u64(quint64 value) { integer(value, 8); }
		void tags(quint8 first, quint8 second)
		{
			u8(first);
			u8(second);
		}
		void string(const QByteArray &value)
		{
			Q_ASSERT(value.size() < 0xffff);
			u16(static_cast<quint16>(value.size()));
			data += value;
		}
		void fourcc(QByteArray value)
		{
			if (!bigEndian)
				std::reverse(value.begin(), value.end());
			data += value;
		}
		quint32 littleWord(const QByteArray &raw, int start, int width) const
		{
			quint32 value = 0;
			for (int i = 0; i < width; ++i)
				value |= quint32(quint8(raw[start + i])) << (8 * i);
			return value;
		}
		void legacyWords(const QByteArray &id)
		{
			u32(littleWord(id, 16, 4));
			u32(littleWord(id, 20, 4));
		}
		void mob(const QByteArray &id)
		{
			Q_ASSERT(id.size() == 32);
			u8(65);
			u32(12);
			data += id.left(12);
			for (int i = 12; i < 16; ++i)
			{
				u8(68);
				u8(quint8(id[i]));
			}
			u8(72);
			u32(littleWord(id, 16, 4));
			u8(70);
			u16(quint16(littleWord(id, 20, 2)));
			u8(70);
			u16(quint16(littleWord(id, 22, 2)));
			u8(65);
			u32(8);
			data += id.mid(24);
		}
	};

	inline QByteArray component(bool big, const QByteArray &name = {}, quint32 attributes = 0,
								quint32 precomputed = 0)
	{
		Bytes b(big);
		b.tags(2, 3);
		b.u32(0);
		b.u32(0); // left/right BOB
		b.u16(1);
		b.u32(25);
		b.u16(0); // picture, 25 * 10^0
		b.string(name);
		b.u16(0xffff); // effect ID absent
		b.u32(attributes);
		b.u32(0);
		b.u32(precomputed);
		return b.data;
	}

	inline QByteArray composition(bool big, const QByteArray &id = Master,
								  const QByteArray &name = "Binary master", quint32 attributes = 0,
								  const QVector<quint32> &tracks = {}, int usage = 0, bool typed = true,
								  quint32 descriptor = 0, int mobType = 2)
	{
		Bytes b(big);
		b.data = component(big, name, attributes);
		b.tags(2, 8);
		b.u8(0);
		b.u32(100);
		b.u32(0);
		b.u32(quint32(tracks.size()));
		for (quint32 reference : tracks)
		{
			b.u16(5);
			b.u16(1);
			b.u32(reference);
		}
		b.tags(2, 2);
		b.legacyWords(id);
		b.u32(1700000000);
		b.u8(quint8(mobType));
		b.u32(quint32(usage));
		b.u32(descriptor);
		if (typed)
		{
			b.tags(1, 2);
			b.mob(id);
		}
		b.u8(3);
		return b.data;
	}

	inline QByteArray sourceClip(bool big, const QByteArray &id = Source, bool typed = true)
	{
		Bytes b(big);
		b.data = component(big);
		b.tags(2, 1);
		b.u32(100);
		b.tags(2, 3);
		b.legacyWords(id);
		b.u16(1);
		b.u32(25);
		if (typed)
		{
			b.tags(1, 1);
			b.mob(id);
		}
		b.u8(3);
		return b.data;
	}

	inline QByteArray sequence(bool big, const QVector<quint32> &references)
	{
		Bytes b(big);
		b.data = component(big);
		b.tags(2, 3);
		b.u32(static_cast<quint32>(references.size()));
		for (const auto reference : references)
			b.u32(reference);
		b.u8(3);
		return b.data;
	}

	inline QByteArray referenceList(bool big, const QVector<quint32> &references, bool shortCount)
	{
		Bytes b(big);
		b.tags(2, 1);
		if (shortCount)
			b.u16(static_cast<quint16>(references.size()));
		else
			b.u32(static_cast<quint32>(references.size()));
		for (const auto reference : references)
			b.u32(reference);
		b.u8(3);
		return b.data;
	}

	inline QByteArray mobReference(bool big, const QByteArray &id = Other, bool typed = true)
	{
		Bytes b(big);
		b.tags(2, 1);
		b.legacyWords(id);
		b.u32(25);
		if (typed)
		{
			b.tags(1, 1);
			b.mob(id);
		}
		b.u8(3);
		return b.data;
	}

	inline QByteArray attributes(bool big, quint32 originalBin = 0, const QByteArray &comment = {},
								 quint32 mobReference = 0)
	{
		Bytes b(big);
		b.tags(2, 1);
		b.u32((originalBin != 0) + !comment.isEmpty() + (mobReference != 0));
		if (originalBin)
		{
			b.u32(3);
			b.string("_ORG_BIN");
			b.u32(originalBin);
		}
		if (!comment.isEmpty())
		{
			b.u32(2);
			b.string("Comments");
			b.string(comment);
		}
		if (mobReference)
		{
			b.u32(3);
			b.string("Marker");
			b.u32(mobReference);
		}
		b.u8(3);
		return b.data;
	}

	inline QByteArray binReference(bool big, const QByteArray &name = "Camera originals",
								   const QByteArray &utf8 = QByteArray::fromHex("e69db1e4baac"))
	{
		Bytes b(big);
		b.tags(2, 1);
		b.u32(0x12345678);
		b.u32(0x90abcdef);
		b.string(name);
		if (!utf8.isEmpty())
		{
			b.tags(1, 1);
			b.u8(76);
			b.string(QByteArray(2, '\0') + utf8);
		}
		b.u8(3);
		return b.data;
	}

	inline QByteArray bin(bool big, const QVector<quint32> &items = {}, bool large = false,
						  bool first = false, quint32 attributes = 0)
	{
		Bytes b(big);
		b.tags(2, large ? 15 : 14);
		b.u32(0);
		b.u64(0x1234567890abcdef);
		if (large)
			b.u32(quint32(items.size()));
		else
			b.u16(quint16(items.size()));
		for (quint32 reference : items)
		{
			b.u32(reference);
			b.u16(0);
			b.u16(0);
			b.u32(0);
			b.u8(1);
		}
		b.u32(98999);
		b.u16(0);
		b.u8(0);
		for (int i = 0; i < 6; ++i)
		{
			b.u16(1);
			b.string({});
			b.string("Any");
		}
		b.u16(0); // no sort columns
		b.u16(1);
		b.u16(11);
		b.u16(5);
		b.u16(1);
		b.u16(0);
		b.u16(0);
		b.u16(300);
		b.u16(600); // versioned rect
		b.u16(1);
		b.u16(45568);
		b.u16(45568);
		b.u16(45568); // background
		b.u16(1);
		b.u16(3328);
		b.u16(3328);
		b.u16(3328); // foreground
		b.u16(6);
		b.u32(attributes);
		b.u8(0);
		if (first)
		{
			b.tags(2, 1);
			b.u32(0);
		}
		b.u8(3);
		return b.data;
	}

	struct Document
	{
		struct Object
		{
			QByteArray type;
			QByteArray payload;
		};
		bool bigEndian = false;
		QVector<Object> objects;
		quint32 root = 1;
		qsizetype countOffset = 0;
		qsizetype rootOffset = 0;
		QVector<qsizetype> chunkOffsets;
		QByteArray bytes()
		{
			Bytes b(bigEndian);
			b.u16(6);
			b.data += "Domain";
			b.fourcc("OBJD");
			b.string("AObjDoc");
			b.u8(4);
			b.string("2026/09/06 12:34:56");
			countOffset = b.data.size();
			b.u32(quint32(objects.size()));
			rootOffset = b.data.size();
			b.u32(root);
			b.u32(bigEndian ? 0x4d4d4d4d : 0x49494949);
			b.u32(1700000000);
			b.u32(0);
			b.fourcc("ATob");
			b.fourcc("ATve");
			b.string(QByteArray("MediaMuster test fixture").leftJustified(30, ' '));
			b.data += QByteArray(16, '\0');
			chunkOffsets.clear();
			for (const Object &object : objects)
			{
				chunkOffsets.append(b.data.size());
				b.fourcc(object.type);
				b.u32(quint32(object.payload.size()));
				b.data += object.payload;
			}
			return b.data;
		}
	};

	inline void replaceU32(QByteArray &bytes, qsizetype offset, quint32 value, bool big = false)
	{
		Bytes replacement(big);
		replacement.u32(value);
		bytes.replace(offset, 4, replacement.data);
	}

	inline QByteArray masterBin(const QVector<QByteArray> &ids = {Master}, bool big = false)
	{
		Document document;
		document.bigEndian = big;
		QVector<quint32> references;
		for (qsizetype i = 0; i < ids.size(); ++i)
			references.append(quint32(i + 2));
		document.objects.append({"ABIN", bin(big, references)});
		for (const auto &id : ids)
			document.objects.append({"CMPO", composition(big, id)});
		return document.bytes();
	}

	inline QString write(const QString &path, const QByteArray &bytes)
	{
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
			return {};
		return path;
	}

} // namespace TestAvb
