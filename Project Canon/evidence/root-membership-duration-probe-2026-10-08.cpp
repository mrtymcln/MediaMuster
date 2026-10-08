#include "canon/mdbreader.h"
#include "canon/projection.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QSet>
#include <QTextStream>
#include <algorithm>

using namespace Canon;

QString className(const AvidObject &object)
{
	for (const auto &property : object.properties)
		if (property.locator.name == "OMFI:ObjID" || property.locator.name == "OMFI:OOBJ:ObjClass")
			return property.decoded.toString();
	return {};
}

QString mobIdentity(const RawProperty &property)
{
	return canonicalDatabaseId(property.encoding,
		property.bento ? property.bento->metadataBigEndian.value_or(false) : false);
}

bool componentLink(const QString &name)
{
	return name == "OMFI:TRKG:Tracks" || name == "OMFI:TRAK:TrackComponent" ||
		name == "OMFI:SEQU:Sequence" || name == "OMFI:MOBJ:Slots" ||
		name == "OMFI:MSLT:Segment" || name == "OMFI:SEQU:Components" ||
		name == "OMFI:NEST:Slots" || name == "OMFI:SLCT:Selected" ||
		name == "OMFI:SLCT:Alternates" || name == "OMFI:MGRP:Choices" ||
		name == "OMFI:ERAT:InputSegment";
}

bool retainedField(const QString &name)
{
	return componentLink(name) || name == "OMFI:ObjID" || name == "OMFI:OOBJ:ObjClass" ||
		name == "OMFI:MOBJ:MobID" || name == "OMFI:CPNT:Name" ||
		name == "OMFI:MOBJ:UsageCode" || name == "OMFI:CPNT:TrackKind" ||
		name == "OMFI:CPNT:EditRate" || name == "OMFI:TRKG:GroupLength" ||
		name == "OMFI:CLIP:Length" || name == "OMFI:TRAK:LabelNumber" ||
		name == "OMFI:SCLP:SourceID" || name == "OMFI:SCLP:SourceTrack" ||
		name == "OMFI:SCLP:SourcePosition";
}

QJsonObject inspectDatabase(const QJsonObject &request)
{
	const QString path = request["path"].toString();
	QFile input(path);
	if (!input.open(QIODevice::ReadOnly))
		return {{"path", path}, {"error", input.errorString()}};
	const auto before = QFileInfo(input);
	const auto sizeBefore = before.size();
	const auto mtimeBefore = before.lastModified().toMSecsSinceEpoch();
	const Cancellation cancellation;
	const auto source = MdbReader{}.read(input, {{}, cancellation});
	const auto projection = projectOmf(source, cancellation);
	QHash<ObjectHandle, const AvidObject *> objects;
	QHash<ObjectHandle, QVector<const Relationship *>> outgoing;
	QHash<QString, QSet<ObjectHandle>> mobsById, rootMembers;
	QSet<QString> wantedMasters;
	for (const auto &row : request["rows"].toArray())
		wantedMasters.insert(row.toObject()["masterId"].toString());
	for (const auto &object : source.objects)
	{
		objects.insert(object.handle, &object);
		if (className(object) == "MOBJ")
			for (const auto &property : object.properties)
				if (property.locator.name == "OMFI:MOBJ:MobID")
					mobsById[mobIdentity(property)].insert(object.handle);
	}
	QJsonArray rootFields;
	for (const auto &edge : source.relationships)
	{
		outgoing[edge.origin].append(&edge);
		if (edge.origin == 1 && (edge.locator.name == "OMFI:ObjectSpine" ||
			edge.locator.name == "OMFI:SourceMobs" || edge.locator.name == "OMFI:CompositionMobs"))
			rootMembers[edge.locator.name].insert(edge.target);
	}
	if (const auto *head = objects.value(1))
		for (const auto &property : head->properties)
			if (property.locator.name == "OMFI:ObjectSpine" || property.locator.name == "OMFI:SourceMobs" ||
				property.locator.name == "OMFI:CompositionMobs" || property.locator.name == "OMFI:NumDelMobs")
				rootFields.append(QJsonObject{{"name", property.locator.name}, {"state", int(property.state)},
					{"type", property.bento ? property.bento->typeName : QString{}},
					{"bytes", property.encoding.size()}, {"decoded", QJsonValue::fromVariant(property.decoded)},
					{"hex", QString::fromLatin1(property.encoding.toHex())},
					{"resolvedUniqueMembers", rootMembers.value(property.locator.name).size()}});

	QJsonArray masters;
	QStringList orderedMasters = wantedMasters.values();
	orderedMasters.sort();
	for (const auto &id : orderedMasters)
	{
		QJsonArray copies;
		auto handles = mobsById.value(id).values();
		std::sort(handles.begin(), handles.end());
		for (const auto handle : handles)
		{
			QSet<ObjectHandle> visited;
			QVector<ObjectHandle> pending{handle};
			while (!pending.isEmpty())
			{
				const auto current = pending.takeLast();
				if (visited.contains(current)) continue;
				visited.insert(current);
				for (const auto *edge : outgoing.value(current))
					if (componentLink(edge->locator.name) && edge->target && objects.contains(edge->target))
						pending.append(edge->target);
			}
			auto ordered = visited.values();
			std::sort(ordered.begin(), ordered.end());
			QJsonArray graph;
			for (const auto current : ordered)
			{
				const auto *object = objects.value(current);
				if (!object) continue;
				QJsonArray fields, edges;
				for (const auto &property : object->properties)
					if (retainedField(property.locator.name))
					{
						QJsonObject field{{"name", property.locator.name}, {"state", int(property.state)},
							{"type", property.bento ? property.bento->typeName : QString{}},
							{"bytes", property.encoding.size()}, {"hex", QString::fromLatin1(property.encoding.toHex())},
							{"interpretation", property.interpretation}};
						if (property.locator.name.endsWith(":MobID") || property.locator.name == "OMFI:SCLP:SourceID")
							field["canonicalId"] = mobIdentity(property);
						else field["decoded"] = QJsonValue::fromVariant(property.decoded);
						fields.append(field);
					}
				for (const auto *edge : outgoing.value(current))
					if (componentLink(edge->locator.name))
						edges.append(QJsonObject{{"name", edge->locator.name}, {"target", qint64(edge->target)},
							{"resolved", bool(objects.value(edge->target))}, {"explanation", edge->explanation}});
				graph.append(QJsonObject{{"handle", qint64(current)}, {"class", className(*object)},
					{"properties", fields}, {"outgoing", edges}});
			}
			copies.append(QJsonObject{{"handle", qint64(handle)},
				{"inObjectSpine", rootMembers["OMFI:ObjectSpine"].contains(handle)},
				{"inCompositionMobs", rootMembers["OMFI:CompositionMobs"].contains(handle)},
				{"inSourceMobs", rootMembers["OMFI:SourceMobs"].contains(handle)}, {"componentGraph", graph}});
		}
		masters.append(QJsonObject{{"masterId", id}, {"rawCopies", copies}});
	}
	QJsonArray rows;
	for (const auto &entry : request["rows"].toArray())
	{
		auto row = entry.toObject();
		QJsonArray facts, rawFiles;
		auto rawHandles = mobsById.value(row["fileId"].toString()).values();
		std::sort(rawHandles.begin(), rawHandles.end());
		for (const auto handle : rawHandles)
		{
			QJsonArray descriptors;
			for (const auto *edge : outgoing.value(handle))
				if (edge->locator.name == "OMFI:MOBJ:PhysicalMedia")
					descriptors.append(QJsonObject{{"handle", qint64(edge->target)},
						{"class", objects.contains(edge->target) ? className(*objects.value(edge->target)) : QString{}},
						{"explanation", edge->explanation}});
			rawFiles.append(QJsonObject{{"handle", qint64(handle)},
				{"inObjectSpine", rootMembers["OMFI:ObjectSpine"].contains(handle)},
				{"inSourceMobs", rootMembers["OMFI:SourceMobs"].contains(handle)}, {"descriptors", descriptors}});
		}
		for (const auto &file : projection.files)
			if (file.fileMobId == row["fileId"].toString())
			{
				QJsonArray durations;
				for (const auto &item : file.evidence.observations(MediaProperty::ClipDuration))
					durations.append(QJsonObject{{"eligible", item.eligible}, {"value", QJsonValue::fromVariant(item.value)},
						{"object", item.objectIdentity}, {"property", item.property}});
				facts.append(QJsonObject{{"masters", QJsonArray::fromStringList(file.masterMobIds)},
					{"clipDurationObservations", durations}});
			}
		row["currentDatabaseFacts"] = facts;
		row["rawFileCopies"] = rawFiles;
		rows.append(row);
	}
	const QFileInfo after(path);
	return {{"path", path}, {"outcome", int(source.outcome)}, {"omfRevision", source.omfRevision ? int(*source.omfRevision) : 0},
		{"sizeBefore", sizeBefore}, {"sizeAfter", after.size()},
		{"mtimeBefore", mtimeBefore}, {"mtimeAfter", after.lastModified().toMSecsSinceEpoch()},
		{"parseDiagnostics", QJsonArray::fromStringList(source.diagnostics)},
		{"projectionDiagnostics", QJsonArray::fromStringList(projection.diagnostics)},
		{"rootFields", rootFields}, {"rows", rows}, {"masters", masters}};
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	if (app.arguments().size() != 2) return 2;
	QFile request(app.arguments().at(1));
	if (!request.open(QIODevice::ReadOnly)) return 2;
	const auto databases = QJsonDocument::fromJson(request.readAll()).object()["databases"].toArray();
	QTextStream out(stdout);
	for (const auto &database : databases)
	{
		out << QJsonDocument(inspectDatabase(database.toObject())).toJson(QJsonDocument::Compact) << '\n';
		out.flush();
	}
}
