// Authored in-memory typed Bento specimens characterize F17/F21 boundaries.
// No genuine-format certification or real-media I/O is performed.

#include "canon/projection.h"
#include "canon/mdbreader.h"
#include "testcanonbento.h"

#include <QBuffer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	using TestCanonBento::number;
	using TestCanonBento::TypedBento;

	QByteArray uid(quint32 value, bool big = false)
	{
		return number<quint32>(42, big) + number(value, big) + number<quint32>(900, big);
	}

	void object(TypedBento &writer, quint32 handle, const char *cls)
	{
		writer.add(handle, "OMFI:ObjID", "omfi:ObjectTag", cls, true);
	}

	Canon::ParsedSource read(QByteArray bytes)
	{
		QBuffer input(&bytes);
		input.open(QIODevice::ReadOnly);
		Canon::Cancellation cancellation;
		return Canon::MdbReader{}.read(input, {{}, cancellation});
	}

	Canon::Projection project(const Canon::ParsedSource &source)
	{
		Canon::Cancellation cancellation;
		return Canon::projectOmf(source, cancellation);
	}

	QVariant single(const Canon::ProjectedFile &file, MediaProperty field)
	{
		const auto &values = file.evidence.observations(field);
		return values.size() == 1 ? values.first().value : QVariant{};
	}

	QByteArray chunk(const QByteArray &key, const QByteArray &value)
	{
		if (quint64(value.size()) > std::numeric_limits<quint32>::max())
			throw std::length_error("Diagnostic chunk length exceeds UInt32.");
		return key + number(quint32(value.size())) + value + QByteArray(value.size() & 1, '\0');
	}

}

int main()
{
	QJsonArray reports;
	for (const bool wideId : {false, true})
	{
		TypedBento writer;
		writer.head(1);
		writer.referenceArray(1, "OMFI:ObjectSpine", {101}, 1);
		object(writer, 101, "MOBJ");
		object(writer, 201, "CDCI");
		const auto id = wideId ? QByteArray::fromHex("060a2b340101010501010f1013000000") + QByteArray(16, 'f') : uid(11);
		writer.add(101, "OMFI:MOBJ:MobID", "omfi:UID", id);
		writer.add(101, "OMFI:MOBJ:PhysicalMedia", "omfi:ObjRef", writer.reference(201, 1));
		writer.add(201, "OMFI:DIDD:Compression", "omfi:String", "JFIF");
		writer.add(201, "OMFI:DIDD:DIDResolutionID", "omfi:UInt32", number<quint32>(82));
		const auto source = read(writer.build());
		const auto projection = project(source);
		QJsonObject row{{"case", wideId ? "legacy-descriptor-32-byte-id" : "legacy-descriptor-12-byte-id"},
						{"reader_outcome", int(source.outcome)},
						{"file_candidates", projection.files.size()}};
		if (!projection.files.isEmpty())
			row.insert("codec", single(projection.files.first(), MediaProperty::Compression).toString());
		reports.append(row);
	}
	for (const bool aifc : {false, true})
	{
		TypedBento writer;
		writer.head(1);
		writer.referenceArray(1, "OMFI:ObjectSpine", {101}, 1);
		object(writer, 101, "MOBJ");
		object(writer, 201, aifc ? "AIFD" : "WAVD");
		writer.add(101, "OMFI:MOBJ:MobID", "omfi:UID", uid(11));
		writer.add(101, "OMFI:MOBJ:PhysicalMedia", "omfi:ObjRef", writer.reference(201, 1));
		QByteArray summary;
		if (aifc)
		{
			const auto common = number<quint16>(1, true) + number<quint32>(100, true) + number<quint16>(24, true) + QByteArray::fromHex("400ebb80000000000000") + "in24";
			const auto comm = QByteArray("COMM") + number<quint32>(quint32(common.size()), true) + common;
			summary = QByteArray("FORM") + number<quint32>(quint32(comm.size() + 4), true) + "AIFC" + comm;
		}
		else
		{
			const auto fmt = number<quint16>(0xfffe) + number<quint16>(1) + number<quint32>(48000) + number<quint32>(144000) + number<quint16>(3) + number<quint16>(24) + number<quint16>(22) + number<quint16>(20);
			const auto data = chunk("fmt ", fmt);
			summary = QByteArray("RIFF") + number<quint32>(quint32(data.size() + 4)) + "WAVE" + data;
		}
		writer.add(201, aifc ? "OMFI:AIFD:Summary" : "OMFI:WAVD:Summary", "omfi:DataValue", summary);
		const auto source = read(writer.build());
		const auto projection = project(source);
		QJsonObject row{{"case", aifc ? "summary-aifc-missing-name-length" : "summary-extensible-missing-guid"},
						{"reader_outcome", int(source.outcome)},
						{"file_candidates", projection.files.size()}};
		if (projection.files.isEmpty())
		{
			reports.append(row);
			continue;
		}
		const auto &file = projection.files.first();
		row.insert("bit_depth", single(file, MediaProperty::BitDepth).toString());
		row.insert("codec", single(file, MediaProperty::Compression).toString());
		const auto values = file.evidence.observations(MediaProperty::BitDepth);
		if (!values.isEmpty())
		{
			row.insert("bit_depth_read_state", int(values.first().readState));
			row.insert("bit_depth_explanation", values.first().explanation);
		}
		reports.append(row);
	}
	std::cout << QJsonDocument(reports).toJson().constData();
}
