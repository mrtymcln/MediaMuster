// Interprets the MDB reader's object graph without using the OMF media path.
// MDB uses OMFI property names, but its database entries are independent file
// candidates: a media file's own contents list does not select their ownership.
// These local helpers are deliberately separate so OMF support can be removed
// without changing database interpretation. Recorded references remain intact.

#include "projection.h"
#include "compressionnames_p.h"
#include "picturegeometry_p.h"
#include "avidprecompute.h"
#include "mediametadata.h"

#include <QHash>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace MediaEngine
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
			// Inspect every matching property in place, keeping duplicate conflicts
			// and the chosen property's provenance without a temporary pointer list.
			for (const auto &property : object.properties)
			{
				if (!named(property.locator.name, names))
					continue;
				if (property.state != PropertyReadState::Present || !property.decoded.isValid())
					return nullptr;
				if (result && result->decoded != property.decoded)
					return nullptr;
				result = &property;
			}
			return result;
		}

		Property uniqueRaw(const AvidObject &object, std::initializer_list<const char *> names)
		{
			Property result = nullptr;
			for (const auto &property : object.properties)
			{
				if (!named(property.locator.name, names))
					continue;
				if (property.state != PropertyReadState::Present || !property.bytesRetained)
					return nullptr;
				if (result && result->encoding != property.encoding)
					return nullptr;
				result = &property;
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

		bool mediaDataClass(const QString &value)
		{
			return named(value, {"MDAT", "IDAT", "JPEG", "TIFF", "WAVE", "AIFC"});
		}

		bool descriptorOrComponentClass(const QString &value)
		{
			return (mediaClass(value) && value != QLatin1String("WAVE")) ||
				   named(value, {"HEAD", "CLSD", "TRAK", "ATTB", "ATTR", "MDES", "MDFL", "MDFM", "MDTP",
								 "DIDD", "CPNT", "SCLP", "TCCP", "SEQU", "FILL", "TRKG", "TRAN", "SLCT",
								 "SPED", "MASK", "REPT", "PVOL", "PDWN", "CTRL", "ECCP", "FXCP", "NEST"});
		}

		bool completeObject(const ParsedSource &source, const AvidObject &object)
		{
			// A complete Bento read enumerates the owner's TOC entries. An unnamed
			// entry may conceal a checked property behind an unresolved dictionary
			// definition, so it cannot establish that property's absence.
			return source.outcome == ParsedSource::Outcome::Complete && !object.properties.isEmpty() &&
				   std::all_of(object.properties.cbegin(), object.properties.cend(), [&](const auto &property)
							   { return property.bento && !property.locator.name.isEmpty() &&
										property.locator.objectNumber == object.handle; });
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
			// and duration. See the 7 October DNx clock evidence.
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
			file.evidence.registerSource(source.snapshot, QStringLiteral("object:%1").arg(object.handle));
			if (std::none_of(file.objects.cbegin(), file.objects.cend(), [&](const auto &existing)
							 { return existing.source == source.snapshot && existing.handle == object.handle; }))
				file.objects.append({source.snapshot, object.handle});
		}

		class MdbProjection
		{
		public:
			MdbProjection(const ParsedSource &source, const Cancellation &cancellation)
				: m_source(source), m_cancellation(cancellation)
			{
				for (const auto &object : source.objects)
				{
					if (cancellation.cancelled())
						return;
					m_objects.insert(object.handle, &object);
				}
				for (const auto &edge : source.relationships)
				{
					if (cancellation.cancelled())
						return;
					m_edges[edge.origin].append(&edge);
				}
				establishContents();
				for (const auto handle : m_activeMobs)
				{
					const auto *object = m_objects.value(handle);
					QSet<QString> claims;
					for (const auto *property : properties(*object, {"OMFI:MOBJ:MobID"}))
						if (const auto id = identity(property); !id.isEmpty())
							claims.insert(id);
					if (claims.size() > 1 &&
						!properties(*object, {"OMFI:MOBJ:PhysicalMedia", "OMFI:SMOB:MediaDescription"}).isEmpty())
						m_conflictingMobIds += claims;
				}
				for (const auto handle : m_activeMobs)
					if (!m_disputedMobs.contains(handle))
					{
						const auto id = identity(unique(*m_objects.value(handle), {"OMFI:MOBJ:MobID"}));
						if (!id.isEmpty() && !m_conflictingMobIds.contains(id))
							m_mobs[id].append(handle);
					}
			}

			Projection project()
			{
				Projection result;
				result.diagnostics = m_contentsDiagnostics;
				if (!m_contentsComplete)
					return result;
				QHash<ObjectHandle, QVector<qsizetype>> fileSubjects;
				QHash<qsizetype, QVariantList> clipDurations;
				for (const auto &object : m_source.objects)
				{
					if (m_cancellation.cancelled())
						return result;
					if (!m_activeMobs.contains(object.handle))
						continue;
					QSet<QString> recordedIds;
					for (const auto *property : properties(object, {"OMFI:MOBJ:MobID"}))
						if (const auto id = identity(property); !id.isEmpty())
							recordedIds.insert(id);
					const bool disputedIdentity = std::any_of(recordedIds.cbegin(), recordedIds.cend(),
															  [&](const auto &id)
															  { return m_conflictingMobIds.contains(id); });
					if (disputedIdentity &&
						!properties(object, {"OMFI:MOBJ:PhysicalMedia", "OMFI:SMOB:MediaDescription"}).isEmpty())
					{
						ProjectedFile carrier;
						link(carrier, m_source, object);
						for (const auto *property : properties(object, {"OMFI:MOBJ:MobID"}))
							if (const auto id = identity(property); !id.isEmpty())
								observe(carrier, MediaProperty::FileMobId, m_source, object, *property, id, EvidenceBasis::Derived,
										QStringLiteral("This active descriptor-owning mob's identity claims overlap a conflicting active owner; technical and editorial ownership is not established."));
						result.files.append(std::move(carrier));
						result.diagnostics.append(QStringLiteral("An active OMF mob has conflicting MobID claims; identity evidence is retained without technical ownership."));
						continue;
					}
					if (m_disputedMobs.contains(object.handle))
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
						technical(file, *descriptor, editRate(object));
						timecode(file, object);
						attributes(file, object);
						const auto sourceHandles = sourceMobs(object);
						for (const auto referenced : sourceHandles)
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
					if (!m_activeMobs.contains(master.handle) || m_disputedMobs.contains(master.handle) || !isMaster(master))
						continue;
					const auto *masterProperty = unique(master, {"OMFI:MOBJ:MobID"});
					const auto masterId = identity(masterProperty);
					if (masterId.isEmpty())
						continue;
					ProjectedFile masterFacts;
					masterFacts.masterMobIds.append(masterId);
					link(masterFacts, m_source, master);
					recordPropertyCoverage(masterFacts, MediaProperty::ClipName, m_source, master,
										   {"OMFI:CPNT:Name", "OMFI:MOBJ:Name"}, completeObject(m_source, master));
					recordPropertyCoverage(masterFacts, MediaProperty::Type, m_source, master,
										   {"OMFI:MOBJ:UsageCode"}, false);
					recordPropertyCoverage(masterFacts, MediaProperty::PrecomputeCategory, m_source, master,
										   {"OMFI:MOBJ:UsageCode", "OMFI:CPNT:Attributes", "OMFI:TRKG:Tracks"}, false);
					observe(masterFacts, MediaProperty::MasterMobId, m_source, master, *masterProperty, masterId,
							EvidenceBasis::Derived, QStringLiteral("Canonical identity of this recorded master object."));
					for (const auto *name : properties(master, {"OMFI:CPNT:Name", "OMFI:MOBJ:Name"}))
						text(masterFacts, MediaProperty::ClipName, master, *name);
					attributes(masterFacts, master);
					classification(masterFacts, master);
					const auto sourceHandles = sourceMobs(master);
					for (const auto handle : sourceHandles)
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
				result.diagnostics += m_referenceDiagnostics;
				return result;
			}

		private:
			struct ContentsList
			{
				bool present = false;
				bool complete = false;
				QSet<ObjectHandle> objects;
				QVector<const Relationship *> entries;
			};

			ContentsList contentsList(const char *name, bool index = false) const
			{
				ContentsList result;
				const auto *head = m_objects.value(1);
				if (!head || !m_source.omfRevision)
					return result;
				const auto values = properties(*head, {name});
				result.present = !values.isEmpty();
				if (values.size() != 1)
					return result;
				const auto *value = values.first();
				const qsizetype width = *m_source.omfRevision == OmfRevision::V1 ? 8 : 4;
				const qsizetype stride = width + (index ? 12 : 0);
				if (value->state != PropertyReadState::Present || !value->bytesRetained || !value->bento ||
					value->bento->typeName != QLatin1String(index ? "omfi:MobIndex" : "omfi:ObjRefArray") ||
					value->encoding.size() < 2 || (value->encoding.size() - 2) % stride != 0)
					return result;
				// The toolkit's array length is determined by byte extent. This also
				// accommodates its 0xffff count marker without dropping any slots.
				const qsizetype count = (value->encoding.size() - 2) / stride;
				for (const auto *edge : m_edges.value(1))
					if (edge->locator.name == QLatin1String(name))
					{
						result.entries.append(edge);
						if (!edge->target || !m_objects.contains(edge->target))
							return result;
						result.objects.insert(edge->target);
					}
				result.complete = result.entries.size() == count;
				return result;
			}

			void qualifyLegacyIndex(const char *name, const QSet<ObjectHandle> &expected, bool media = false)
			{
				const auto list = contentsList(name, true);
				if (!list.present)
					return; // OMF1's typed indexes are optional; ObjectSpine establishes membership.
				QSet<ObjectHandle> disputed;
				bool incomparableWidth = false;
				if (!list.complete)
					disputed = expected;
				else
				{
					disputed = expected - list.objects;
					for (const auto *edge : list.entries)
					{
						const auto *owner = m_objects.value(edge->target);
						const auto uid = edge->recordedReference.toMap().value(QStringLiteral("mobId")).toByteArray();
						const auto *head = m_objects.value(1);
						const auto *index = uniqueRaw(*head, {name});
						const auto recordedId = uid.size() == 12 && index && index->bento && index->bento->metadataBigEndian
													? canonicalDatabaseId(uid, *index->bento->metadataBigEndian)
													: QString{};
						const auto ownerIds = media ? mediaIdentities(*owner) : properties(*owner, {"OMFI:MOBJ:MobID"});
						QSet<QString> ownerClaims;
						bool unreadableClaim = ownerIds.isEmpty();
						for (const auto *property : ownerIds)
						{
							const auto claim = identity(property);
							if (claim.isEmpty())
								unreadableClaim = true;
							else
								ownerClaims.insert(claim);
						}
						const auto ownerId = !unreadableClaim && ownerClaims.size() == 1 ? *ownerClaims.cbegin() : QString{};
						if (!expected.contains(edge->target) || recordedId.isEmpty() || ownerId.isEmpty())
							disputed.insert(edge->target);
						else if (ownerIds.first()->encoding.size() == 12 && recordedId != ownerId)
							disputed.insert(edge->target);
						else if (ownerIds.first()->encoding.size() == 32 && recordedId != ownerId)
							incomparableWidth = true;
					}
				}
				if (incomparableWidth)
					m_contentsDiagnostics.append(QStringLiteral("%1 contains 12-byte index UIDs referencing 32-byte Avid MobIDs. Their identity comparison is not established; recorded object references establish membership and both encodings are retained.").arg(QLatin1String(name)));
				if (!list.complete || !disputed.isEmpty())
				{
					m_contentsDiagnostics.append(QStringLiteral("%1 is unreadable or disagrees with ObjectSpine membership/recorded MobID; affected identity associations are not eligible. Raw properties and references remain available.").arg(QLatin1String(name)));
					if (!media)
						m_disputedMobs += disputed;
				}
			}

			void establishContents()
			{
				const auto fail = [&](const char *name)
				{
					m_contentsDiagnostics.append(QStringLiteral("%1 required contents list is absent, unreadable or has unresolved/unsupported ownership; owned OMF metadata is not eligible. Raw properties and references remain available.").arg(QLatin1String(name)));
				};
				if (!m_source.omfRevision)
				{
					fail("OMF HEAD");
					return;
				}
				if (*m_source.omfRevision == OmfRevision::V1)
				{
					const auto spine = contentsList("OMFI:ObjectSpine");
					if (!spine.complete)
					{
						fail("OMFI:ObjectSpine");
						return;
					}
					QSet<ObjectHandle> sources, compositions;
					for (const auto handle : spine.objects)
					{
						const auto *object = m_objects.value(handle);
						const auto cls = objectClass(*object);
						if (cls == QLatin1String("MOBJ"))
						{
							m_activeMobs.insert(handle);
							if (properties(*object, {"OMFI:MOBJ:PhysicalMedia"}).isEmpty())
								compositions.insert(handle);
							else
								sources.insert(handle);
						}
						else if (cls.isEmpty() || descriptorOrComponentClass(cls) || mobClass(cls))
						{
							fail("OMFI:ObjectSpine");
							return;
						}
						else
						{
							m_activeMedia.insert(handle);
							if (!mediaDataClass(cls))
							{
								m_contentsDiagnostics.append(QStringLiteral("ObjectSpine media-data class %1 has no established identity interpretation; physical ownership remains unknown and its raw context is retained.").arg(cls));
							}
						}
					}
					qualifyLegacyIndex("OMFI:SourceMobs", sources);
					qualifyLegacyIndex("OMFI:CompositionMobs", compositions);
					qualifyLegacyIndex("OMFI:MediaData", m_activeMedia, true);
				}
				else
				{
					const auto mobs = contentsList("OMFI:HEAD:Mobs"), media = contentsList("OMFI:HEAD:MediaData");
					if (!mobs.complete || !media.complete)
					{
						if (!mobs.complete)
							fail("OMFI:HEAD:Mobs");
						if (!media.complete)
							fail("OMFI:HEAD:MediaData");
						return;
					}
					for (const auto handle : mobs.objects)
						if (!mobClass(objectClass(*m_objects.value(handle))))
						{
							fail("OMFI:HEAD:Mobs");
							return;
						}
					for (const auto handle : media.objects)
						if (!mediaDataClass(objectClass(*m_objects.value(handle))))
						{
							fail("OMFI:HEAD:MediaData");
							return;
						}
					m_activeMobs = mobs.objects;
					m_activeMedia = media.objects;
				}
				m_contentsComplete = true;
			}

			QVector<Property> mediaIdentities(const AvidObject &object) const
			{
				if (!m_activeMedia.contains(object.handle))
					return {};
				const auto cls = objectClass(object);
				if (*m_source.omfRevision == OmfRevision::V2)
					return mediaDataClass(cls) ? properties(object, {"OMFI:MDAT:MobID"}) : QVector<Property>{};
				if (cls == QLatin1String("WAVE"))
					return properties(object, {"OMFI:WAVE:MobID"});
				if (cls == QLatin1String("AIFC"))
					return properties(object, {"OMFI:AIFC:MobID"});
				if (cls == QLatin1String("TIFF"))
					return properties(object, {"OMFI:TIFF:MobID"});
				if (named(cls, {"MDAT", "IDAT", "JPEG"}))
					return properties(object, {"OMFI:MDAT:MobID"});
				return {};
			}

			QVector<const AvidObject *> targets(ObjectHandle handle, std::initializer_list<const char *> names) const
			{
				QVector<const AvidObject *> result;
				const auto *owner = m_objects.value(handle);
				if (!owner)
					return result;
				for (const auto *name : names)
					if (!properties(*owner, {name}).isEmpty() &&
						!completeReferences(*owner, name, named(QLatin1String(name), {"OMFI:CPNT:Attributes", "OMFI:MOBJ:UserAttributes"})))
					{
						incompleteReference(*owner, name);
						return result;
					}
				for (const auto *edge : m_edges.value(handle))
					if (edge->target && named(edge->locator.name, names))
						if (const auto *object = m_objects.value(edge->target))
							result.append(object);
				return result;
			}

			void incompleteReference(const AvidObject &owner, const char *name) const
			{
				const auto explanation = QStringLiteral("%1 on object %2 is not a complete resolved reference list; dependent metadata remains unknown and recorded relationships are retained.")
											 .arg(QLatin1String(name))
											 .arg(owner.handle);
				if (!m_referenceDiagnostics.contains(explanation))
					m_referenceDiagnostics.append(explanation);
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
				if (values.isEmpty() || !m_source.omfRevision)
					return false;
				const qsizetype width = *m_source.omfRevision == OmfRevision::V1 ? 8 : 4;
				quint64 expected = 0, observed = 0;
				for (const auto *value : values)
				{
					if (value->state != PropertyReadState::Present || !value->bytesRetained || !value->bento ||
						(value->bento->typeName != QLatin1String("omfi:ObjRef") && value->bento->typeName != QLatin1String("omfi:ObjRefArray")))
						return false;
					if (value->bento->typeName == QLatin1String("omfi:ObjRef"))
					{
						if (value->encoding.size() != width)
							return false;
						++expected;
					}
					else
					{
						if (value->encoding.size() < 2 || (value->encoding.size() - 2) % width != 0)
							return false;
						expected += (value->encoding.size() - 2) / width;
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

			struct ComponentTraversal
			{
				QVector<const AvidObject *> objects;
				bool complete = false;
			};

			ComponentTraversal collectComponents(const AvidObject &owner) const
			{
				QVector<ObjectHandle> pending{owner.handle};
				QVector<const AvidObject *> result;
				QSet<ObjectHandle> visited;
				bool complete = true;
				while (!pending.isEmpty())
				{
					if (m_cancellation.cancelled())
						return {std::move(result), false};
					const auto handle = pending.takeLast();
					if (visited.contains(handle))
						continue;
					visited.insert(handle);
					const auto *object = m_objects.value(handle);
					if (!object)
					{
						complete = false;
						continue;
					}
					for (const auto *name : {"OMFI:TRKG:Tracks", "OMFI:TRAK:TrackComponent", "OMFI:SEQU:Sequence",
											 "OMFI:MOBJ:Slots", "OMFI:MSLT:Segment", "OMFI:SEQU:Components", "OMFI:NEST:Slots",
											 "OMFI:SLCT:Selected", "OMFI:SLCT:Alternates", "OMFI:MGRP:Choices", "OMFI:ERAT:InputSegment"})
						if (!properties(*object, {name}).isEmpty() && !completeReferences(*object, name))
						{
							incompleteReference(*object, name);
							return {}; // A resolved branch cannot hide a missing declared component.
						}
					result.append(object);
					for (const auto *target : targets(handle, {"OMFI:TRKG:Tracks", "OMFI:TRAK:TrackComponent", "OMFI:SEQU:Sequence",
															   "OMFI:MOBJ:Slots", "OMFI:MSLT:Segment", "OMFI:SEQU:Components", "OMFI:NEST:Slots",
															   "OMFI:SLCT:Selected", "OMFI:SLCT:Alternates", "OMFI:MGRP:Choices", "OMFI:ERAT:InputSegment"}))
						pending.append(target->handle);
				}
				return {std::move(result), complete};
			}

			QVector<const AvidObject *> components(const AvidObject &owner) const
			{
				return collectComponents(owner).objects;
			}

			QVector<ObjectHandle> sourceMobs(const AvidObject &mob) const
			{
				if (m_cancellation.cancelled())
					return {};
				if (const auto cached = m_sourceMobsCache.constFind(mob.handle); cached != m_sourceMobsCache.cend())
					return cached.value();

				QVector<ObjectHandle> result;
				const auto traversal = collectComponents(mob);
				for (const auto *object : traversal.objects)
					if (objectClass(*object) == QLatin1String("SCLP"))
					{
						if (properties(*object, {"OMFI:SCLP:SourceID"}).isEmpty())
							continue; // Original-source endpoints need not name an onward mob.
						const auto *sourceId = unique(*object, {"OMFI:SCLP:SourceID"});
						const auto id = identity(sourceId);
						if (id.isEmpty())
						{
							if (sourceId && sourceId->bento && sourceId->bento->typeName == QLatin1String("omfi:UID") &&
								(sourceId->encoding.size() == 12 || sourceId->encoding.size() == 32) &&
								std::all_of(sourceId->encoding.cbegin(), sourceId->encoding.cend(), [](char byte)
											{ return byte == 0; }))
								continue; // Explicit zero UID is the original-source sentinel.
							const auto explanation = QStringLiteral("OMFI:SCLP:SourceID on object %1 has no unique interpreted identity; dependent mob associations remain unknown and original properties are retained.").arg(object->handle);
							if (!m_referenceDiagnostics.contains(explanation))
								m_referenceDiagnostics.append(explanation);
							return {};
						}
						for (const auto target : m_mobs.value(id))
							if (target != mob.handle && !result.contains(target))
								result.append(target);
					}
				// Reuse only finished lookups within this source's projection. A valid
				// empty answer is cacheable; unresolved or cancelled work is not.
				if (traversal.complete && !m_cancellation.cancelled())
					m_sourceMobsCache.insert(mob.handle, result);
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
				const auto value = interpreted.decoded.metaType().id() == QMetaType::QString
									   ? interpreted.decoded
									   : QVariant{};
				observe(file, field, m_source, object, interpreted, value);
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
						if (!completeReferences(*entry, "OMFI:ATTB:ObjAttribute"))
						{
							incompleteReference(*entry, "OMFI:ATTB:ObjAttribute");
							continue;
						}
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
				recordPropertyCoverage(file, MediaProperty::OriginalBin, m_source, object,
									   {"OMFI:MCBR:MC:binNameUTF8", "OMFI:MCBR:MC:binName"}, completeObject(m_source, object));
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
				recordPropertyCoverage(file, MediaProperty::SourceFilename, m_source, object,
									   {"OMFI:ATTB:StringAttribute", "OMFI:FL:POSIXPathName", "OMFI:FL:PathNameUTF8",
										"OMFI:FL:PathName", "OMFI:UNXL:PathName", "OMFI:WINL:PathName", "OMFI:MACL:PathName"},
									   false);
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

			void technical(ProjectedFile &file, const AvidObject &descriptor, MediaRate editRate);

			const ParsedSource &m_source;
			const Cancellation &m_cancellation;
			QHash<ObjectHandle, const AvidObject *> m_objects;
			QHash<ObjectHandle, QVector<const Relationship *>> m_edges;
			QHash<QString, QVector<ObjectHandle>> m_mobs;
			mutable QHash<ObjectHandle, QVector<ObjectHandle>> m_sourceMobsCache;
			QSet<ObjectHandle> m_activeMobs;
			QSet<ObjectHandle> m_activeMedia;
			QSet<ObjectHandle> m_disputedMobs;
			QSet<QString> m_conflictingMobIds;
			QStringList m_contentsDiagnostics;
			mutable QStringList m_referenceDiagnostics;
			bool m_contentsComplete = false;
		};

		struct AudioFacts
		{
			int channels = 0;
			int bits = 0;
			MediaRate sampleRate;
			QString codec;
			QString representation;
			Property channelsProperty = nullptr;
			Property bitsProperty = nullptr;
			Property rateProperty = nullptr;
			Property codecProperty = nullptr;
		};

		Property audioField(const AvidObject &owner, const RawProperty &header, const char *name)
		{
			const QString fieldName = header.locator.name + QLatin1Char('.') + QLatin1String(name);
			for (const auto &field : owner.properties)
			{
				if (field.locator.name != fieldName || field.bento != header.bento || field.locator.ranges.isEmpty())
					continue;
				const bool contained = std::all_of(field.locator.ranges.cbegin(), field.locator.ranges.cend(), [&](const auto &range)
												   { return std::any_of(header.locator.ranges.cbegin(), header.locator.ranges.cend(), [&](const auto &parent)
																		{ return range.offset >= parent.offset && range.offset - parent.offset <= parent.length &&
																				 range.length <= parent.length - (range.offset - parent.offset); }); });
				if (contained)
					return &field;
			}
			return nullptr;
		}

		AudioFacts waveFormat(const AvidObject &owner, const RawProperty &header)
		{
			AudioFacts result;
			const auto fields = header.decoded.toMap();
			if (!fields.contains(QStringLiteral("wFormatTag")))
				return result;
			quint16 format = quint16(fields.value(QStringLiteral("wFormatTag")).toUInt());
			result.channelsProperty = audioField(owner, header, "nChannels");
			result.rateProperty = audioField(owner, header, "nSamplesPerSec");
			result.codecProperty = audioField(owner, header, "wFormatTag");
			result.bitsProperty = audioField(owner, header, "wBitsPerSample");
			result.channels = fields.value(QStringLiteral("nChannels")).toInt();
			const quint32 sampleRate = fields.value(QStringLiteral("nSamplesPerSec")).toUInt();
			if (sampleRate <= quint32(std::numeric_limits<qint32>::max()))
				result.sampleRate = {qint32(sampleRate), 1};
			if (format == 0xfffe)
			{
				result.codecProperty = audioField(owner, header, "SubFormat");
				result.bitsProperty = audioField(owner, header, "wValidBitsPerSample");
				const auto guid = result.codecProperty && result.codecProperty->state == PropertyReadState::Present
									  ? result.codecProperty->encoding
									  : QByteArray{};
				if (guid == QByteArray::fromHex("0100000000001000800000aa00389b71"))
					format = 1;
				else if (guid == QByteArray::fromHex("0300000000001000800000aa00389b71"))
					format = 3;
				else
				{
					// Storage width remains recorded, but cannot stand in for
					// valid precision when this extension is not established.
					if (result.codecProperty && result.codecProperty->state == PropertyReadState::Unreadable)
						result.bitsProperty = result.codecProperty;
					return result;
				}
			}
			if (result.bitsProperty && result.bitsProperty->state == PropertyReadState::Present)
				result.bits = result.bitsProperty->decoded.toInt();
			if (format == 1)
			{
				result.codec = QStringLiteral("PCM");
				result.representation = QStringLiteral("Integer");
			}
			else if (format == 3)
			{
				result.codec = QStringLiteral("IEEE float");
				result.representation = QStringLiteral("Float");
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

		AudioFacts aiffCommon(const AvidObject &owner, const RawProperty &header)
		{
			AudioFacts result;
			const auto fields = header.decoded.toMap();
			if (!fields.contains(QStringLiteral("sampleSize")))
				return result;
			result.channelsProperty = audioField(owner, header, "numChannels");
			result.bitsProperty = audioField(owner, header, "sampleSize");
			result.rateProperty = audioField(owner, header, "sampleRate");
			result.codecProperty = audioField(owner, header, "compressionType");
			result.channels = fields.value(QStringLiteral("numChannels")).toInt();
			result.bits = fields.value(QStringLiteral("sampleSize")).toInt();
			if (result.rateProperty)
				result.sampleRate = extendedRate(result.rateProperty->encoding);
			const auto codec = result.codecProperty && result.codecProperty->state == PropertyReadState::Present
								   ? result.codecProperty->encoding
								   : QByteArray{};
			if (!result.codecProperty || codec == "NONE" || codec == "twos" || codec == "sowt" || codec == "in24" || codec == "in32")
			{
				result.codec = QStringLiteral("PCM");
				result.representation = QStringLiteral("Integer");
			}
			else if (codec == "fl32" || codec == "FL32" || codec == "fl64" || codec == "FL64")
			{
				result.codec = QStringLiteral("IEEE float");
				result.representation = QStringLiteral("Float");
			}
			return result;
		}

		void audioObservations(ProjectedFile &file, const ParsedSource &source, const AvidObject &object,
							   const RawProperty &property, const AudioFacts &audio)
		{
			const auto add = [&](MediaProperty field, Property input, const QVariant &value)
			{
				if (input && (value.isValid() || input->state == PropertyReadState::Unreadable))
					observe(file, field, source, object, *input, value, EvidenceBasis::Derived,
							input->state == PropertyReadState::Unreadable ? input->interpretation
																		  : QStringLiteral("Decoded recorded audio field %1; original header and field bytes retained. %2")
																				.arg(input->locator.name, input->interpretation));
			};
			add(MediaProperty::Channels, audio.channelsProperty, audio.channels > 0 ? QVariant(audio.channels) : QVariant{});
			add(MediaProperty::BitDepth, audio.bitsProperty, audio.bits > 0 && audio.bits <= 64 ? QVariant(QStringLiteral("%1-bit").arg(audio.bits)) : QVariant{});
			add(MediaProperty::SampleRate, audio.rateProperty, audio.sampleRate.valid() ? QVariant(rateValue(audio.sampleRate)) : QVariant{});
			if (audio.codecProperty || !audio.codec.isEmpty())
			{
				add(MediaProperty::Compression, audio.codecProperty ? audio.codecProperty : &property, audio.codec.isEmpty() ? QVariant{} : QVariant(audio.codec));
				add(MediaProperty::SampleFormat, audio.codecProperty ? audio.codecProperty : &property, audio.representation.isEmpty() ? QVariant{} : QVariant(audio.representation));
			}
		}

		void MdbProjection::technical(ProjectedFile &file, const AvidObject &descriptor, MediaRate editRate)
		{
			const auto cls = objectClass(descriptor);
			const bool audio = audioClass(cls);
			const bool complete = completeObject(m_source, descriptor);
			recordPropertyCoverage(file, MediaProperty::Kind, m_source, descriptor,
								   {"OMFI:ObjID", "OMFI:OOBJ:ObjClass"}, false);
			recordPropertyCoverage(file, audio ? MediaProperty::SampleRate : MediaProperty::FrameRate, m_source, descriptor,
								   {"OMFI:MDFL:SampleRate", "OMFI:WAVD:Summary", "OMFI:AIFD:Summary"}, complete);
			recordPropertyCoverage(file, MediaProperty::FileDuration, m_source, descriptor,
								   {"OMFI:MDFL:Length", "OMFI:MDFL:SampleRate", "OMFI:WAVD:Summary", "OMFI:AIFD:Summary"}, false);
			recordPropertyCoverage(file, MediaProperty::Compression, m_source, descriptor,
								   {"OMFI:DIDD:EssenceCompression", "OMFI:DIDD:DIDResolutionID", "OMFI:DIDD:Compression",
									"OMFI:WAVD:Summary", "OMFI:AIFD:Summary", "OMFI:ObjID", "OMFI:OOBJ:ObjClass"},
								   false);
			recordPropertyCoverage(file, MediaProperty::CompressionLabel, m_source, descriptor,
								   {"OMFI:DIDD:EssenceCompression", "OMFI:DIDD:DIDResolutionID"}, complete);
			recordPropertyCoverage(file, MediaProperty::ComponentDepth, m_source, descriptor,
								   {"OMFI:CDCI:ComponentWidth", "OMFI:MDAU:BitsPerSample"}, complete);
			for (const auto field : {MediaProperty::BitDepth, MediaProperty::SampleFormat})
				recordPropertyCoverage(file, field, m_source, descriptor,
									   {"OMFI:CDCI:ComponentWidth", "OMFI:MDAU:BitsPerSample", "OMFI:RGBA:PixelStructure",
										"OMFI:WAVD:Summary", "OMFI:AIFD:Summary", "OMFI:DIDD:EssenceCompression"},
									   false);
			if (audio)
				recordPropertyCoverage(file, MediaProperty::Channels, m_source, descriptor,
									   {"OMFI:MDAU:NumChannels", "OMFI:WAVD:Summary", "OMFI:AIFD:Summary"}, complete);
			else
			{
				recordPropertyCoverage(file, MediaProperty::PixelLayout, m_source, descriptor,
									   {"OMFI:RGBA:PixelLayout"}, complete);
				recordPropertyCoverage(file, MediaProperty::Alpha, m_source, descriptor,
									   {"OMFI:RGBA:PixelLayout", "OMFI:RGBA:PixelStructure"}, false);
				recordPropertyCoverage(file, MediaProperty::Resolution, m_source, descriptor,
									   {"OMFI:DIDD:StoredWidth", "OMFI:DIDD:StoredHeight", "OMFI:DIDD:FrameLayout",
										"OMFI:DIDD:SampledWidth", "OMFI:DIDD:SampledHeight", "OMFI:DIDD:SampledXOffset", "OMFI:DIDD:SampledYOffset",
										"OMFI:DIDD:DisplayWidth", "OMFI:DIDD:DisplayHeight", "OMFI:DIDD:DisplayXOffset", "OMFI:DIDD:DisplayYOffset"},
									   false);
				for (const auto field : {MediaProperty::NewDnx, MediaProperty::OldDnx, MediaProperty::ReallyOldDnx})
					recordPropertyCoverage(file, field, m_source, descriptor,
										   {"OMFI:DIDD:EssenceCompression", "OMFI:DIDD:DIDResolutionID"}, false);
			}
			const auto *classProperty = unique(descriptor, {"OMFI:ObjID", "OMFI:OOBJ:ObjClass"});
			if (!classProperty)
				return;
			observe(file, MediaProperty::Kind, m_source, descriptor, *classProperty, audio ? 1 : 0,
					EvidenceBasis::Derived, QStringLiteral("Established media descriptor class."));
			const auto *sampleRate = unique(descriptor, {"OMFI:MDFL:SampleRate"});
			MediaRate unitsRate = rate(sampleRate);
			if (unitsRate.valid())
				observe(file, audio ? MediaProperty::SampleRate : MediaProperty::FrameRate, m_source, descriptor, *sampleRate, rateValue(unitsRate));
			if (audio)
			{
				MediaRate summaryRate;
				bool conflictingRates = false;
				for (const auto &header : descriptor.properties)
				{
					const bool waveSummary = (cls == QLatin1String("WAVD") || cls == QLatin1String("WAVE")) &&
											 header.locator.name == QLatin1String("OMFI:WAVD:Summary.fmt ");
					const bool aiffSummary = cls == QLatin1String("AIFD") && header.locator.name == QLatin1String("OMFI:AIFD:Summary.COMM");
					if (!waveSummary && !aiffSummary)
						continue;
					const auto facts = waveSummary ? waveFormat(descriptor, header) : aiffCommon(descriptor, header);
					// Compression is the actual encoding; WAVE/AIFF is retained as the
					// descriptor/container fact, not substituted for compression.
					audioObservations(file, m_source, descriptor, header, facts);
					if (facts.sampleRate.valid())
					{
						if (summaryRate.valid() && (summaryRate.numerator != facts.sampleRate.numerator || summaryRate.denominator != facts.sampleRate.denominator))
							conflictingRates = true;
						else
							summaryRate = facts.sampleRate;
					}
				}
				if (!unitsRate.valid() && !conflictingRates)
					unitsRate = summaryRate;
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
			const bool codingAbsent = properties(descriptor, {"OMFI:DIDD:EssenceCompression"}).isEmpty();
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
			if (label.isEmpty() && codingAbsent && resolution)
			{
				label = knownDnxLabel(*resolution);
				codecProperty = resolutionId;
			}
			if (!label.isEmpty())
				observe(file, MediaProperty::CompressionLabel, m_source, descriptor, *codecProperty, label, EvidenceBasis::Derived,
						coding ? QStringLiteral("Recorded structured AUID converted to its UL octet order.") : QStringLiteral("Verified Avid DNx resolution-ID mapping; not a recorded compression UL."));

			Detail::CompressionFacts facts;
			facts.codingLabel = label;
			facts.descriptorClass = cls;
			facts.codingAbsent = codingAbsent && complete;
			facts.geometry = {storedWidth.value_or(0), frameHeight};
			const auto namingRate = resolution && label == knownDnxLabel(*resolution)
										? legacyDnxNamingRate(*resolution, unitsRate)
										: unitsRate;
			facts.rate = namingRate;
			facts.layout = layout;
			facts.depth = integer(unique(descriptor, {"OMFI:CDCI:ComponentWidth"}));
			facts.horizontal = integer(unique(descriptor, {"OMFI:CDCI:HorizontalSubsampling"}));
			facts.vertical = integer(unique(descriptor, {"OMFI:CDCI:VerticalSubsampling"}));
			const auto *compression = uniqueRaw(descriptor, {"OMFI:DIDD:Compression"});
			// Only typed descriptor identifiers enter the legacy name table. An
			// unknown or conflicting coding property is not evidence of absence.
			if (resolutionId && compression && resolutionId->bento && compression->bento &&
				named(resolutionId->bento->typeName, {"omfi:Long", "omfi:Int32", "omfi:UInt32"}) &&
				resolutionId->encoding.size() == 4 && compression->bento->typeName == QLatin1String("omfi:String") &&
				(compression->encoding.size() == 4 ||
				 (compression->encoding.size() == 5 && compression->encoding.back() == '\0')))
			{
				facts.legacyResolution = resolution;
				facts.legacyCompression = compression->encoding.first(4);
			}
			const auto saveCodec = [&]
			{
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
				facts.bitDepth = descriptorValue(MediaProperty::BitDepth);
				facts.sampleFormat = descriptorValue(MediaProperty::SampleFormat);
				const auto names = Detail::compressionNames(facts);
				if (!names.newDnx.isEmpty() && codecProperty)
					observe(file, MediaProperty::NewDnx, m_source, descriptor, *codecProperty, names.newDnx, EvidenceBasis::Derived,
							QStringLiteral("Verified coding profile mapped to Avid's unified DNx branding."));
				if (!names.oldDnx.isEmpty() && codecProperty)
					observe(file, MediaProperty::OldDnx, m_source, descriptor, *codecProperty, names.oldDnx,
							EvidenceBasis::Derived, QStringLiteral("Historical HD/HR profile identity; not inferred from image dimensions."));
				if (!names.reallyOldDnx.isEmpty() && codecProperty)
					observe(file, MediaProperty::ReallyOldDnx, m_source, descriptor, *codecProperty, names.reallyOldDnx, EvidenceBasis::Derived,
							!namingRate.sameRate(unitsRate)
								? QStringLiteral("Verified legacy OMF/MDB name-table clock spelling for resolution ID %1: recorded %2/%3, naming operating point %4/%5. Matching MDB/MXF identities or Avid's supplied OMF slate corroborate this exact profile/clock pair; raster, layout, depth and sampling must also match Avid's 2012 white paper pp. 9–10. Recorded Frame Rate and Duration are unchanged.")
									  .arg(*resolution)
									  .arg(unitsRate.numerator)
									  .arg(unitsRate.denominator)
									  .arg(namingRate.numerator)
									  .arg(namingRate.denominator)
								: QStringLiteral("Exact profile, stored raster, frame layout, depth, sampling and rational rate match Avid's 2012 white paper pp. 9–10."));
				const auto *anchor = names.legacyIdentifiersUsed ? compression : codecProperty;
				if (!anchor && (cls == QLatin1String("PCMA") || cls == QLatin1String("WAVE")))
					anchor = classProperty;
				if (!names.compression.isEmpty() && anchor)
					observe(file, MediaProperty::Compression, m_source, descriptor, *anchor, names.compression, EvidenceBasis::Derived,
							names.legacyIdentifiersUsed
								? QStringLiteral("Typed %1 descriptor Compression '%2' and applicable DIDResolutionID/descriptor facts match the shared verified Avid name catalogue%3; original properties retained.")
									  .arg(cls, QString::fromLatin1(compression->encoding.first(4)), codingAbsent ? QStringLiteral("; EssenceCompression is absent") : QStringLiteral(" and its exact compatible recorded EssenceCompression label"))
								: QStringLiteral("Established descriptor coding and shared verified Avid compression-name catalogue; original identifiers retained."));
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
			if (layout && *layout >= 0 && *layout <= 3)
			{
				const bool complete = m_source.outcome == ParsedSource::Outcome::Complete;
				const auto read = [&](const QByteArray &name, std::optional<qint64> fallback)
				{
					return !properties(descriptor, {name.constData()}).isEmpty() ? integer(unique(descriptor, {name.constData()}))
						   : complete											 ? fallback
																				 : std::nullopt;
				};
				const auto rectangle = [&](const char *prefix)
				{
					const QByteArray base = QByteArray("OMFI:DIDD:") + prefix;
					return Detail::PictureRectangle{read(base + "Width", storedWidth), read(base + "Height", storedHeight),
													read(base + "XOffset", 0), read(base + "YOffset", 0)};
				};
				const auto recorded = [&](const char *prefix)
				{
					const QByteArray base = QByteArray("OMFI:DIDD:") + prefix;
					return !properties(descriptor, {(base + "Width").constData(), (base + "Height").constData(),
													(base + "XOffset").constData(), (base + "YOffset").constData()})
								.isEmpty();
				};
				Detail::PictureGeometry geometry;
				geometry.stored = {storedWidth, storedHeight, 0, 0};
				geometry.sampled = rectangle("Sampled");
				geometry.display = rectangle("Display");
				geometry.sampledRecorded = recorded("Sampled");
				geometry.displayRecorded = recorded("Display");
				geometry.layout = layout;
				geometry.heightMultiplier = *layout == 1 || (omf1 && *layout == 3) ? 2 : 1;
				geometry.coding = label;
				geometry.resolutionId = resolution;
				const auto selected = Detail::visibleGeometry(geometry);
				if (selected.origin != Detail::VisibleGeometry::Origin::Unknown)
				{
					const auto *anchor = selected.origin == Detail::VisibleGeometry::Origin::Display
											 ? unique(descriptor, {"OMFI:DIDD:DisplayWidth"})
											 : storedWidthProperty;
					if (!anchor)
						anchor = storedWidthProperty;
					const QString reason = selected.origin == Detail::VisibleGeometry::Origin::VerifiedProxy
											   ? QStringLiteral("Verified Avid H.264 descriptor configuration (ResolutionID %1, coding label, all three rasters, layout and zero offsets) selects this file's stored proxy raster. Matching specimens were independently checked with ffprobe; this is a qualified inference, not a universal proxy flag.").arg(*geometry.resolutionId)
											   : QStringLiteral("Visible raster: OMF Display offsets are relative to Stored, independently of Sampled. The Display rectangle is validated within Stored before field-height handling. Absent optional properties follow original toolkit Stored/zero defaults, including legacy partial sets; unreadable/conflicting values do not default. All recorded rectangles remain retained.");
					observe(file, MediaProperty::Resolution, m_source, descriptor, *anchor,
							QStringLiteral("%1x%2").arg(selected.width).arg(selected.height), EvidenceBasis::Derived, reason);
				}
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
					if (codingAbsent && complete &&
						compression && (compression->encoding == "NONE" || compression->encoding == QByteArray("NONE\0", 5)) &&
						oneValue(pixels->encoding, 'A') && depths && !depths->encoding.isEmpty() &&
						quint8(depths->encoding.front()) > 0 && quint8(depths->encoding.front()) <= 64 &&
						oneValue(depths->encoding, depths->encoding.front()))
					{
						facts.legacyCompression = QByteArray("NONE");
						facts.alphaDepth = quint8(depths->encoding.front());
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
	}

	Projection projectMdb(const ParsedSource &source, const Cancellation &cancellation)
	{
		if (cancellation.cancelled())
			return {};
		return MdbProjection(source, cancellation).project();
	}
}
