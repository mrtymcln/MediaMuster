// Authored post-reader graphs check F06/F07 reference and identity selection.
// This diagnostic reports observations; it does not certify genuine MXF files.

#include "mediaengine/projection.h"
#include "mediaengine/scancoordinator.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace
{
	using namespace MediaEngine;

	QByteArray umid(char material)
	{
		return QByteArray::fromHex("060a2b340101010501010f1013000000") + QByteArray(16, material);
	}

	struct Graph
	{
		ParsedSource source;
		ObjectHandle ecd = 0, file = 0, descriptor = 0, preface = 0, storage = 0;
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
			storage = object("ContentStorage", partition);
			edge(preface, "Preface.ContentStorage", storage);
			list(storage, "ContentStorage.Packages", {file});
			list(storage, "ContentStorage.EssenceContainerData", {ecd});
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
			property.encoding = encoding(value);
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

		QByteArray encoding(const QVariant &value) const
		{
			if (value.metaType().id() == QMetaType::QByteArray)
				return value.toByteArray();
			if (value.metaType().id() != QMetaType::QVariantList)
				return QByteArray(4, 'v');
			const auto references = value.toList();
			QByteArray bytes(8, '\0');
			qToBigEndian(quint32(references.size()), bytes.data());
			qToBigEndian(quint32(16), bytes.data() + 4);
			for (const auto &reference : references)
				bytes += reference.toByteArray();
			return bytes;
		}

		void reframe(ObjectHandle handle)
		{
			auto &object = at(handle);
			auto set = QSharedPointer<MxfSetContext>::create(*object.mxf);
			qint64 offset = set->value.offset;
			for (auto &property : object.properties)
			{
				auto context = QSharedPointer<MxfPropertyContext>::create(*property.mxf);
				context->framingRanges = {{offset, 4}};
				property.locator.ranges = {{offset + 4, property.encoding.size()}};
				property.mxf = context;
				offset += 4 + property.encoding.size();
			}
			set->value.length = offset - set->value.offset;
			object.mxf = set;
			refreshLinks(handle);
		}

		void refreshLinks(ObjectHandle handle)
		{
			for (auto &link : source.relationships)
				if (link.origin == handle)
					link.locator.ranges.clear();
			for (const auto &property : at(handle).properties)
			{
				const bool array = property.decoded.metaType().id() == QMetaType::QVariantList;
				const auto references = array ? property.decoded.toList() : QVariantList{property.decoded};
				for (qsizetype index = 0; index < references.size(); ++index)
					for (auto &link : source.relationships)
						if (link.origin == handle && link.locator.name == property.locator.name &&
							link.locator.ranges.isEmpty() && link.recordedReference == references[index])
						{
							link.locator.objectNumber = handle;
							link.locator.ranges = {{property.locator.ranges.first().offset +
								(array ? 8 + index * 16 : 0), 16}};
							break;
						}
			}
		}

		void edge(ObjectHandle from, const char *name, ObjectHandle to)
		{
			property(from, name, QByteArray(16, char(to)));
			Relationship relationship;
			relationship.origin = from;
			relationship.target = to;
			relationship.locator.name = QString::fromLatin1(name);
			relationship.recordedReference = QByteArray(16, char(to));
			source.relationships.append(relationship);
			refreshLinks(from);
		}

		void list(ObjectHandle from, const char *name, const QVector<ObjectHandle> &targets)
		{
			QVariantList references;
			for (const auto target : targets)
				references.append(QByteArray(16, char(target)));
			bool replaced = false;
			for (auto &item : at(from).properties)
				if (item.locator.name == QLatin1String(name))
				{
					item.decoded = references;
					item.encoding = encoding(references);
					replaced = true;
				}
			if (!replaced)
				property(from, name, references);
			reframe(from);
			for (qsizetype index = source.relationships.size(); index > 0; --index)
				if (source.relationships[index - 1].origin == from &&
					source.relationships[index - 1].locator.name == QLatin1String(name))
					source.relationships.removeAt(index - 1);
			for (const auto target : targets)
			{
				Relationship relationship;
				relationship.origin = from;
				relationship.target = target;
				relationship.locator.name = QString::fromLatin1(name);
				relationship.recordedReference = QByteArray(16, char(target));
				source.relationships.append(relationship);
			}
			refreshLinks(from);
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
			list(storage, "ContentStorage.Packages", {file, result});
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

	qsizetype ownerCount(const Projection &projection)
	{
		return std::count_if(projection.files.cbegin(), projection.files.cend(),
			[](const ProjectedFile &file) { return !file.fileMobId.isEmpty(); });
	}
}

int main()
{
	QJsonObject report;
	Graph ordinary;
	const auto duplicate = ordinary.object("SourcePackage");
	ordinary.property(duplicate, "GenericPackage.PackageUID", ordinary.fileId);
	ordinary.list(ordinary.storage, "ContentStorage.Packages", {ordinary.file, duplicate});
	report.insert("distinct_same_uid_package_file_candidates", ownerCount(project(ordinary)));
	ordinary.property(duplicate, "GenericPackage.PackageUID", umid('x'));
	const auto contradictory = project(ordinary);
	qsizetype owners = 0;
	MediaEvidence identities;
	for (const auto &file : contradictory.files)
	{
		if (!file.fileMobId.isEmpty())
			++owners;
		appendEvidence(identities, file.evidence);
	}
	selectMetadata(identities);
	report.insert("contradictory_duplicate_uid_package_file_candidates", owners);
	report.insert("contradictory_identity_is_conflicting", identities.selected(MediaProperty::FileMobId).agreement == PropertyAgreement::Conflicting);
	report.insert("contradictory_identity_selected_value", identities.selected(MediaProperty::FileMobId).value.toString());
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
		{
			property.decoded = QVariantList{QByteArray(16, char(existing)), QByteArray(16, 'z')};
			property.encoding = partial.encoding(property.decoded);
		}
	partial.reframe(master);
	Relationship missing;
	missing.origin = master;
	missing.target = 0;
	missing.locator.name = QStringLiteral("GenericPackage.Tracks");
	missing.recordedReference = QByteArray(16, 'z');
	partial.source.relationships.append(missing);
	partial.refreshLinks(master);
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
