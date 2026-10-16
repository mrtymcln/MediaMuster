// Whole Bin starts at its listed clips/sequences and follows their recorded links.
// Sequence filtering still follows recorded edges, including all selector tracks and
// precomputes. It keeps a compact reachable graph instead of copying every
// possible path, which can multiply exponentially or contain cycles.

#include "avbreferences.h"
#include <QSet>
#include <QtEndian>
#include <algorithm>

namespace MediaEngine
{
	namespace
	{
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

		bool numberIs(const AvidObject &object, const QString &name, qint64 value)
		{
			const auto *field = property(object.properties, name);
			bool ok = false;
			return field && field->decoded.toLongLong(&ok) == value && ok;
		}

		bool nonzero(const QByteArray &value)
		{
			return std::any_of(value.cbegin(), value.cend(), [](char c)
							   { return c != 0; });
		}

		std::optional<AvbTerminalReference::Kind> terminal(const QByteArray &identity)
		{
			// Exact native Avid IsNullMobID/IsFillerMobID specimens, in our
			// canonical material-LE form. See the archived native evidence receipt.
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
			if (!numberIs(object, QStringLiteral("Composition.mob_type"), 1) ||
				!numberIs(object, QStringLiteral("Composition.usage_code"), 0))
				continue;
			const auto *sequenceName = property(object.properties, QStringLiteral("Component.name"));
			const auto *placed = property(bin.properties, name.chopped(3) + QStringLiteral("user_placed"));
			m_sequences.append({object.handle, sequenceName ? sequenceName->decoded.toString() : QString{}, object.recordedIdentity, placed && placed->decoded.toBool(), edge.locator});
		}
		if (countKnown && quint64(itemIndices.size()) != itemCount)
			issue(m_membershipIssues, AvbResolutionIssue::Kind::IncompleteSource, root,
				  QStringLiteral("The bin's recorded item count and available list do not agree."));
		m_indexComplete = true;
	}

	AvbReferenceResult AvbReferenceIndex::resolveReferences(const AvbSelection &selection, const Cancellation &cancellation) const
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
			issue(result.issues, AvbResolutionIssue::Kind::InvalidSelection, 0, QStringLiteral("The selected bin is not available in this reference index."));
			return result;
		}
		result.issues = m_membershipIssues;
		if (m_source->outcome != ParsedSource::Outcome::Complete)
			issue(result.issues, AvbResolutionIssue::Kind::IncompleteSource, 0,
				  QStringLiteral("The bin was not completely read. Its reference coverage is not established."));
		QVector<ObjectHandle> pending;
		QSet<ObjectHandle> visited;
		QSet<ObjectHandle> roots;
		const auto addRoot = [&](ObjectHandle handle)
		{
			if (!roots.contains(handle))
			{
				roots.insert(handle);
				result.roots.append(handle);
				pending.append(handle);
			}
		};
		if (selection.kind == AvbSelection::Kind::EntireBin)
		{
			for (const auto handle : m_listedCompositions)
			{
				if (cancellation.cancelled())
					break;
				addRoot(handle);
			}
		}
		else
		{
			for (const auto handle : selection.selectedSequences)
			{
				if (cancellation.cancelled())
					break;
				if (std::any_of(m_sequences.cbegin(), m_sequences.cend(), [&](const auto &sequence)
								{ return sequence.objectHandle == handle; }))
					addRoot(handle);
				else
					issue(result.issues, AvbResolutionIssue::Kind::InvalidSelection, handle, QStringLiteral("The object is not a listed sequence in this bin."));
			}
		}

		while (!pending.isEmpty())
		{
			if (cancellation.cancelled())
			{
				result.cancelled = true;
				issue(result.issues, AvbResolutionIssue::Kind::Cancelled, {}, QStringLiteral("AVB reference resolution was cancelled."));
				break;
			}
			const auto key = pending.takeLast();
			if (visited.contains(key))
				continue;
			visited.insert(key);
			const auto &source = *m_source;
			const auto found = m_objectIndices.constFind(key);
			if (found == m_objectIndices.cend())
			{
				issue(result.issues, AvbResolutionIssue::Kind::UnresolvedReference, key, QStringLiteral("Referenced AVB object is absent."));
				continue;
			}
			const auto &object = source.objects[*found];
			if (!object.avb || !object.avb->interpretationComplete)
				issue(result.issues, AvbResolutionIssue::Kind::IncompleteSource, key, QStringLiteral("The referenced object's full layout is not established."));
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
					result.mediaReferences.append({key, full - object.properties.constData(), full->decoded.toByteArray(), {}});
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
							result.mediaReferences.append({key, word0 - object.properties.constData(), {}, legacy});
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
						result.terminalReferences.append({key, r, *end});
						continue;
					}
					targets = m_objectsByMobId.value(id);
					if (targets.size() > 1)
						issue(result.issues, AvbResolutionIssue::Kind::AmbiguousReference, key,
							  QStringLiteral("Multiple recorded objects have this MobID. All candidate edges remain available; no first-wins selection."), edge.locator);
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
					result.edges.append({key, target, r});
					pending.append(target);
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
