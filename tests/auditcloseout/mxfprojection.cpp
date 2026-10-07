// Authored post-reader graphs characterize F06/F07 selection gaps.
// This diagnostic reports observations; it does not certify genuine MXF files.

#include "canon/projection.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <stdexcept>

namespace
{
	using namespace Canon;

	QByteArray umid(char material)
	{
		return QByteArray::fromHex("060a2b340101010501010f1013000000") + QByteArray(16, material);
	}

	struct Graph
	{
		ParsedSource source;
		ObjectHandle ecd = 0, file = 0, descriptor = 0, preface = 0;
		QByteArray fileId = umid('f');

		Graph(const char *descriptorClass = "CDCIEssenceDescriptor", qint64 partition = 0)
		{
			source.container = ParsedSource::Container::Mxf;
			source.outcome = ParsedSource::Outcome::Complete;
			source.snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
				MetadataSource::Mxf, QStringLiteral("test.mxf"), {}, SourceReadState::Complete});
			preface = object("Preface", partition);
			property(preface, "Preface.ProjectEditRate", rateValue({24, 1}));
			ecd = object("EssenceContainerData", partition);
			property(ecd, "EssenceContainerData.LinkedPackageUID", fileId);
			file = object("SourcePackage", partition);
			property(file, "GenericPackage.PackageUID", fileId);
			descriptor = object(descriptorClass, partition);
			edge(file, "SourcePackage.Descriptor", descriptor);
		}

		ObjectHandle object(const char *name, qint64 partition = 0)
		{
			AvidObject value;
			if (source.objects.size() >= 255)
				throw std::length_error("This small diagnostic graph uses one-byte synthetic handles.");
			value.handle = quint64(source.objects.size()) + 1;
			value.snapshot = source.snapshot;
			auto context = QSharedPointer<MxfSetContext>::create();
			context->name = QString::fromLatin1(name);
			context->partitionOffset = partition;
			context->value.offset = qint64(value.handle) * 10000;
			value.mxf = context;
			source.objects.append(value);
			property(value.handle, "InterchangeObject.InstanceUID", QByteArray(16, char(value.handle)));
			return value.handle;
		}

		AvidObject &at(ObjectHandle handle)
		{
			if (!handle || handle > quint64(source.objects.size()))
				throw std::out_of_range("Diagnostic graph handle is absent.");
			return source.objects[qsizetype(handle - 1)];
		}

		void property(ObjectHandle handle, const char *name, const QVariant &value)
		{
			auto &object = at(handle);
			RawProperty property;
			property.locator.name = QString::fromLatin1(name);
			property.locator.objectNumber = handle;
			property.decoded = value;
			property.state = PropertyReadState::Present;
			property.encoding = value.metaType().id() == QMetaType::QByteArray ? value.toByteArray() : QByteArray(4, 'v');
			auto set = QSharedPointer<MxfSetContext>::create(*object.mxf);
			auto context = QSharedPointer<MxfPropertyContext>::create();
			context->mappedAuid = QByteArray(16, 'k');
			const qint64 offset = set->value.offset + set->value.length;
			context->framingRanges.append({offset, 4});
			property.locator.ranges.append({offset + 4, property.encoding.size()});
			property.mxf = context;
			set->value.length += 4 + property.encoding.size();
			object.mxf = set;
			object.properties.append(property);
		}

		void edge(ObjectHandle from, const char *name, ObjectHandle to)
		{
			property(from, name, QByteArray(16, char(to)));
			Relationship relationship;
			relationship.origin = from;
			relationship.target = to;
			relationship.locator.name = QString::fromLatin1(name);
			source.relationships.append(relationship);
		}

		ObjectHandle master(char id = 'm')
		{
			const auto partition = at(file).mxf->partitionOffset;
			const auto result = object("MaterialPackage", partition);
			property(result, "GenericPackage.PackageUID", umid(id));
			property(result, "GenericPackage.Name", QStringLiteral("Clip %1").arg(QChar::fromLatin1(id)));
			const auto track = object("Track", partition), clip = object("SourceClip", partition);
			property(track, "GenericTrack.TrackID", 1u);
			property(track, "Track.EditRate", rateValue({24, 1}));
			property(clip, "SourceClip.SourcePackageID", fileId);
			property(clip, "StructuralComponent.Duration", qint64(48));
			edge(result, "GenericPackage.Tracks", track);
			edge(track, "GenericTrack.Sequence", clip);
			return result;
		}
	};

	QVariant value(const ProjectedFile &file, MediaProperty field)
	{
		const auto &values = file.evidence.observations(field);
		return values.isEmpty() ? QVariant{} : values.first().value;
	}

	Projection project(const Graph &graph)
	{
		Cancellation cancellation;
		return projectMxf(graph.source, cancellation);
	}
}

int main()
{
	QJsonObject report;
	Graph ordinary;
	const auto duplicate = ordinary.object("SourcePackage");
	ordinary.property(duplicate, "GenericPackage.PackageUID", ordinary.fileId);
	report.insert("distinct_same_uid_package_file_candidates", project(ordinary).files.size());
	ordinary.property(duplicate, "GenericPackage.PackageUID", umid('x'));
	report.insert("contradictory_duplicate_uid_package_file_candidates", project(ordinary).files.size());
	Graph partial;
	const auto master = partial.master();
	ObjectHandle existing = 0;
	for (const auto &edge : partial.source.relationships)
		if (edge.origin == master && edge.locator.name == QLatin1String("GenericPackage.Tracks"))
			existing = edge.target;
	if (!existing)
		throw std::logic_error("Diagnostic master has no authored track.");
	for (auto &property : partial.at(master).properties)
		if (property.locator.name == QLatin1String("GenericPackage.Tracks"))
			property.decoded = QVariantList{QByteArray(16, char(existing)), QByteArray(16, 'z')};
	Relationship missing;
	missing.origin = master;
	missing.target = 0;
	missing.locator.name = QStringLiteral("GenericPackage.Tracks");
	missing.recordedReference = QByteArray(16, 'z');
	partial.source.relationships.append(missing);
	const auto parsed = project(partial);
	report.insert("partial_master_tracks_file_candidates", parsed.files.size());
	if (!parsed.files.isEmpty())
	{
		report.insert("partial_master_tracks_selected_master_count", parsed.files.first().masterMobIds.size());
		report.insert("partial_master_tracks_clip_name", value(parsed.files.first(), MediaProperty::ClipName).toString());
		report.insert("partial_master_tracks_clip_duration_count", value(parsed.files.first(), MediaProperty::ClipDuration).toList().size());
	}
	std::cout << QJsonDocument(report).toJson().constData();
}
