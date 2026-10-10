// Authored in-memory MXF packets characterize the F09/F10 type-width boundary.
// Controls compare valid widths; no genuine-format certification is implied.

#include "mediaengine/mxfreader.h"

#include <QBuffer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <iostream>
#include <QtEndian>
#include <limits>
#include <stdexcept>

namespace
{

	QByteArray hex(const char *text) { return QByteArray::fromHex(text); }
	template <typename T>
	QByteArray number(T value)
	{
		QByteArray result(sizeof(T), '\0');
		qToBigEndian(value, result.data());
		return result;
	}
	QByteArray ber(quint64 length)
	{
		if (length < 128)
			return QByteArray(1, char(length));
		QByteArray digits;
		while (length)
		{
			digits.prepend(char(length & 255));
			length >>= 8;
		}
		return QByteArray(1, char(0x80 | digits.size())) + digits;
	}
	QByteArray klv(const QByteArray &key, const QByteArray &value)
	{
		return key + ber(quint64(value.size())) + value;
	}
	const QByteArray descriptorKey = hex("060e2b34025301010d01010101012800");
	using Mapping = QPair<quint16, QByteArray>;
	QByteArray primer(const QVector<Mapping> &mappings)
	{
		if (quint64(mappings.size()) > std::numeric_limits<quint32>::max())
			throw std::length_error("Diagnostic Primer count exceeds UInt32.");
		QByteArray value = number(quint32(mappings.size())) + number<quint32>(18);
		for (const auto &entry : mappings)
			value += number(entry.first) + entry.second;
		return klv(hex("060e2b34020501010d01020101050100"), value);
	}
	QByteArray item(quint16 tag, const QByteArray &value, bool berLength = false)
	{
		if (!berLength && value.size() > std::numeric_limits<quint16>::max())
			throw std::length_error("Diagnostic local-item length exceeds UInt16.");
		return number(tag) + (berLength ? ber(quint64(value.size())) : number(quint16(value.size()))) + value;
	}
	QByteArray partition(quint8 kind, quint64 offset, quint64 previous, quint64 footer,
						 quint64 metadataBytes = 0, quint32 bodySid = 0)
	{
		QByteArray key = hex("060e2b34020501010d01020101020400");
		key[13] = char(kind);
		QByteArray value = number<quint16>(1) + number<quint16>(3) + number<quint32>(1) + number(offset) + number(previous) + number(footer) + number(metadataBytes) + number<quint64>(0) + number<quint32>(0) + number<quint64>(0) + number(bodySid) + hex("060e2b34040101020d01020110030000") + number<quint32>(0) + number<quint32>(16);
		return klv(key, value);
	}
	QByteArray document(const QByteArray &headerMetadata, const QByteArray &footerMetadata = {})
	{
		const quint64 footerOffset = quint64(partition(2, 0, 0, 0).size() + headerMetadata.size());
		return partition(2, 0, 0, footerOffset, quint64(headerMetadata.size())) + headerMetadata + partition(4, footerOffset, 0, footerOffset, quint64(footerMetadata.size())) + footerMetadata;
	}
	MediaEngine::ParsedSource parse(QByteArray bytes)
	{
		QBuffer source(&bytes);
		source.open(QIODevice::ReadOnly);
		MediaEngine::Cancellation cancellation;
		return MediaEngine::MxfReader{}.read(source, {{}, cancellation});
	}
	QVector<const MediaEngine::RawProperty *> properties(const MediaEngine::ParsedSource &result, const QByteArray &ul)
	{
		QVector<const MediaEngine::RawProperty *> found;
		for (const auto &object : result.objects)
			for (const auto &property : object.properties)
				if (property.mxf && property.mxf->mappedAuid == ul)
					found.append(&property);
		return found;
	}
}

int main()
{
	QJsonArray reports;
	const QByteArray durationUl = hex("060e2b34010101010406010200000000");
	const QByteArray codingUl = hex("060e2b34010101020401060100000000");
	const QByteArray widthUl = hex("060e2b34010101010401050202000000");
	struct Case
	{
		const char *name;
		QByteArray ul;
		QByteArray payload;
	};
	for (const auto &test : {
			 Case{"duration-5", durationUl, QByteArray::fromHex("00000000fa")},
			 Case{"duration-8", durationUl, number<qint64>(250)},
			 Case{"coding-20", codingUl, hex("060e2b340401010d040102020307010000000000")},
			 Case{"coding-16", codingUl, hex("060e2b340401010d0401020203070100")},
			 Case{"width-5", widthUl, number<quint32>(1920) + QByteArray(1, '\0')},
			 Case{"width-4", widthUl, number<quint32>(1920)}})
	{
		const auto result = parse(document(primer({{0x8001, test.ul}}) + klv(descriptorKey, item(0x8001, test.payload))));
		const auto found = properties(result, test.ul);
		QJsonObject row{{"name", QLatin1String(test.name)}, {"outcome", int(result.outcome)}, {"property_count", found.size()}};
		if (!found.isEmpty())
		{
			row.insert("read_state", int(found.first()->state));
			row.insert("decoded_valid", found.first()->decoded.isValid());
			row.insert("interpretation", found.first()->interpretation);
		}
		reports.append(row);
	}
	std::cout << QJsonDocument(reports).toJson().constData();
}
