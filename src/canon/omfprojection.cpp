// Turns the fresh MDB/legacy reader's object graph into file-owned observations.
// Dictionary names and recorded references establish ownership; object order,
// filenames and an unrelated descriptor never supply a missing association.

#include "projection.h"
#include "dnxnames_p.h"
#include "avidprecompute.h"
#include "mediametadata.h"
#include "omfresolutions.h"

#include <QHash>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Canon
{
	namespace
	{
		using Property = const RawProperty *;

		bool named(const QString &name, std::initializer_list<const char *> names)
		{
			return std::any_of(names.begin(), names.end(), [&](const char *candidate)
							   { return name == QLatin1String(candidate); });
		}

		QVector<Property> properties(const AvidObject &object, std::initializer_list<const char *> names)
		{
			QVector<Property> result;
			for (const auto &property : object.properties)
				if (named(property.locator.name, names))
					result.append(&property);
			return result;
		}

		Property unique(const AvidObject &object, std::initializer_list<const char *> names)
		{
			Property result = nullptr;
			for (const auto *property : properties(object, names))
			{
				if (property->state != PropertyReadState::Present || !property->decoded.isValid())
					return nullptr;
				if (result && result->decoded != property->decoded)
					return nullptr;
				result = property;
			}
			return result;
		}

		Property uniqueRaw(const AvidObject &object, std::initializer_list<const char *> names)
		{
			Property result = nullptr;
			for (const auto *property : properties(object, names))
			{
				if (property->state != PropertyReadState::Present || !property->bytesRetained)
					return nullptr;
				if (result && result->encoding != property->encoding)
					return nullptr;
				result = property;
			}
			return result;
		}

		QString objectClass(const AvidObject &object)
		{
			const auto *value = unique(object, {"OMFI:ObjID", "OMFI:OOBJ:ObjClass"});
			return value ? value->decoded.toString() : QString{};
		}

		bool audioClass(const QString &value)
		{
			return named(value, {"PCMA", "MPGA", "WAVE", "WAVD", "AIFD"});
		}

		bool mediaClass(const QString &value)
		{
			return audioClass(value) || named(value, {"CDCI", "MPGI", "RGBA", "JPED", "TIFD"});
		}

		bool mobClass(const QString &value)
		{
			return named(value, {"MOBJ", "SMOB", "MMOB", "CMOB"});
		}

		std::optional<qint64> integer(Property property)
		{
			if (!property || property->state != PropertyReadState::Present || !property->decoded.isValid())
				return {};
			bool ok = false;
			const auto value = property->decoded.toLongLong(&ok);
			return ok ? std::optional<qint64>(value) : std::nullopt;
		}

		MediaRate rate(Property property)
		{
			if (!property || property->state != PropertyReadState::Present)
				return {};
			const auto value = property->decoded.toMap();
			bool numeratorOk = false, denominatorOk = false;
			const auto numerator = value.value(QStringLiteral("numerator")).toLongLong(&numeratorOk);
			const auto denominator = value.value(QStringLiteral("denominator")).toLongLong(&denominatorOk);
			if (!numeratorOk || !denominatorOk || numerator <= 0 || denominator <= 0 ||
				numerator > std::numeric_limits<qint32>::max() || denominator > std::numeric_limits<qint32>::max())
				return {};
			return {qint32(numerator), qint32(denominator)};
		}

		QString identity(Property property)
		{
			if (!property || property->state != PropertyReadState::Present || !property->bento ||
				property->bento->typeName != QLatin1String("omfi:UID"))
				return {};
			const auto &bytes = property->encoding;
			if (bytes.size() == 12 && !property->bento->metadataBigEndian)
				return {};
			if (bytes.size() != 12 && bytes.size() != 32)
				return {};
			if (std::all_of(bytes.cbegin(), bytes.cend(), [](char byte)
							{ return byte == 0; }))
				return {};
			return canonicalDatabaseId(bytes, property->bento->metadataBigEndian.value_or(false));
		}

		QByteArray auidToUl(const RawProperty &property)
		{
			if (property.state != PropertyReadState::Present || property.encoding.size() != 16 ||
				!property.bento || !property.bento->metadataBigEndian)
				return {};
			const auto &bytes = property.encoding;
			QByteArray result = bytes.mid(8, 8);
			// AAF's structured AUID fields precede the UL's first eight octets.
			if (*property.bento->metadataBigEndian)
				result += bytes.first(8);
			else
			{
				for (int index : {3, 2, 1, 0, 5, 4, 7, 6})
					result += bytes[index];
			}
			return result;
		}

		QByteArray knownDnxLabel(qint64 resolution)
		{
			// The observed/registered IDs are explicit: the intervening numbers
			// are not proven codecs merely because an arithmetic label fits.
			switch (resolution)
			{
			case 1235:
			case 1237:
			case 1238:
			case 1241:
			case 1242:
			case 1243:
			case 1244:
			case 1250:
			case 1251:
			case 1252:
			case 1253:
				break;
			default:
				return {};
			}
			QByteArray label = QByteArray::fromHex("060e2b340401010a0401020271000000");
			label[13] = char(resolution - 1234);
			if (resolution == 1244)
				label[7] = 0x0d;
			return label;
		}

		MediaRate legacyDnxNamingRate(qint64 resolution, MediaRate recorded)
		{
			// Matched MDB/MXF identities establish these decimal database clocks;
			// Avid's supplied OMF slate also establishes 1243 at 29.97. This is
			// only a name-table lookup: retain the original rate for every fact
			// and duration. See Project Canon's 7 October DNx clock evidence.
			if (recorded.sameRate({2997, 100}))
			{
				switch (resolution)
				{
				case 1235:
				case 1237:
				case 1238:
				case 1241:
				case 1242:
				case 1243:
				case 1253:
					return {30000, 1001};
				}
			}
			if (recorded.sameRate({23976, 1000}))
			{
				switch (resolution)
				{
				case 1235:
				case 1237:
				case 1238:
				case 1250:
				case 1251:
				case 1252:
				case 1253:
					return {24000, 1001};
				}
			}
			return recorded;
		}

		void link(ProjectedFile &file, const ParsedSource &source, const AvidObject &object)
		{
			if (std::none_of(file.objects.cbegin(), file.objects.cend(), [&](const auto &existing)
							 { return existing.source == source.snapshot && existing.handle == object.handle; }))
				file.objects.append({source.snapshot, object.handle});
		}

		void retainAmbiguousCandidate(ProjectedFile &file, const ProjectedFile &candidate)
		{
			appendEvidence(file.evidence, candidate.evidence, false);
			file.objects += candidate.objects;
			// A positive identity disagreement is different from an unreadable ID:
			// keep competing IDs active while excluding unowned technical facts.
			for (auto value : candidate.evidence.observations(MediaProperty::FileMobId))
			{
				value.explanation += QStringLiteral(" OMF physical-file ownership is ambiguous; competing identity observations must be reconciled before use.");
				file.evidence.observe(MediaProperty::FileMobId, std::move(value));
			}
		}

		class ObjectProjection
		{
		public:
			ObjectProjection(const ParsedSource &source, const Cancellation &cancellation)
				: m_source(source), m_cancellation(cancellation)
			{
				for (const auto &object : source.objects)
				{
					if (cancellation.cancelled())
						return;
					m_objects.insert(object.handle, &object);
					if (mobClass(objectClass(object)))
					{
						const auto id = identity(unique(object, {"OMFI:MOBJ:MobID"}));
						if (!id.isEmpty())
							m_mobs[id].append(object.handle);
					}
				}
				for (const auto &edge : source.relationships)
				{
					if (cancellation.cancelled())
						return;
					m_edges[edge.origin].append(&edge);
				}
			}

			Projection project(bool database)
			{
				Projection result;
				QHash<ObjectHandle, QVector<qsizetype>> fileSubjects;
				QHash<qsizetype, QVariantList> clipDurations;
				for (const auto &object : m_source.objects)
				{
					if (m_cancellation.cancelled())
						return result;
					if (!mobClass(objectClass(object)))
						continue;
					const auto *mobId = unique(object, {"OMFI:MOBJ:MobID"});
					const QString id = identity(mobId);
					if (id.isEmpty())
						continue;
					for (const auto *descriptor : targets(object.handle, {"OMFI:MOBJ:PhysicalMedia", "OMFI:SMOB:MediaDescription"}))
					{
						if (!mediaClass(objectClass(*descriptor)))
							continue;
						ProjectedFile file;
						file.fileMobId = id;
						link(file, m_source, object);
						link(file, m_source, *descriptor);
						observe(file, MediaProperty::FileMobId, m_source, object, *mobId, id, EvidenceBasis::Derived,
								QStringLiteral("Canonical identity of the mob owning this media descriptor; original UID retained."));
						technical(file, *descriptor, mobId->encoding.size() == 12, editRate(object));
						timecode(file, object);
						attributes(file, object);
						for (const auto referenced : sourceMobs(object))
							if (const auto *sourceMob = m_objects.value(referenced))
								attributes(file, *sourceMob, true);
						fileSubjects[object.handle].append(result.files.size());
						result.files.append(std::move(file));
					}
				}

				for (const auto &master : m_source.objects)
				{
					if (m_cancellation.cancelled())
						return result;
					if (!isMaster(master))
						continue;
					const auto *masterProperty = unique(master, {"OMFI:MOBJ:MobID"});
					const auto masterId = identity(masterProperty);
					if (masterId.isEmpty())
						continue;
					ProjectedFile masterFacts;
					masterFacts.masterMobIds.append(masterId);
					link(masterFacts, m_source, master);
					observe(masterFacts, MediaProperty::MasterMobId, m_source, master, *masterProperty, masterId,
							EvidenceBasis::Derived, QStringLiteral("Canonical identity of this recorded master object."));
					for (const auto *name : properties(master, {"OMFI:CPNT:Name", "OMFI:MOBJ:Name"}))
						text(masterFacts, MediaProperty::ClipName, master, *name);
					attributes(masterFacts, master);
					classification(masterFacts, master);
					for (const auto handle : sourceMobs(master))
						for (const auto index : fileSubjects.value(handle))
						{
							auto &file = result.files[index];
							if (!file.masterMobIds.contains(masterId))
								file.masterMobIds.append(masterId);
							link(file, m_source, master);
							observe(file, MediaProperty::MasterMobId, m_source, master, *masterProperty, masterId,
									EvidenceBasis::Derived, QStringLiteral("This master's source-clip graph references the file mob."));
							appendEvidence(file.evidence, masterFacts.evidence);
							clipDurations[index] += trackDurations(master, *m_objects.value(handle), file);
						}
					result.masters.append(std::move(masterFacts));
				}
				for (qsizetype index = 0; index < result.files.size(); ++index)
				{
					auto &file = result.files[index];
					file.masterMobIds.sort();
					if (clipDurations.value(index).isEmpty())
						continue;
					const auto *owner = m_objects.value(file.objects.first().handle);
					const auto *id = unique(*owner, {"OMFI:MOBJ:MobID"});
					observe(file, MediaProperty::ClipDuration, m_source, *owner, *id, clipDurations.value(index), EvidenceBasis::Derived,
							QStringLiteral("Independent recorded master-track lengths linked to this file; unknown lengths, clocks or drop-frame status are not invented."));
				}
				if (database)
					return result;

				QSet<QString> mediaIds;
				bool mediaIdentityRecorded = false;
				bool uncertainMediaIdentity = false;
				for (const auto &object : m_source.objects)
					for (const auto *property : properties(object, {"OMFI:MDAT:MobID", "OMFI:WAVE:MobID", "OMFI:AIFC:MobID"}))
					{
						mediaIdentityRecorded = true;
						const auto id = identity(property);
						if (!id.isEmpty())
							mediaIds.insert(id);
						else
							uncertainMediaIdentity = true;
					}
				QVector<ProjectedFile> eligible;
				QSet<QString> eligibleIds;
				for (auto &file : result.files)
					if (!mediaIdentityRecorded || mediaIds.contains(file.fileMobId))
					{
						eligibleIds.insert(file.fileMobId);
						eligible.append(std::move(file));
					}
				if (!uncertainMediaIdentity && eligibleIds.size() == 1 && (!mediaIdentityRecorded || mediaIds.size() == 1))
					result.files = std::move(eligible);
				else
				{
					if (!result.files.isEmpty() || mediaIdentityRecorded)
						result.diagnostics.append(QStringLiteral("OMF physical-file ownership is absent or ambiguous; no unrelated file mob was selected."));
					ProjectedFile ambiguous;
					// Candidates moved to eligible are still observations, even though
					// their ownership could not be selected.
					for (const auto &candidate : result.files)
						retainAmbiguousCandidate(ambiguous, candidate);
					for (const auto &candidate : eligible)
						retainAmbiguousCandidate(ambiguous, candidate);
					for (const auto &object : m_source.objects)
						for (const auto *property : properties(object, {"OMFI:MDAT:MobID", "OMFI:WAVE:MobID", "OMFI:AIFC:MobID"}))
						{
							const auto id = identity(property);
							if (!id.isEmpty())
								observe(ambiguous, MediaProperty::FileMobId, m_source, object, *property, id, EvidenceBasis::Derived,
										QStringLiteral("Recorded media-data ownership disagrees with the available file-mob candidates; original identity retained."));
						}
					result.files.clear();
					if (!ambiguous.evidence.observations(MediaProperty::FileMobId).isEmpty())
						result.files.append(std::move(ambiguous));
				}
				return result;
			}

		private:
			QVector<const AvidObject *> targets(ObjectHandle handle, std::initializer_list<const char *> names) const
			{
				QVector<const AvidObject *> result;
				for (const auto *edge : m_edges.value(handle))
					if (edge->target && named(edge->locator.name, names))
						if (const auto *object = m_objects.value(edge->target))
							result.append(object);
				return result;
			}

			bool isMaster(const AvidObject &object) const
			{
				const auto cls = objectClass(object);
				if (cls == QLatin1String("MMOB"))
					return true;
				if (cls != QLatin1String("MOBJ") || !properties(object, {"OMFI:MOBJ:PhysicalMedia"}).isEmpty())
					return false;
				const auto usage = integer(unique(object, {"OMFI:MOBJ:UsageCode"}));
				return usage && (*usage == 1 || *usage == 7);
			}

			MediaRate editRate(const AvidObject &object) const
			{
				const auto direct = properties(object, {"OMFI:CPNT:EditRate"});
				if (!direct.isEmpty())
					return rate(unique(object, {"OMFI:CPNT:EditRate"}));
				MediaRate result;
				for (const auto *slot : targets(object.handle, {"OMFI:MOBJ:Slots"}))
				{
					const auto current = rate(unique(*slot, {"OMFI:MSLT:EditRate"}));
					if (!current.valid() || (result.valid() && !result.sameRate(current)))
						return {};
					result = current;
				}
				return result;
			}

			bool completeReferences(const AvidObject &object, const char *name, bool nullAllowed = false) const
			{
				const auto values = properties(object, {name});
				if (values.isEmpty())
					return false;
				quint64 expected = 0, observed = 0;
				for (const auto *value : values)
				{
					if (value->state != PropertyReadState::Present || !value->bento ||
						(value->bento->typeName != QLatin1String("omfi:ObjRef") && value->bento->typeName != QLatin1String("omfi:ObjRefArray")))
						return false;
					if (value->bento->typeName == QLatin1String("omfi:ObjRef"))
						++expected;
					else
					{
						if (value->encoding.size() < 2 || !value->bento->metadataBigEndian)
							return false;
						const auto count = *value->bento->metadataBigEndian ? qFromBigEndian<quint16>(value->encoding.constData())
																			: qFromLittleEndian<quint16>(value->encoding.constData());
						if (count == 0xffff)
							return false; // Retained extended count needs a separate absence proof.
						expected += count;
					}
				}
				for (const auto *edge : m_edges.value(object.handle))
					if (edge->locator.name == QLatin1String(name))
					{
						++observed;
						if (!edge->target)
						{
							const auto recorded = edge->recordedReference.toMap();
							if (!nullAllowed || recorded.value(QStringLiteral("key")).toUInt() != 0)
								return false;
						}
					}
				return observed == expected;
			}

			QVector<const AvidObject *> components(const AvidObject &owner) const
			{
				QVector<ObjectHandle> pending{owner.handle};
				QVector<const AvidObject *> result;
				QSet<ObjectHandle> visited;
				while (!pending.isEmpty())
				{
					if (m_cancellation.cancelled())
						return result;
					const auto handle = pending.takeLast();
					if (visited.contains(handle))
						continue;
					visited.insert(handle);
					const auto *object = m_objects.value(handle);
					if (!object)
						continue;
					result.append(object);
					for (const auto *target : targets(handle, {"OMFI:TRKG:Tracks", "OMFI:TRAK:TrackComponent", "OMFI:SEQU:Sequence",
															   "OMFI:MOBJ:Slots", "OMFI:MSLT:Segment", "OMFI:SEQU:Components", "OMFI:NEST:Slots",
															   "OMFI:SLCT:Selected", "OMFI:SLCT:Alternates", "OMFI:MGRP:Choices", "OMFI:ERAT:InputSegment"}))
						pending.append(target->handle);
				}
				return result;
			}

			QVector<ObjectHandle> sourceMobs(const AvidObject &mob) const
			{
				QVector<ObjectHandle> result;
				for (const auto *object : components(mob))
					if (objectClass(*object) == QLatin1String("SCLP"))
						for (const auto *sourceId : properties(*object, {"OMFI:SCLP:SourceID"}))
							for (const auto target : m_mobs.value(identity(sourceId)))
								if (target != mob.handle && !result.contains(target))
									result.append(target);
				return result;
			}

			void timecode(ProjectedFile &file, const AvidObject &owner)
			{
				QVector<ObjectHandle> pending{owner.handle};
				QSet<ObjectHandle> visited;
				while (!pending.isEmpty())
				{
					if (m_cancellation.cancelled())
						return;
					const auto handle = pending.takeLast();
					if (visited.contains(handle))
						continue;
					visited.insert(handle);
					const auto *mob = m_objects.value(handle);
					if (!mob)
						continue;
					for (const auto *component : components(*mob))
						if (objectClass(*component) == QLatin1String("TCCP"))
							for (const auto *property : properties(*component, {"OMFI:TCCP:Flags", "OMFI:TCCP:Drop"}))
							{
								const auto value = integer(property);
								if (value && (*value == 0 || *value == 1))
								{
									link(file, m_source, *component);
									observe(file, MediaProperty::DropFrame, m_source, *component, *property, *value == 1,
											EvidenceBasis::Recorded, QStringLiteral("Explicit OMF timecode drop-frame flag on this file's source graph; OMF toolkit omMobGet.c/omfiTimecodeGetInfo defines 0 and 1. No frame-rate inference."));
								}
							}
					pending += sourceMobs(*mob);
				}
			}

			struct ComponentLength
			{
				qint64 units = 0;
				QString property;
				QVariantList components;
			};

			std::optional<ComponentLength> legacyComponentLength(const AvidObject &component, MediaRate clock) const
			{
				// OMF toolkit omMobGet.c/Get1xSequLength adds segments and subtracts
				// transitions. OMF1 sequences have no CLIP:Length of their own.
				// Walk iteratively so nested sequences cannot exhaust the C++ stack.
				struct Visit
				{
					const AvidObject *object;
					bool expanded;
				};
				QVector<Visit> pending{{&component, false}};
				QSet<ObjectHandle> active;
				QHash<ObjectHandle, qint64> lengths;
				QVariantList provenance;
				QString rootProperty;
				while (!pending.isEmpty())
				{
					if (m_cancellation.cancelled())
						return {};
					const auto visit = pending.takeLast();
					const auto &object = *visit.object;
					if (lengths.contains(object.handle))
						continue;
					if (!rate(unique(object, {"OMFI:CPNT:EditRate"})).sameRate(clock))
						return {};
					const auto cls = objectClass(object);
					const bool sequence = cls == QLatin1String("SEQU");
					if (!visit.expanded && sequence)
					{
						if (active.contains(object.handle) || properties(object, {"OMFI:SEQU:Sequence"}).size() != 1 ||
							!completeReferences(object, "OMFI:SEQU:Sequence"))
							return {};
						active.insert(object.handle);
						pending.append({&object, true});
						const auto children = targets(object.handle, {"OMFI:SEQU:Sequence"});
						for (auto it = children.crbegin(); it != children.crend(); ++it)
							pending.append({*it, false});
						continue;
					}
					qint64 units = 0;
					QString property;
					QVariantList contributions;
					if (sequence)
					{
						property = QStringLiteral("OMFI:SEQU:Sequence");
						for (const auto *child : targets(object.handle, {"OMFI:SEQU:Sequence"}))
						{
							const auto found = lengths.constFind(child->handle);
							if (found == lengths.cend())
								return {};
							const bool subtract = objectClass(*child) == QLatin1String("TRAN");
							if (subtract ? units < std::numeric_limits<qint64>::min() + *found
										 : units > std::numeric_limits<qint64>::max() - *found)
								return {};
							units += subtract ? -*found : *found;
							contributions.append(QVariantMap{{QStringLiteral("Object"), child->handle},
															 {QStringLiteral("Units"), *found},
															 {QStringLiteral("Subtract"), subtract}});
						}
					}
					else
					{
						const bool transition = cls == QLatin1String("TRAN");
						if (!transition && !named(cls, {"SCLP", "FILL", "TCCP", "ECCP"}))
							return {}; // Unknown component length semantics remain unclaimed.
						const auto *length = unique(object, {transition ? "OMFI:TRKG:GroupLength" : "OMFI:CLIP:Length"});
						const auto count = integer(length);
						if (!count)
							return {};
						units = *count;
						property = length->locator.name;
					}
					if (units < 0)
						return {};
					lengths.insert(object.handle, units);
					active.remove(object.handle);
					provenance.append(QVariantMap{{QStringLiteral("Object"), object.handle}, {QStringLiteral("Property"), property}, {QStringLiteral("Units"), units}, {QStringLiteral("Rate"), rateValue(clock)}, {QStringLiteral("Contributions"), contributions}});
					if (object.handle == component.handle)
						rootProperty = property;
				}
				return ComponentLength{lengths.value(component.handle), rootProperty,
									   objectClass(component) == QLatin1String("SEQU") ? provenance : QVariantList{}};
			}

			QVariantList trackDurations(const AvidObject &master, const AvidObject &fileMob, const ProjectedFile &file) const
			{
				QVariantList result;
				const auto masterId = identity(unique(master, {"OMFI:MOBJ:MobID"}));
				const auto drop = file.evidence.resolve(MediaProperty::DropFrame, [](MetadataSource)
														{ return 1; }, QStringLiteral("Associated OMF timecode values"));
				const auto kind = file.evidence.resolve(MediaProperty::Kind, [](MetadataSource)
														{ return 1; }, QStringLiteral("Associated descriptor kind"));
				const bool video = kind.value.isValid() && kind.value.toInt() == 0;
				const auto fileClock = editRate(fileMob);
				for (const auto *track : targets(master.handle, {"OMFI:TRKG:Tracks", "OMFI:MOBJ:Slots"}))
				{
					if (!sourceMobs(*track).contains(fileMob.handle))
						continue;
					const bool omf1 = objectClass(*track) == QLatin1String("TRAK");
					std::optional<qint64> trackId;
					if (omf1)
						trackId = integer(unique(*track, {"OMFI:TRAK:LabelNumber"}));
					else if (objectClass(*track) == QLatin1String("MSLT"))
					{
						const auto descriptors = targets(track->handle, {"OMFI:MSLT:TrackDesc"});
						if (descriptors.size() == 1 && objectClass(*descriptors.first()) == QLatin1String("TRKD"))
							trackId = integer(unique(*descriptors.first(), {"OMFI:TRKD:TrackID"}));
					}
					if (!trackId || *trackId < 0 || *trackId > std::numeric_limits<quint32>::max())
						continue;
					for (const auto *segment : targets(track->handle, {"OMFI:TRAK:TrackComponent", "OMFI:MSLT:Segment"}))
					{
						const auto clock = rate(omf1 ? unique(*segment, {"OMFI:CPNT:EditRate"}) : unique(*track, {"OMFI:MSLT:EditRate"}));
						if (!clock.valid())
							continue;
						std::optional<ComponentLength> length;
						if (omf1)
							length = legacyComponentLength(*segment, clock);
						else if (const auto *property = unique(*segment, {"OMFI:CPNT:Length"}))
							if (const auto count = integer(property); count && *count >= 0)
								length = ComponentLength{*count, property->locator.name, {}};
						if (!length)
							continue;
						QVariantMap duration{{QStringLiteral("MasterMobId"), masterId}, {QStringLiteral("TrackId"), quint32(*trackId)}, {QStringLiteral("DurationObject"), segment->handle}, {QStringLiteral("DurationProperty"), length->property}, {QStringLiteral("Duration"), durationValue({length->units, clock, fileClock.valid() ? fileClock : video ? clock
																																																																																							 : MediaRate{},
																																																																								 MediaDuration::Source::ClipReference})}};
						if (!length->components.isEmpty())
							duration.insert(QStringLiteral("DurationComponents"), length->components);
						if (drop.value.isValid())
							duration.insert(QStringLiteral("DropFrame"), drop.value);
						result.append(std::move(duration));
					}
				}
				return result;
			}

			void text(ProjectedFile &file, MediaProperty field, const AvidObject &object, const RawProperty &property)
			{
				const auto interpreted = withInferredText(property, 0, true);
				if (interpreted.state == PropertyReadState::Present && interpreted.decoded.metaType().id() == QMetaType::QString &&
					!interpreted.decoded.toString().isEmpty())
					observe(file, field, m_source, object, interpreted, interpreted.decoded);
			}

			void attributes(ProjectedFile &file, const AvidObject &owner, bool projectOnly = false)
			{
				QVector<const AvidObject *> pending = targets(owner.handle, {"OMFI:CPNT:Attributes", "OMFI:MOBJ:UserAttributes"});
				QSet<ObjectHandle> visited;
				while (!pending.isEmpty())
				{
					if (m_cancellation.cancelled())
						return;
					const auto *attributes = pending.takeLast();
					if (visited.contains(attributes->handle))
						continue;
					visited.insert(attributes->handle);
					for (const auto *entry : targets(attributes->handle, {"OMFI:ATTR:AttrRefs"}))
					{
						const auto *name = unique(*entry, {"OMFI:ATTB:Name"});
						const auto kind = integer(unique(*entry, {"OMFI:ATTB:Kind"}));
						if (!name || !kind)
							continue;
						const auto key = name->decoded.toString();
						if (*kind == 2)
						{
							for (const auto *value : properties(*entry, {"OMFI:ATTB:StringAttribute"}))
							{
								if (key == QLatin1String("_PJ"))
									text(file, MediaProperty::Project, *entry, *value);
								else if (!projectOnly && (key == QLatin1String("Video") || key == QLatin1String("Audio")))
									text(file, MediaProperty::SourceContainer, *entry, *value);
								else if (!projectOnly && key == QLatin1String("UNC Path"))
									sourcePath(file, *entry, *value);
							}
							continue;
						}
						if (*kind != 3)
							continue;
						if (key == QLatin1String("_IMPORTSETTING") && !projectOnly)
							observe(file, MediaProperty::Imported, m_source, *entry, *name, true,
									EvidenceBasis::Derived, QStringLiteral("Recorded _IMPORTSETTING object attribute."));
						for (const auto *target : targets(entry->handle, {"OMFI:ATTB:ObjAttribute"}))
						{
							if (key == QLatin1String("_USER") || key == QLatin1String("_IMPORTSETTING"))
								pending.append(target);
							else if (!projectOnly && key == QLatin1String("_ORG_BIN") && objectClass(*target) == QLatin1String("MCBR"))
								originalBin(file, *target);
							else if (!projectOnly && key == QLatin1String("_SRCFILE") &&
									 named(objectClass(*target), {"MACL", "WINL", "UNXL"}))
							{
								for (const auto *value : properties(*target, {"OMFI:FL:POSIXPathName", "OMFI:FL:PathNameUTF8",
																			  "OMFI:FL:PathName", "OMFI:UNXL:PathName", "OMFI:WINL:PathName", "OMFI:MACL:PathName"}))
									sourcePath(file, *target, *value);
							}
						}
					}
				}
			}

			void originalBin(ProjectedFile &file, const AvidObject &object)
			{
				const auto modern = properties(object, {"OMFI:MCBR:MC:binNameUTF8"});
				const bool usableModern = std::any_of(modern.cbegin(), modern.cend(), [](Property value)
													  { return value->state == PropertyReadState::Present && value->decoded.metaType().id() == QMetaType::QString &&
															   !value->decoded.toString().isEmpty(); });
				// An empty or damaged modern field does not erase a readable legacy
				// name. Keep both observations, including the one not selected.
				for (const auto *property : properties(object, {"OMFI:MCBR:MC:binNameUTF8", "OMFI:MCBR:MC:binName"}))
				{
					const auto interpreted = withInferredText(*property, 0, true);
					auto value = observation(m_source, object, interpreted, interpreted.decoded);
					const bool isModern = property->locator.name == QLatin1String("OMFI:MCBR:MC:binNameUTF8");
					value.eligible = isModern == usableModern;
					if (!value.eligible)
						value.explanation += isModern
												 ? QStringLiteral(" The modern bin-name field is empty or unreadable; the legacy name may supply the value.")
												 : QStringLiteral(" A non-empty UTF-8 bin name takes precedence over the legacy name.");
					file.evidence.observe(MediaProperty::OriginalBin, std::move(value));
				}
			}

			void sourcePath(ProjectedFile &file, const AvidObject &object, const RawProperty &property)
			{
				const auto interpreted = withInferredText(property, 0, true);
				text(file, MediaProperty::SourcePath, object, interpreted);
				if (interpreted.decoded.metaType().id() != QMetaType::QString || interpreted.decoded.toString().isEmpty())
					return;
				observe(file, MediaProperty::SourceFilename, m_source, object, interpreted,
						MediaMetadataUtil::sourceFileBaseName(interpreted.decoded.toString()), EvidenceBasis::Derived,
						QStringLiteral("Basename of this recorded import path, using either operating system's separators."));
			}

			void classification(ProjectedFile &file, const AvidObject &master)
			{
				const auto *property = unique(master, {"OMFI:MOBJ:UsageCode"});
				const auto usage = integer(property);
				if (!usage || (*usage != 1 && *usage != 7))
					return;
				observe(file, MediaProperty::Type, m_source, master, *property, *usage == 1 ? 1 : 0,
						EvidenceBasis::Derived, QStringLiteral("The associated master's recorded usage is precompute (1) or master media (7)."));
				if (*usage != 1 || objectClass(master) != QLatin1String("MOBJ"))
					return;
				// Match the verified Avid predicate only when its direct attributes
				// and direct track kinds are fully established.
				AvidPrecompute::Evidence evidence;
				const auto attributes = targets(master.handle, {"OMFI:CPNT:Attributes"});
				bool complete = m_source.outcome == ParsedSource::Outcome::Complete;
				if (!properties(master, {"OMFI:CPNT:Attributes"}).isEmpty() &&
					!completeReferences(master, "OMFI:CPNT:Attributes", true))
					complete = false;
				bool imported = false;
				for (const auto *list : attributes)
				{
					if (objectClass(*list) != QLatin1String("ATTR") || !completeReferences(*list, "OMFI:ATTR:AttrRefs"))
					{
						complete = false;
						continue;
					}
					for (const auto *entry : targets(list->handle, {"OMFI:ATTR:AttrRefs"}))
					{
						if (objectClass(*entry) != QLatin1String("ATTB"))
						{
							complete = false;
							continue;
						}
						const auto *name = unique(*entry, {"OMFI:ATTB:Name"});
						const auto kind = integer(unique(*entry, {"OMFI:ATTB:Kind"}));
						if (!name || !kind || *kind == 0)
						{
							complete = false;
							continue;
						}
						if (name->decoded.toString() == QLatin1String("_IMPORTSETTING") && *kind == 3)
							imported = true;
					}
				}
				if (!complete)
					return;
				evidence.importAttribute = imported ? AvidPrecompute::ImportAttribute::Present : AvidPrecompute::ImportAttribute::Absent;
				if (imported)
				{
					evidence.videoTrackCount = 0;
					if (!completeReferences(master, "OMFI:TRKG:Tracks"))
						return;
					for (const auto *track : targets(master.handle, {"OMFI:TRKG:Tracks"}))
					{
						const auto components = targets(track->handle, {"OMFI:TRAK:TrackComponent"});
						if (objectClass(*track) != QLatin1String("TRAK") || components.size() != 1 ||
							!completeReferences(*track, "OMFI:TRAK:TrackComponent"))
							return;
						const auto kind = integer(unique(*components.first(), {"OMFI:CPNT:TrackKind"}));
						if (!kind)
							return;
						if (*kind == 1)
							++evidence.videoTrackCount;
					}
				}
				const auto category = AvidPrecompute::classify(evidence);
				if (category != AvidPrecompute::Category::Unknown)
					observe(file, MediaProperty::PrecomputeCategory, m_source, master, *property, int(category), EvidenceBasis::Derived,
							QStringLiteral("Avid's direct import-attribute/direct video-track predicate; not an effect-name guess."));
			}

			void technical(ProjectedFile &file, const AvidObject &descriptor, bool legacyIdentity, MediaRate editRate);

			const ParsedSource &m_source;
			const Cancellation &m_cancellation;
			QHash<ObjectHandle, const AvidObject *> m_objects;
			QHash<ObjectHandle, QVector<const Relationship *>> m_edges;
			QHash<QString, QVector<ObjectHandle>> m_mobs;
		};

		struct AudioFacts
		{
			int channels = 0;
			int bits = 0;
			MediaRate sampleRate;
			std::optional<qint64> frames;
			QString codec;
			QString representation;
			quint16 blockAlign = 0;
			bool uncompressed = false;
		};

		AudioFacts waveFormat(QByteArrayView bytes)
		{
			AudioFacts result;
			if (bytes.size() < 16)
				return result;
			const auto u16 = [&](qsizetype offset)
			{ return qFromLittleEndian<quint16>(bytes.data() + offset); };
			quint16 format = u16(0);
			result.channels = u16(2);
			const quint32 sampleRate = qFromLittleEndian<quint32>(bytes.data() + 4);
			if (sampleRate <= quint32(std::numeric_limits<qint32>::max()))
				result.sampleRate = {qint32(sampleRate), 1};
			result.blockAlign = u16(12);
			result.bits = u16(14);
			if (format == 0xfffe)
			{
				if (bytes.size() < 40 || u16(16) < 22 || u16(16) > bytes.size() - 18)
					return result;
				const QByteArray guid = bytes.mid(24, 16).toByteArray();
				if (guid == QByteArray::fromHex("0100000000001000800000aa00389b71"))
					format = 1;
				else if (guid == QByteArray::fromHex("0300000000001000800000aa00389b71"))
					format = 3;
				else
					return result;
				const auto validBits = u16(18);
				if (validBits > 0 && validBits <= result.bits)
					result.bits = validBits;
			}
			if (format == 1)
			{
				result.codec = QStringLiteral("PCM");
				result.representation = QStringLiteral("Integer");
				result.uncompressed = true;
			}
			else if (format == 3)
			{
				result.codec = QStringLiteral("IEEE float");
				result.representation = QStringLiteral("Float");
				result.uncompressed = true;
			}
			return result;
		}

		MediaRate extendedRate(QByteArrayView bytes)
		{
			if (bytes.size() != 10)
				return {};
			const quint16 signExponent = qFromBigEndian<quint16>(bytes.data());
			if ((signExponent & 0x8000) || (signExponent & 0x7fff) == 0x7fff)
				return {};
			quint64 significand = qFromBigEndian<quint64>(bytes.data() + 2);
			if (!significand)
				return {};
			int power = int(signExponent & 0x7fff) - 16383 - 63;
			if ((signExponent & 0x7fff) == 0)
				++power;
			while (!(significand & 1))
			{
				significand >>= 1;
				++power;
			}
			const auto maximum = quint64(std::numeric_limits<qint32>::max());
			if (power >= 0)
			{
				if (power > 30 || significand > (maximum >> power))
					return {};
				return {qint32(significand << power), 1};
			}
			if (power < -30 || significand > maximum)
				return {};
			return {qint32(significand), qint32(quint32(1) << -power)};
		}

		AudioFacts aiffCommon(QByteArrayView bytes, bool compressed)
		{
			AudioFacts result;
			if (bytes.size() < 18)
				return result;
			result.channels = qFromBigEndian<qint16>(bytes.data());
			result.frames = qFromBigEndian<quint32>(bytes.data() + 2);
			result.bits = qFromBigEndian<qint16>(bytes.data() + 6);
			result.sampleRate = extendedRate(bytes.mid(8, 10));
			const auto codec = compressed && bytes.size() >= 22 ? bytes.mid(18, 4).toByteArray() : QByteArray{};
			if (!compressed || codec == "NONE" || codec == "twos" || codec == "sowt" || codec == "in24" || codec == "in32")
			{
				result.codec = QStringLiteral("PCM");
				result.representation = QStringLiteral("Integer");
				result.uncompressed = true;
			}
			else if (codec == "fl32" || codec == "FL32" || codec == "fl64" || codec == "FL64")
			{
				result.codec = QStringLiteral("IEEE float");
				result.representation = QStringLiteral("Float");
				result.uncompressed = true;
			}
			return result;
		}

		AudioFacts audioSummary(QByteArrayView bytes)
		{
			if (bytes.size() < 12)
				return {};
			const bool wave = bytes.first(4) == QByteArrayView("RIFF") && bytes.mid(8, 4) == QByteArrayView("WAVE");
			const bool aiff = bytes.first(4) == QByteArrayView("FORM") &&
							  (bytes.mid(8, 4) == QByteArrayView("AIFF") || bytes.mid(8, 4) == QByteArrayView("AIFC"));
			if (!wave && !aiff)
				return {};
			// Summaries deliberately omit samples and can advertise the full
			// recording extent. Validate each copied metadata chunk, not that extent.
			for (qsizetype offset = 12; offset <= bytes.size() - 8;)
			{
				const auto key = bytes.mid(offset, 4);
				const quint32 length = wave ? qFromLittleEndian<quint32>(bytes.data() + offset + 4)
											: qFromBigEndian<quint32>(bytes.data() + offset + 4);
				if (key == QByteArrayView("data") || key == QByteArrayView("SSND"))
					return {};
				if (length > quint64(bytes.size() - offset - 8))
					return {};
				const auto payload = bytes.mid(offset + 8, length);
				if (wave && key == QByteArrayView("fmt "))
					return waveFormat(payload);
				if (aiff && key == QByteArrayView("COMM"))
					return aiffCommon(payload, bytes.mid(8, 4) == QByteArrayView("AIFC"));
				offset += 8 + qsizetype(length) + (length & 1);
			}
			return {};
		}

		void audioObservations(ProjectedFile &file, const ParsedSource &source, const AvidObject &object,
							   const RawProperty &property, const AudioFacts &audio, bool duration)
		{
			const auto reason = QStringLiteral("Decoded fields in the recorded audio format header; original header bytes retained.");
			if (audio.channels > 0)
				observe(file, MediaProperty::Channels, source, object, property, audio.channels, EvidenceBasis::Derived, reason);
			if (audio.bits > 0 && audio.bits <= 64)
				observe(file, MediaProperty::BitDepth, source, object, property, QStringLiteral("%1-bit").arg(audio.bits), EvidenceBasis::Derived, reason);
			if (audio.sampleRate.valid())
				observe(file, MediaProperty::SampleRate, source, object, property, rateValue(audio.sampleRate), EvidenceBasis::Derived, reason);
			if (!audio.codec.isEmpty())
				observe(file, MediaProperty::Codec, source, object, property, audio.codec, EvidenceBasis::Derived, reason);
			if (!audio.representation.isEmpty())
				observe(file, MediaProperty::SampleFormat, source, object, property, audio.representation, EvidenceBasis::Derived, reason);
			if (duration && audio.frames && *audio.frames >= 0 && audio.sampleRate.valid())
				observe(file, MediaProperty::FileDuration, source, object, property,
						durationValue({*audio.frames, audio.sampleRate, {}, MediaDuration::Source::Descriptor}), EvidenceBasis::Derived,
						QStringLiteral("Recorded sample-frame count and exact native sampling rate; no video clock inferred."));
		}

		void ObjectProjection::technical(ProjectedFile &file, const AvidObject &descriptor, bool legacyIdentity, MediaRate editRate)
		{
			const auto cls = objectClass(descriptor);
			const bool audio = audioClass(cls);
			const auto *classProperty = unique(descriptor, {"OMFI:ObjID", "OMFI:OOBJ:ObjClass"});
			if (!classProperty)
				return;
			observe(file, MediaProperty::Kind, m_source, descriptor, *classProperty, audio ? 1 : 0,
					EvidenceBasis::Derived, QStringLiteral("Established media descriptor class."));
			const auto *sampleRate = unique(descriptor, {"OMFI:MDFL:SampleRate"});
			MediaRate unitsRate = rate(sampleRate);
			if (unitsRate.valid())
				observe(file, audio ? MediaProperty::SampleRate : MediaProperty::FrameRate, m_source, descriptor, *sampleRate, rateValue(unitsRate));
			Property summary = nullptr;
			AudioFacts summaryFacts;
			if (audio)
			{
				summary = uniqueRaw(descriptor, {"OMFI:WAVD:Summary", "OMFI:AIFD:Summary"});
				if (summary)
				{
					summaryFacts = audioSummary(summary->encoding);
					// Codec is the actual encoding; WAVE/AIFF is retained as the
					// descriptor/container fact, not substituted for compression.
					audioObservations(file, m_source, descriptor, *summary, summaryFacts, false);
					if (!unitsRate.valid())
						unitsRate = summaryFacts.sampleRate;
				}
				for (const auto *channels : properties(descriptor, {"OMFI:MDAU:NumChannels"}))
					if (const auto value = integer(channels); value && *value > 0)
						observe(file, MediaProperty::Channels, m_source, descriptor, *channels, *value);
			}

			const auto *length = unique(descriptor, {"OMFI:MDFL:Length"});
			if (const auto count = integer(length); count && *count >= 0 && unitsRate.valid())
				observe(file, MediaProperty::FileDuration, m_source, descriptor, *length,
						durationValue({*count, unitsRate, audio ? editRate : unitsRate, MediaDuration::Source::Descriptor}),
						EvidenceBasis::Derived, QStringLiteral("Descriptor Length in its recorded SampleRate clock; any separate display clock comes from this file mob's explicit CPNT/MSLT EditRate. Master/reference lengths do not replace it."));

			const auto *coding = uniqueRaw(descriptor, {"OMFI:DIDD:EssenceCompression"});
			const auto layout = integer(unique(descriptor, {"OMFI:DIDD:FrameLayout"}));
			const bool omf1 = !properties(descriptor, {"OMFI:ObjID"}).isEmpty();
			const auto *storedWidthProperty = unique(descriptor, {"OMFI:DIDD:StoredWidth"});
			const auto storedWidth = integer(storedWidthProperty);
			const auto storedHeight = integer(unique(descriptor, {"OMFI:DIDD:StoredHeight"}));
			qint64 frameHeight = storedHeight.value_or(0);
			if (layout && (*layout == 1 || (omf1 && *layout == 3)))
				frameHeight = frameHeight > 0 && frameHeight <= std::numeric_limits<qint64>::max() / 2 ? frameHeight * 2 : 0;
			QByteArray label = coding ? auidToUl(*coding) : QByteArray{};
			const auto *resolutionId = unique(descriptor, {"OMFI:DIDD:DIDResolutionID"});
			const auto resolution = integer(resolutionId);
			Property codecProperty = coding;
			if (label.isEmpty() && properties(descriptor, {"OMFI:DIDD:EssenceCompression"}).isEmpty() && resolution)
			{
				label = knownDnxLabel(*resolution);
				codecProperty = resolutionId;
			}
			QString codec;
			if (!label.isEmpty())
			{
				observe(file, MediaProperty::CompressionLabel, m_source, descriptor, *codecProperty, label, EvidenceBasis::Derived,
						coding ? QStringLiteral("Recorded structured AUID converted to its UL octet order.") : QStringLiteral("Verified Avid DNx resolution-ID mapping; not a recorded compression UL."));
				codec = MediaMetadataUtil::codecFromCompressionLabel(label, {});
				for (const auto &profile : Detail::dnxProfiles)
				{
					if (label != QByteArray::fromHex(profile.label))
						continue;
					codec = QStringLiteral("Avid DNx %1").arg(QLatin1String(profile.level));
					observe(file, MediaProperty::NewDnx, m_source, descriptor, *codecProperty, codec, EvidenceBasis::Derived,
							QStringLiteral("Verified coding profile mapped to Avid's unified DNx branding."));
					observe(file, MediaProperty::OldDnx, m_source, descriptor, *codecProperty,
							QStringLiteral("DNx%1 %2").arg(profile.hr ? QLatin1String("HR") : QLatin1String("HD"), QLatin1String(profile.level)),
							EvidenceBasis::Derived, QStringLiteral("Historical HD/HR profile identity; not inferred from image dimensions."));
					const auto namingRate = resolution && label == knownDnxLabel(*resolution)
												? legacyDnxNamingRate(*resolution, unitsRate)
												: unitsRate;
					const auto oldName = Detail::reallyOldDnx(profile, {storedWidth.value_or(0), frameHeight}, namingRate, layout,
															  integer(unique(descriptor, {"OMFI:CDCI:ComponentWidth"})),
															  integer(unique(descriptor, {"OMFI:CDCI:HorizontalSubsampling"})),
															  integer(unique(descriptor, {"OMFI:CDCI:VerticalSubsampling"})));
					if (!oldName.isEmpty())
					{
						observe(file, MediaProperty::ReallyOldDnx, m_source, descriptor, *codecProperty, oldName, EvidenceBasis::Derived,
								!namingRate.sameRate(unitsRate)
									? QStringLiteral("Verified legacy OMF/MDB name-table clock spelling for resolution ID %1: recorded %2/%3, naming operating point %4/%5. Matching MDB/MXF identities or Avid's supplied OMF slate corroborate this exact profile/clock pair; raster, layout, depth and sampling must also match Avid's 2012 white paper pp. 9–10. Recorded Frame Rate and Duration are unchanged.")
										  .arg(*resolution)
										  .arg(unitsRate.numerator)
										  .arg(unitsRate.denominator)
										  .arg(namingRate.numerator)
										  .arg(namingRate.denominator)
									: QStringLiteral("Exact profile, stored raster, frame layout, depth, sampling and rational rate match Avid's 2012 white paper pp. 9–10."));
						codec += QStringLiteral(" [%1]").arg(oldName);
					}
					break;
				}
			}
			const auto *compression = uniqueRaw(descriptor, {"OMFI:DIDD:Compression"});
			if (legacyIdentity && resolution && compression && *resolution >= 0 && *resolution <= std::numeric_limits<quint32>::max())
			{
				// Exclude the old table's expressly unverified filename-only DV100 rows.
				if (*resolution != 2500 && *resolution != 2502 && *resolution != 2402)
				{
					const auto legacyName = OmfResolutions::name(quint32(*resolution), compression->encoding);
					if (!legacyName.isEmpty())
					{
						codec = legacyName;
						codecProperty = compression;
					}
				}
			}
			if (codec.isEmpty() && (cls == QLatin1String("PCMA") || cls == QLatin1String("WAVE")))
			{
				codec = QStringLiteral("PCM");
				codecProperty = classProperty;
			}
			if (named(codec, {"DV 25 411", "DV 25 420", "DV 50", "DV 25P 420"}) && storedWidth == 720 && layout)
			{
				const QString scan = *layout == 0 ? QStringLiteral("p") : (*layout == 1 || *layout == 3) ? QStringLiteral("i")
																										 : QString{};
				const QString standard = unitsRate.sameRate({25, 1}) && frameHeight == 576								   ? QStringLiteral("PAL")
										 : unitsRate.sameRate({30000, 1001}) && (frameHeight == 480 || frameHeight == 486) ? QStringLiteral("NTSC")
																														   : QString{};
				if (!scan.isEmpty() && !standard.isEmpty())
					codec += QStringLiteral(" %1(%2)").arg(scan, standard);
			}
			const auto saveCodec = [&]
			{
				if (label == QByteArray::fromHex("060e2b340401010d0401020203070100") ||
					label == QByteArray::fromHex("060e2b340401010d0401020203070200"))
				{
					codec = QStringLiteral("Avid DNxUncompressed");
					const auto descriptorValue = [&](MediaProperty field)
					{
						QString selected;
						for (const auto &value : file.evidence.observations(field))
							if (value.objectIdentity == QStringLiteral("object:%1").arg(descriptor.handle) && value.readState == PropertyReadState::Present)
							{
								if (!selected.isEmpty() && selected != value.value.toString())
									return QString{};
								selected = value.value.toString();
							}
						return selected;
					};
					const auto depth = descriptorValue(MediaProperty::BitDepth);
					const auto format = descriptorValue(MediaProperty::SampleFormat);
					if (!depth.isEmpty() && !format.isEmpty())
						codec += QStringLiteral(" — %1 %2").arg(depth, format);
				}
				if (!codec.isEmpty() && codecProperty)
					observe(file, MediaProperty::Codec, m_source, descriptor, *codecProperty, codec, EvidenceBasis::Derived,
							QStringLiteral("Established descriptor coding and applicable verified name table; original identifiers retained."));
			};

			for (const auto *depth : properties(descriptor, {"OMFI:CDCI:ComponentWidth", "OMFI:MDAU:BitsPerSample"}))
			{
				const auto value = integer(depth);
				if (!value)
					continue;
				observe(file, MediaProperty::ComponentDepth, m_source, descriptor, *depth, *value);
				qint64 bits = *value;
				QString representation;
				const bool dnx = label == QByteArray::fromHex("060e2b340401010d0401020203070100");
				const bool fixed = label == QByteArray::fromHex("060e2b340401010d0401020203070200");
				if (dnx && bits == 253)
				{
					bits = 16;
					representation = QStringLiteral("Half float");
				}
				else if (dnx && bits == 254)
				{
					bits = 32;
					representation = QStringLiteral("Float");
				}
				else if (fixed && (bits == 254 || bits == 10 || bits == 12))
				{
					representation = bits == 254 ? QStringLiteral("S2.14 fixed point") : bits == 10 ? QStringLiteral("10.6 fixed point")
																									: QStringLiteral("12.4 fixed point");
					bits = 16;
				}
				else if ((dnx && (bits == 8 || bits == 10 || bits == 12 || bits == 16)) ||
						 cls == QLatin1String("PCMA") || cls == QLatin1String("WAVE"))
					representation = QStringLiteral("Integer");
				if (bits > 0 && bits <= 64)
					observe(file, MediaProperty::BitDepth, m_source, descriptor, *depth, QStringLiteral("%1-bit").arg(bits), EvidenceBasis::Derived,
							QStringLiteral("Recorded component/sample precision; DNxUncompressed sentinels require the exact coding variant."));
				if (!representation.isEmpty())
					observe(file, MediaProperty::SampleFormat, m_source, descriptor, *depth, representation, EvidenceBasis::Derived);
			}

			if (audio)
			{
				saveCodec();
				return;
			}
			if (layout && *layout >= 0 && *layout <= 3 && storedWidth && *storedWidth > 0 && frameHeight > 0)
			{
				observe(file, MediaProperty::Resolution, m_source, descriptor, *storedWidthProperty,
						QStringLiteral("%1x%2").arg(*storedWidth).arg(frameHeight), EvidenceBasis::Derived,
						QStringLiteral("OMFI:DIDD:StoredWidth/StoredHeight, expressed as a frame raster using recorded FrameLayout. Separate fields and OMF1 mixed fields double the height; a single field does not. Display/sample dimensions and padding are unchanged."));
			}
			const auto *pixels = uniqueRaw(descriptor, {"OMFI:RGBA:PixelLayout"});
			const auto *depths = uniqueRaw(descriptor, {"OMFI:RGBA:PixelStructure"});
			if (cls == QLatin1String("RGBA") && pixels)
			{
				observe(file, MediaProperty::PixelLayout, m_source, descriptor, *pixels, pixels->encoding);
				const auto terminator = pixels->encoding.indexOf('\0');
				const auto count = terminator < 0 ? pixels->encoding.size() : terminator;
				bool known = count > 0 && count <= 8;
				QSet<char> seen;
				for (qsizetype index = 0; index < count; ++index)
				{
					const char code = pixels->encoding[index];
					if (!QByteArray("ABFGPR0").contains(code) || (code != '0' && seen.contains(code)))
						known = false;
					seen.insert(code);
				}
				if (known)
				{
					// These Avid database descriptors explicitly declare no
					// compression and one alpha component. Missing coding alone
					// cannot establish this descriptive codec name.
					const auto oneValue = [](const QByteArray &bytes, char expected)
					{
						return bytes == QByteArray(1, expected) ||
							   bytes == QByteArray(1, expected) + '\0';
					};
					if (codec.isEmpty() && properties(descriptor, {"OMFI:DIDD:EssenceCompression"}).isEmpty() &&
						compression && (compression->encoding == "NONE" || compression->encoding == QByteArray("NONE\0", 5)) &&
						oneValue(pixels->encoding, 'A') && depths && !depths->encoding.isEmpty() &&
						quint8(depths->encoding.front()) > 0 && quint8(depths->encoding.front()) <= 64 &&
						oneValue(depths->encoding, depths->encoding.front()))
					{
						codec = QStringLiteral("Uncompressed alpha");
						codecProperty = compression;
					}
					observe(file, MediaProperty::Alpha, m_source, descriptor, *pixels, seen.contains('A'), EvidenceBasis::Derived,
							QStringLiteral("Explicit complete RGBA component list; transparency polarity alone is not presence evidence."));
					if (depths && depths->encoding.size() >= count)
					{
						QSet<int> widths;
						for (qsizetype index = 0; index < count; ++index)
							if (pixels->encoding[index] != 'F' && pixels->encoding[index] != '0')
								widths.insert(quint8(depths->encoding[index]));
						if (widths.size() == 1 && *widths.cbegin() > 0 && *widths.cbegin() <= 64)
							observe(file, MediaProperty::BitDepth, m_source, descriptor, *depths,
									QStringLiteral("%1-bit").arg(*widths.cbegin()), EvidenceBasis::Derived,
									QStringLiteral("All described non-fill RGBA components have this recorded width; component arrays remain separate."));
					}
				}
			}
			saveCodec();
		}

		ProjectedFile nativeAudio(const ParsedSource &source)
		{
			ProjectedFile file;
			AvidObject owner;
			owner.snapshot = source.snapshot;
			QVector<Property> dataChunks;
			for (const auto &property : source.unownedProperties)
				if (property.locator.key == "data" && property.state == PropertyReadState::Present)
					dataChunks.append(&property);
			for (const auto &property : source.unownedProperties)
			{
				if (property.state != PropertyReadState::Present || !property.bytesRetained)
					continue;
				AudioFacts facts;
				if (property.locator.key == "fmt ")
				{
					facts = waveFormat(property.encoding);
					if (facts.uncompressed && facts.blockAlign && dataChunks.size() == 1 && dataChunks.first()->locator.ranges.size() == 1)
					{
						const auto bytes = dataChunks.first()->locator.ranges.first().length;
						if (bytes >= 0 && bytes % facts.blockAlign == 0)
							facts.frames = bytes / facts.blockAlign;
					}
				}
				else if (property.locator.key == "COMM")
				{
					const auto fields = property.decoded.toMap();
					facts = aiffCommon(property.encoding, fields.contains(QStringLiteral("compressionType")));
				}
				else
					continue;
				observe(file, MediaProperty::Kind, source, owner, property, 1, EvidenceBasis::Derived,
						QStringLiteral("A native WAVE/AIFF audio format header was recorded."));
				audioObservations(file, source, owner, property, facts, true);
			}
			return file;
		}
	}

	Projection projectOmf(const ParsedSource &source, const Cancellation &cancellation)
	{
		if (cancellation.cancelled())
			return {};
		const bool database = source.snapshot && source.snapshot->source == MetadataSource::Mdb;
		if (database || source.container == ParsedSource::Container::Omf || source.container == ParsedSource::Container::Bento)
			return ObjectProjection(source, cancellation).project(database);
		Projection result;
		if (source.container != ParsedSource::Container::Wave && source.container != ParsedSource::Container::Aiff)
			return result;
		auto file = nativeAudio(source);
		QVector<ProjectedFile> embedded;
		QSet<QString> identities;
		for (const auto &child : source.embeddedSources)
		{
			if (cancellation.cancelled())
				return result;
			auto projection = ObjectProjection(child, cancellation).project(false);
			result.diagnostics += projection.diagnostics;
			for (auto &candidate : projection.files)
			{
				for (const auto &value : candidate.evidence.observations(MediaProperty::FileMobId))
					if (value.eligible && value.readState == PropertyReadState::Present && !value.value.toString().isEmpty())
						identities.insert(value.value.toString());
				embedded.append(std::move(candidate));
			}
		}
		if (identities.size() == 1)
		{
			file.fileMobId = *identities.cbegin();
			for (const auto &candidate : embedded)
			{
				appendEvidence(file.evidence, candidate.evidence);
				file.objects += candidate.objects;
				for (const auto &master : candidate.masterMobIds)
					if (!file.masterMobIds.contains(master))
						file.masterMobIds.append(master);
			}
			file.masterMobIds.sort();
		}
		else if (identities.size() > 1)
		{
			result.diagnostics.append(QStringLiteral("Embedded OMF graphs disagree about the physical audio file's identity; native audio facts remain available."));
			for (const auto &candidate : embedded)
				retainAmbiguousCandidate(file, candidate);
		}
		result.files.append(std::move(file));
		return result;
	}
}
