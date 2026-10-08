#include "canon/mdbreader.h"
#include "canon/mxfreader.h"
#include "canon/projection.h"
#include "canon/scanengine.h"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>

using namespace Canon;

QString identity(const RawProperty &property)
{
	if (property.state != PropertyReadState::Present)
		return {};
	if (property.locator.name == QLatin1String("GenericPackage.PackageUID"))
		return canonicalMxfId(property.encoding);
	if (property.locator.name == QLatin1String("OMFI:MOBJ:MobID"))
		return canonicalDatabaseId(property.encoding, property.bento ? property.bento->metadataBigEndian.value_or(false) : false);
	return {};
}

QJsonObject inspect(const ParsedSource &source, const Projection &projection)
{
	QSet<QString> relevantIds{
		QStringLiteral("060a2b3401010105.01010f1013000000.52a5ed1573110696.f5547c57583610fa"),
		QStringLiteral("060a2b3401010105.01010f1013000000.297f2b1573110696.a4dc7c57583610fa"),
		QStringLiteral("060a2b3401010105.01010f1013000000.66e22e1573110696.be6e7c57583610fa"),
		QStringLiteral("060a2b3401010105.01010f1013000000.80642b1573110696.0a017c57583610fa")};
	// Small MXF headers retain all recorded package identities; large MDBs remain targeted.
	if (source.objects.size() < 3000)
		for (const auto &object : source.objects)
			if (object.mxf)
				for (const auto &property : object.properties)
					if (!identity(property).isEmpty())
						relevantIds.insert(identity(property));
	QSet<ObjectHandle> seeds;
	QHash<ObjectHandle, QVector<const Relationship *>> outgoing;
	for (const auto &object : source.objects)
		for (const auto &property : object.properties)
			if (relevantIds.contains(identity(property)))
				seeds.insert(object.handle);
	for (const auto &link : source.relationships)
		outgoing[link.origin].append(&link);
	QSet<ObjectHandle> retained = seeds;
	for (const auto &object : source.objects)
		if (object.mxf && (object.mxf->name == QLatin1String("Preface") ||
			object.mxf->name == QLatin1String("ContentStorage") ||
			object.mxf->name == QLatin1String("EssenceContainerData")))
			retained.insert(object.handle);
	QVector<ObjectHandle> pending(seeds.cbegin(), seeds.cend());
	while (!pending.isEmpty())
	{
		const auto handle = pending.takeLast();
		for (const auto *link : outgoing.value(handle))
			if (link->target && !retained.contains(link->target))
			{
				retained.insert(link->target);
				pending.append(link->target);
			}
	}
	QJsonArray objects, links, rootClaims, subjects;
	for (const auto &object : source.objects)
		if (retained.contains(object.handle))
		{
			QJsonArray properties;
			for (const auto &property : object.properties)
			{
				QJsonObject value{{"name", property.locator.name}, {"state", int(property.state)},
					{"encodingBytes", property.encoding.size()}, {"first128Hex", QString::fromLatin1(property.encoding.first(qMin(qsizetype(128), property.encoding.size())).toHex())}};
				if (property.decoded.metaType().id() != QMetaType::QByteArray)
					value.insert(QStringLiteral("decoded"), QJsonValue::fromVariant(property.decoded));
				const auto id = identity(property);
				if (!id.isEmpty()) value.insert(QStringLiteral("canonicalId"), id);
				properties.append(value);
			}
			objects.append(QJsonObject{{"handle", qint64(object.handle)}, {"seed", seeds.contains(object.handle)},
				{"class", object.mxf ? object.mxf->name : QString{}}, {"properties", properties}});
		}
	for (const auto &link : source.relationships)
	{
		if (retained.contains(link.origin))
			links.append(QJsonObject{{"from", qint64(link.origin)}, {"to", qint64(link.target)},
				{"property", link.locator.name}, {"explanation", link.explanation}});
		if (seeds.contains(link.target) && (link.locator.name.contains(QLatin1String("Mobs")) || link.locator.name == QLatin1String("ContentStorage.Packages")))
		{
			const auto recorded = link.recordedReference.toMap();
			QJsonObject claim{{"from", qint64(link.origin)}, {"target", qint64(link.target)}, {"property", link.locator.name},
				{"index", QJsonValue::fromVariant(recorded.value(QStringLiteral("index")))},
				{"key", QJsonValue::fromVariant(recorded.value(QStringLiteral("key")))},
				{"recordedMobIdHex", QString::fromLatin1(recorded.value(QStringLiteral("mobId")).toByteArray().toHex())},
				{"explanation", link.explanation}};
			rootClaims.append(claim);
		}
	}
	const auto subject = [&](const ProjectedFile &original, const char *kind)
	{
		bool relevant = relevantIds.contains(original.fileMobId);
		for (const auto &id : original.masterMobIds) relevant |= relevantIds.contains(id);
		if (!relevant) return;
		auto file = original;
		selectMetadata(file.evidence);
		QJsonArray fields;
		for (const auto field : {MediaProperty::FileMobId, MediaProperty::MasterMobId, MediaProperty::ClipName,
			MediaProperty::OriginalBin, MediaProperty::ClipDuration, MediaProperty::SourceFilename})
		{
			const auto selected = file.evidence.selected(field);
			QJsonArray observations;
			for (const auto &observation : file.evidence.observations(field))
				observations.append(QJsonObject{{"value", QJsonValue::fromVariant(observation.value)},
					{"readState", int(observation.readState)}, {"eligible", observation.eligible},
					{"object", observation.objectIdentity}, {"property", observation.property}, {"explanation", observation.explanation}});
			fields.append(QJsonObject{{"field", mediaPropertyName(field)}, {"selected", QJsonValue::fromVariant(selected.value)},
				{"agreement", int(selected.agreement)}, {"observations", observations}});
		}
		subjects.append(QJsonObject{{"kind", QString::fromLatin1(kind)}, {"fileId", file.fileMobId},
			{"masterIds", QJsonArray::fromStringList(file.masterMobIds)}, {"fields", fields}});
	};
	for (const auto &file : projection.files) subject(file, "file");
	for (const auto &master : projection.masters) subject(master, "master");
	return QJsonObject{{"sourceOutcome", int(source.outcome)}, {"sourceObjects", source.objects.size()},
		{"retainedObjects", objects}, {"links", links}, {"rootClaims", rootClaims}, {"projectedSubjects", subjects},
		{"projectionDiagnostics", QJsonArray::fromStringList(projection.diagnostics)}};
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTextStream output(stdout);
	for (const auto &path : app.arguments().sliced(1))
	{
		QFile input(path);
		if (!input.open(QIODevice::ReadOnly)) return 1;
		Cancellation cancellation;
		const bool mxf = path.endsWith(QLatin1String(".mxf"));
		const auto source = mxf ? MxfReader{}.read(input, {{}, cancellation}) : MdbReader{}.read(input, {{}, cancellation});
		const auto projection = mxf ? projectMxf(source, cancellation) : projectOmf(source, cancellation);
		output << QJsonDocument(QJsonObject{{"path", path}, {"evidence", inspect(source, projection)}}).toJson(QJsonDocument::Compact) << '\n';
		output.flush();
	}
}
