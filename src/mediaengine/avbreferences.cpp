// Whole Bin starts at its listed clips/sequences and follows their recorded links.
// SourceClip links select a recorded source track by number and audio/video kind.
// All linked camera alternatives and nested portions remain in Whole Bin.
// Keep reachable edges instead of copying every possible path through a cycle.

#include "avbreferences.h"
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <limits>

namespace MediaEngine
{
	namespace
	{
		constexpr qint64 pictureKind = 1;
		constexpr qint64 audioKind = 2;
		constexpr qint64 trackHasIndex = 0x0001;
		constexpr qint64 trackHasComponent = 0x0004;
		constexpr qint64 supportedTrackFlags = 0x03ff;

		struct TrackRequest
		{
			enum class Scope
			{
				All,
				One,
				Unresolved
			};
			Scope scope = Scope::All;
			qint64 number = 0;
			qint64 kind = 0;
			bool operator==(const TrackRequest &other) const
			{
				return scope == other.scope && number == other.number && kind == other.kind;
			}
		};

		struct Visit
		{
			ObjectHandle objectHandle = 0;
			TrackRequest track;
			bool operator==(const Visit &other) const
			{
				return objectHandle == other.objectHandle && track == other.track;
			}
		};

		struct TrackMatch
		{
			std::optional<quint64> slot;
			bool descriptorAllowed = false;
		};

		size_t qHash(const Visit &visit, size_t seed = 0) noexcept
		{
			return qHashMulti(seed, visit.objectHandle, static_cast<int>(visit.track.scope), visit.track.number, visit.track.kind);
		}

		const RawProperty *property(const QVector<RawProperty> &properties, const QString &name)
		{
			const RawProperty *found = nullptr;
			for (const auto &candidate : properties)
				if (candidate.locator.name == name && candidate.state == PropertyReadState::Present)
				{
					// Conflicting repeated fields must not acquire a first-wins policy.
					if (found && found->decoded != candidate.decoded)
						return nullptr;
					found = &candidate;
				}
			return found;
		}

		std::optional<qint64> integer(const AvidObject &object, const QString &name)
		{
			const auto *field = property(object.properties, name);
			bool ok = false;
			const auto value = field ? field->decoded.toLongLong(&ok) : 0;
			return ok ? std::optional<qint64>{value} : std::nullopt;
		}

		bool audioVideoKind(qint64 kind)
		{
			return kind == pictureKind || kind == audioKind;
		}

		TrackRequest sourceTrack(const AvidObject &object)
		{
			const auto number = integer(object, QStringLiteral("SourceClip.track_id"));
			const auto kind = integer(object, QStringLiteral("Component.media_kind_id"));
			if (!number || *number <= 0 || *number > std::numeric_limits<qint16>::max() || !kind || !audioVideoKind(*kind))
				return {TrackRequest::Scope::Unresolved, 0, 0};
			return {TrackRequest::Scope::One, *number, *kind};
		}

		std::optional<quint64> trackSlot(const QString &name)
		{
			const QLatin1String prefix("TrackGroup.tracks[");
			if (!name.startsWith(prefix))
				return {};
			const auto end = name.indexOf(QLatin1String("]."), prefix.size());
			if (end < 0)
				return {};
			bool ok = false;
			const auto slot = name.mid(prefix.size(), end - prefix.size()).toULongLong(&ok);
			return ok ? std::optional<quint64>{slot} : std::nullopt;
		}

		bool nonzero(const QByteArray &value)
		{
			return std::any_of(value.cbegin(), value.cend(), [](char c)
							   { return c != 0; });
		}

		std::optional<AvbTerminalReference::Kind> terminal(const QByteArray &identity)
		{
			// Native IsNullMobID/IsFillerMobID values, in canonical material-LE form.
			// See Project Canon/evidence/avb-reference-semantics-native-evidence-2026-10-04.json.
			static const auto nullId = QByteArray::fromHex("060a2b340101010101010f00130000000000000000000000060e2b347f7f2a80");
			static const auto fillerId = QByteArray::fromHex("060a2b340101010101010f00130000000000000001000000060e2b347f7f2a80");
			if (identity.size() == 32 && (!nonzero(identity) || identity == nullId))
				return AvbTerminalReference::Kind::Null;
			if (identity == fillerId)
				return AvbTerminalReference::Kind::Filler;
			return {};
		}

		void issue(QVector<AvbResolutionIssue> &issues, AvbResolutionIssue::Kind kind, ObjectHandle handle,
				   const QString &explanation, const PropertyLocator &property = {})
		{
			issues.append({kind, handle, property, explanation});
		}
	}

	AvbReferenceIndex::AvbReferenceIndex(QSharedPointer<const ParsedSource> source,
										 const Cancellation &cancellation)
		: m_source(std::move(source))
	{
		if (cancellation.cancelled())
			return;
		if (!m_source || m_source->container != ParsedSource::Container::Avb)
		{
			m_indexComplete = true;
			return;
		}
		for (qsizetype i = 0; i < m_source->objects.size(); ++i)
		{
			if (cancellation.cancelled())
				return;
			const auto &object = m_source->objects[i];
			m_objectIndices.insert(object.handle, i);
			if (object.avb && object.avb->classId == "CMPO" && object.recordedIdentity.size() == 32 && nonzero(object.recordedIdentity))
				m_objectsByMobId[object.recordedIdentity].append(object.handle);
		}
		for (qsizetype i = 0; i < m_source->relationships.size(); ++i)
		{
			if (cancellation.cancelled())
				return;
			m_outgoing[m_source->relationships[i].origin].append(i);
		}

		const auto *rootField = property(m_source->unownedProperties, QStringLiteral("Header.root_index"));
		const auto root = rootField ? rootField->decoded.toULongLong() : 0;
		const auto rootIt = m_objectIndices.constFind(root);
		if (!root || rootIt == m_objectIndices.cend())
		{
			issue(m_membershipIssues, AvbResolutionIssue::Kind::IncompleteSource, root,
				  QStringLiteral("The bin's root object is not established; an empty reference set is not confirmed."));
			m_indexComplete = true;
			return;
		}
		const auto &bin = m_source->objects[*rootIt];
		if (!bin.avb || (bin.avb->classId != "ABIN" && bin.avb->classId != "BINF"))
		{
			issue(m_membershipIssues, AvbResolutionIssue::Kind::IncompleteSource, root,
				  QStringLiteral("The root object is not an established bin document."));
			m_indexComplete = true;
			return;
		}
		const auto *countField = property(bin.properties, QStringLiteral("Bin.item_count"));
		bool countKnown = false;
		const auto itemCount = countField ? countField->decoded.toULongLong(&countKnown) : 0;
		if (!countKnown)
			issue(m_membershipIssues, AvbResolutionIssue::Kind::IncompleteSource, root,
				  QStringLiteral("The bin's complete item list is not established."));
		QSet<quint64> itemIndices;
		QSet<ObjectHandle> listed;
		const QLatin1String itemPrefix("Bin.items[");
		const QLatin1String itemSuffix("].mob");
		for (const auto r : m_outgoing.value(root))
		{
			if (cancellation.cancelled())
				return;
			const auto &edge = m_source->relationships[r];
			const auto &name = edge.locator.name;
			if (!name.startsWith(itemPrefix) || !name.endsWith(itemSuffix))
				continue;
			bool indexKnown = false;
			const auto itemIndex = name.mid(itemPrefix.size(), name.size() - itemPrefix.size() - itemSuffix.size()).toULongLong(&indexKnown);
			if (!indexKnown || itemIndices.contains(itemIndex) || (countKnown && itemIndex >= itemCount))
			{
				issue(m_membershipIssues, AvbResolutionIssue::Kind::IncompleteSource, root,
					  QStringLiteral("The bin has an invalid or repeated item-list position."), edge.locator);
				continue;
			}
			itemIndices.insert(itemIndex);
			// The reader emits this edge only after reading the complete object index.
			if (edge.referenceEncoding != QLatin1String("AVB.ObjectIndex"))
			{
				issue(m_membershipIssues, AvbResolutionIssue::Kind::UnresolvedReference, root,
					  QStringLiteral("The listed item's object reference is not established."), edge.locator);
				continue;
			}
			if (!edge.recordedReference.toULongLong())
				continue; // A recorded null slot contributes no clip or sequence.
			const auto it = m_objectIndices.constFind(edge.target);
			if (!edge.target || it == m_objectIndices.cend())
			{
				issue(m_membershipIssues, AvbResolutionIssue::Kind::UnresolvedReference, root,
					  QStringLiteral("A listed clip or sequence is absent from this bin."), edge.locator);
				continue;
			}
			const auto &object = m_source->objects[*it];
			if (!object.avb || object.avb->classId != "CMPO")
			{
				issue(m_membershipIssues, AvbResolutionIssue::Kind::UnresolvedReference, edge.target,
					  QStringLiteral("A listed item is not an established clip or sequence."), edge.locator);
				continue;
			}
			if (listed.contains(edge.target))
				continue;
			listed.insert(edge.target);
			m_listedCompositions.append(edge.target);
		}
		if (countKnown && quint64(itemIndices.size()) != itemCount)
			issue(m_membershipIssues, AvbResolutionIssue::Kind::IncompleteSource, root,
				  QStringLiteral("The bin's recorded item count and available list do not agree."));
		m_indexComplete = true;
	}

	AvbReferenceResult AvbReferenceIndex::resolveReferences(const Cancellation &cancellation) const
	{
		AvbReferenceResult result;
		if (!m_indexComplete || cancellation.cancelled())
		{
			result.cancelled = true;
			issue(result.issues, AvbResolutionIssue::Kind::Cancelled, 0, QStringLiteral("AVB reference indexing or resolution was cancelled."));
			return result;
		}
		if (!m_source || m_source->container != ParsedSource::Container::Avb)
		{
			issue(result.issues, AvbResolutionIssue::Kind::InvalidSource, 0, QStringLiteral("The bin is not available in this reference index."));
			return result;
		}
		result.issues = m_membershipIssues;
		if (m_source->outcome != ParsedSource::Outcome::Complete)
			issue(result.issues, AvbResolutionIssue::Kind::IncompleteSource, 0,
				  QStringLiteral("The bin was not completely read. Its reference coverage is not established."));
		QVector<Visit> pending;
		QSet<Visit> visited;
		QSet<ObjectHandle> reportedIncompleteObjects;
		QSet<QPair<qsizetype, ObjectHandle>> emittedEdges;
		QSet<ObjectHandle> emittedMedia;
		QSet<qsizetype> emittedTerminals;
		for (const auto handle : m_listedCompositions)
		{
			if (cancellation.cancelled())
				break;
			result.roots.append(handle);
			pending.append(Visit{handle, {}});
		}

		const auto matchTrack = [&](const AvidObject &object, const TrackRequest &request) -> TrackMatch
		{
			const auto count = integer(object, QStringLiteral("TrackGroup.track_count"));
			const auto descriptor = integer(object, QStringLiteral("Composition.descriptor"));
			const auto outgoing = m_outgoing.value(object.handle);
			// A fully read descriptor-only file source establishes its physical file
			// independently of editorial track components.
			if (count && *count == 0 && descriptor && *descriptor > 0 &&
				std::none_of(object.properties.cbegin(), object.properties.cend(), [](const auto &field)
							 { return field.locator.name.startsWith(QLatin1String("TrackGroup.tracks[")); }) &&
				std::none_of(outgoing.cbegin(), outgoing.cend(), [&](const auto r)
							 { return m_source->relationships[r].locator.name.startsWith(QLatin1String("TrackGroup.tracks[")); }))
				return {{}, true};
			if (request.scope == TrackRequest::Scope::Unresolved)
				return {}; // The requesting SourceClip already carries the uncertainty notice.
			bool uncertain = !count || *count < 0;
			QSet<quint64> seenSlots;
			std::optional<quint64> matchingSlot;
			qsizetype matches = 0;
			for (const auto &field : object.properties)
			{
				if (cancellation.cancelled())
					return {};
				if (!field.locator.name.endsWith(QLatin1String(".flags")))
					continue;
				const auto slot = trackSlot(field.locator.name);
				if (!slot)
					continue;
				if (seenSlots.contains(*slot) || (count && *count >= 0 && *slot >= quint64(*count)))
				{
					uncertain = true;
					continue;
				}
				seenSlots.insert(*slot);
				const auto flags = integer(object, field.locator.name);
				if (!flags || *flags < 0 || (*flags & ~supportedTrackFlags))
				{
					uncertain = true;
					continue;
				}
				if (!(*flags & trackHasComponent))
					continue; // This fully read slot has no component reference.
				const auto prefix = field.locator.name.chopped(QStringLiteral(".flags").size());
				const auto number = (*flags & trackHasIndex) ? integer(object, prefix + QStringLiteral(".index")) : std::nullopt;
				if (number && *number != request.number)
					continue;
				const auto componentName = prefix + QStringLiteral(".component");
				const auto component = integer(object, componentName);
				if (component && *component == 0)
					continue; // An explicit native null cannot contribute media.
				const auto child = component && *component > 0 ? m_objectIndices.constFind(ObjectHandle(*component)) : m_objectIndices.cend();
				const auto kind = child != m_objectIndices.cend() ? integer(m_source->objects[*child], QStringLiteral("Component.media_kind_id")) : std::nullopt;
				// A different recorded kind (for example timecode) cannot match Audio/Video.
				if (kind && *kind != request.kind)
					continue;
				if (!number || !kind || !audioVideoKind(*kind))
				{
					uncertain = true;
					continue;
				}
				qsizetype establishedEdges = 0;
				for (const auto r : outgoing)
				{
					if (cancellation.cancelled())
						return {};
					const auto &edge = m_source->relationships[r];
					if (edge.locator.name == componentName && edge.referenceEncoding == QLatin1String("AVB.ObjectIndex") &&
						edge.target == ObjectHandle(*component) && edge.recordedReference.toULongLong() == edge.target)
						++establishedEdges;
				}
				if (establishedEdges != 1)
				{
					uncertain = true;
					continue;
				}
				matchingSlot = *slot;
				++matches;
			}
			if (!count || *count < 0 || quint64(seenSlots.size()) != quint64(*count))
				uncertain = true;
			if (!uncertain && matches == 1)
				return {matchingSlot, true};
			issue(result.issues, matches > 1 ? AvbResolutionIssue::Kind::AmbiguousReference : AvbResolutionIssue::Kind::UnresolvedReference,
				  object.handle, QStringLiteral("Source track %1 (media kind %2) cannot be uniquely matched; its track branches were not included.").arg(request.number).arg(request.kind));
			return {};
		};

		while (!pending.isEmpty())
		{
			if (cancellation.cancelled())
			{
				result.cancelled = true;
				issue(result.issues, AvbResolutionIssue::Kind::Cancelled, {}, QStringLiteral("AVB reference resolution was cancelled."));
				break;
			}
			const auto visit = pending.takeLast();
			if (visited.contains(visit))
				continue;
			visited.insert(visit);
			const auto key = visit.objectHandle;
			const auto &source = *m_source;
			const auto found = m_objectIndices.constFind(key);
			if (found == m_objectIndices.cend())
			{
				issue(result.issues, AvbResolutionIssue::Kind::UnresolvedReference, key, QStringLiteral("Referenced AVB object is absent."));
				continue;
			}
			const auto &object = source.objects[*found];
			if ((!object.avb || !object.avb->interpretationComplete) && !reportedIncompleteObjects.contains(key))
			{
				reportedIncompleteObjects.insert(key);
				issue(result.issues, AvbResolutionIssue::Kind::IncompleteSource, key, QStringLiteral("The referenced object's full layout is not established."));
			}
			const bool restrictTracks = object.avb && object.avb->classId == "CMPO" && visit.track.scope != TrackRequest::Scope::All;
			const auto trackMatch = restrictTracks ? matchTrack(object, visit.track) : TrackMatch{};
			if (object.avb && object.avb->classId != "MULD" &&
				property(object.properties, QStringLiteral("MediaFileDescriptor.length")))
			{
				// Physical/tape descriptors do not need a managed-file identity.
				// A multiple descriptor delegates to its children, each checked here.
				const auto *locator = property(object.properties, QStringLiteral("MediaDescriptor.locator"));
				const auto target = locator ? m_objectIndices.constFind(locator->decoded.toULongLong()) : m_objectIndices.cend();
				if (target == m_objectIndices.cend() || !source.objects[*target].avb || source.objects[*target].avb->classId != "MSML")
					issue(result.issues, AvbResolutionIssue::Kind::UnsupportedIdentity, key,
						  QStringLiteral("File descriptor has no established MSML identity route. A path-only or absent locator does not prove empty media usage."),
						  locator ? locator->locator : PropertyLocator{});
			}

			if (object.avb && object.avb->classId == "MSML")
			{
				// MSML locators identify media by FileMobId; other graph MobIds keep their roles.
				const auto *full = property(object.properties, QStringLiteral("MSMLocator.mob_id"));
				if (full && full->decoded.toByteArray().size() == 32 && !terminal(full->decoded.toByteArray()))
				{
					if (!emittedMedia.contains(key))
					{
						emittedMedia.insert(key);
						result.mediaReferences.append({key, full - object.properties.constData(), full->decoded.toByteArray(), {}});
					}
				}
				else if (std::any_of(object.properties.cbegin(), object.properties.cend(), [](const auto &field)
									 { return field.locator.name == QLatin1String("MSMLocator.mob_id"); }))
					issue(result.issues, AvbResolutionIssue::Kind::UnsupportedIdentity, key, QStringLiteral("MSML full identity is not usable or conflicts; its legacy words do not override it."));
				else if (!object.avb->interpretationComplete)
					issue(result.issues, AvbResolutionIssue::Kind::UnsupportedIdentity, key, QStringLiteral("MSML layout is incomplete; unread fields may contain a modern identity, so legacy words cannot be selected."));
				else
				{
					const auto *word0 = property(object.properties, QStringLiteral("MSMLocator.legacy_word0"));
					const auto *word1 = property(object.properties, QStringLiteral("MSMLocator.legacy_word1"));
					if (word0 && word1)
					{
						QByteArray legacy(8, Qt::Uninitialized);
						qToLittleEndian(quint32(word0->decoded.toULongLong()), legacy.data());
						qToLittleEndian(quint32(word1->decoded.toULongLong()), legacy.data() + 4);
						if (nonzero(legacy))
						{
							if (!emittedMedia.contains(key))
							{
								emittedMedia.insert(key);
								result.mediaReferences.append({key, word0 - object.properties.constData(), {}, legacy});
							}
						}
						else
							issue(result.issues, AvbResolutionIssue::Kind::UnsupportedIdentity, key, QStringLiteral("MSML records no usable file identity."), word0->locator);
					}
					else
						issue(result.issues, AvbResolutionIssue::Kind::UnsupportedIdentity, key, QStringLiteral("MSML file identity could not be read."));
				}
			}

			if (object.avb && object.avb->classId == "SCLP" && !property(object.properties, QStringLiteral("SourceClip.mob_id")))
				issue(result.issues, AvbResolutionIssue::Kind::UnsupportedIdentity, key,
					  QStringLiteral("Legacy-only SourceClip identity or terminal meaning needs verification; no modern identity was invented."));
			if (object.avb && object.avb->classId == "ASPI")
			{
				const bool recorded = std::any_of(object.properties.cbegin(), object.properties.cend(), [](const auto &field)
												  { return field.locator.name == QLatin1String("AudioSuitePluginEffect.mob_id") ||
														   field.locator.name.startsWith(QLatin1String("AudioSuitePluginEffect.legacy_word")); });
				if (recorded && !property(object.properties, QStringLiteral("AudioSuitePluginEffect.mob_id")))
					issue(result.issues, AvbResolutionIssue::Kind::UnsupportedIdentity, key,
						  QStringLiteral("Recorded AudioSuite source-master identity is legacy-only, unreadable or conflicting; dependency coverage is not established."));
			}

			for (const auto r : m_outgoing.value(key))
			{
				if (cancellation.cancelled())
					break;
				const auto &edge = source.relationships[r];
				if (restrictTracks && edge.locator.name.startsWith(QLatin1String("TrackGroup.tracks[")) &&
					(!trackMatch.slot || trackSlot(edge.locator.name) != trackMatch.slot))
					continue;
				if (restrictTracks && edge.locator.name == QLatin1String("Composition.descriptor") && !trackMatch.descriptorAllowed)
					continue;
				const bool sourceJump = object.avb && object.avb->classId == "SCLP" && edge.locator.name == QLatin1String("SourceClip.mob_id");
				if (sourceJump)
				{
					const auto *identity = property(object.properties, QStringLiteral("SourceClip.mob_id"));
					if (!identity || identity->decoded.toByteArray().size() != 32 || identity->decoded != edge.recordedReference ||
						edge.referenceEncoding != QLatin1String("AVB.MobId.MaterialLE"))
					{
						issue(result.issues, AvbResolutionIssue::Kind::UnsupportedIdentity, key,
							  QStringLiteral("The SourceClip identity is not established; its source-track link was not included."), edge.locator);
						continue;
					}
				}
				TrackRequest requestedTrack;
				QVector<ObjectHandle> targets;
				if (edge.referenceEncoding == QLatin1String("AVB.ObjectIndex"))
				{
					const auto recorded = edge.recordedReference.toULongLong();
					if (!recorded)
						continue; // Native index zero explicitly encodes no object.
					if (edge.target && m_objectIndices.contains(edge.target))
						targets.append(edge.target);
				}
				else if (edge.referenceEncoding == QLatin1String("AVB.MobId.MaterialLE"))
				{
					const auto id = edge.recordedReference.toByteArray();
					if (const auto end = terminal(id))
					{
						if (!emittedTerminals.contains(r))
						{
							emittedTerminals.insert(r);
							result.terminalReferences.append({key, r, *end});
						}
						continue;
					}
					targets = m_objectsByMobId.value(id);
					if (targets.size() > 1)
					{
						issue(result.issues, AvbResolutionIssue::Kind::AmbiguousReference, key,
							  sourceJump ? QStringLiteral("Multiple recorded objects have this MobID; the ambiguous source-track link was not included.")
										 : QStringLiteral("Multiple recorded objects have this MobID. All candidate edges remain available; no first-wins selection."),
							  edge.locator);
						if (sourceJump)
							continue;
					}
					if (sourceJump && !targets.isEmpty())
					{
						requestedTrack = sourceTrack(object);
						if (requestedTrack.scope == TrackRequest::Scope::Unresolved)
							issue(result.issues, AvbResolutionIssue::Kind::UnresolvedReference, key,
								  QStringLiteral("The requested source-track number or audio/video kind is unavailable, conflicting or unsupported; unproven track branches were not included."), edge.locator);
					}
				}
				else
				{
					issue(result.issues, AvbResolutionIssue::Kind::UnresolvedReference, key, QStringLiteral("Reference encoding is not supported by the AVB resolver."), edge.locator);
					continue;
				}
				if (targets.isEmpty())
					issue(result.issues, AvbResolutionIssue::Kind::UnresolvedReference, key,
						  QStringLiteral("Reference has no established target in this bin; this is not a missing-media diagnosis."), edge.locator);
				for (const auto &target : targets)
				{
					if (cancellation.cancelled())
						break;
					const auto occurrence = qMakePair(r, target);
					if (!emittedEdges.contains(occurrence))
					{
						emittedEdges.insert(occurrence);
						result.edges.append({key, target, r});
					}
					pending.append(Visit{target, requestedTrack});
				}
			}
		}
		if (cancellation.cancelled() && !result.cancelled)
		{
			result.cancelled = true;
			issue(result.issues, AvbResolutionIssue::Kind::Cancelled, {}, QStringLiteral("AVB reference resolution was cancelled."));
		}
		result.coverageComplete = !result.cancelled && result.issues.isEmpty();
		return result;
	}
}
