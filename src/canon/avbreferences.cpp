// Sequence filtering follows recorded edges, including all selector tracks and
// precomputes. It keeps a compact reachable graph instead of copying every
// possible path, which can multiply exponentially or contain cycles.

#include "avbreferences.h"
#include <QSet>
#include <QtEndian>
#include <algorithm>

namespace Canon
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

		void issue(AvbResolution &result, AvbReferenceIssue::Kind kind, AvbObjectKey key,
				   const QString &explanation, const PropertyLocator &property = {})
		{
			result.issues.append({kind, key, property, explanation});
		}
	}

	AvbReferenceIndex::AvbReferenceIndex(QVector<QSharedPointer<const ParsedSource>> sources,
										 const Cancellation &cancellation)
		: m_sources(std::move(sources)), m_indices(m_sources.size())
	{
		for (qsizetype s = 0; s < m_sources.size(); ++s)
		{
			if (cancellation.cancelled())
				return;
			if (!m_sources[s])
				continue;
			const auto &source = *m_sources[s];
			if (source.container != ParsedSource::Container::Avb)
				continue;
			auto &index = m_indices[s];
			for (qsizetype i = 0; i < source.objects.size(); ++i)
			{
				if (cancellation.cancelled())
					return;
				const auto &object = source.objects[i];
				index.objects.insert(object.handle, i);
				if (object.avb && object.avb->classId == "CMPO" && object.recordedIdentity.size() == 32 && nonzero(object.recordedIdentity))
					m_mobs[object.recordedIdentity].append({s, object.handle});
			}
			for (qsizetype i = 0; i < source.relationships.size(); ++i)
			{
				if (cancellation.cancelled())
					return;
				index.outgoing[source.relationships[i].origin].append(i);
			}

			const auto *rootField = property(source.unownedProperties, QStringLiteral("Header.root_index"));
			const auto root = rootField ? rootField->decoded.toULongLong() : 0;
			const auto rootIt = index.objects.constFind(root);
			if (rootIt == index.objects.cend())
				continue;
			const auto &bin = source.objects[*rootIt];
			if (!bin.avb || (bin.avb->classId != "ABIN" && bin.avb->classId != "BINF"))
				continue;
			QSet<ObjectHandle> listed;
			for (const auto r : index.outgoing.value(root))
			{
				if (cancellation.cancelled())
					return;
				const auto &edge = source.relationships[r];
				if (!edge.locator.name.startsWith(QLatin1String("Bin.items[")) || !edge.locator.name.endsWith(QLatin1String("].mob")))
					continue;
				const auto it = index.objects.constFind(edge.target);
				if (it == index.objects.cend() || listed.contains(edge.target))
					continue;
				const auto &object = source.objects[*it];
				if (!object.avb || object.avb->classId != "CMPO" ||
					!numberIs(object, QStringLiteral("Composition.mob_type"), 1) ||
					!numberIs(object, QStringLiteral("Composition.usage_code"), 0))
					continue;
				const auto *name = property(object.properties, QStringLiteral("Component.name"));
				const auto *placed = property(bin.properties, edge.locator.name.chopped(3) + QStringLiteral("user_placed"));
				m_sequences.append({{s, object.handle}, name ? name->decoded.toString() : QString{}, object.recordedIdentity, placed && placed->decoded.toBool(), edge.locator});
				listed.insert(edge.target);
			}
		}
		m_complete = true;
	}

	AvbResolution AvbReferenceIndex::resolve(const QVector<AvbScope> &scopes, const Cancellation &cancellation) const
	{
		AvbResolution result;
		if (!m_complete || cancellation.cancelled())
		{
			result.cancelled = true;
			issue(result, AvbReferenceIssue::Kind::Cancelled, {}, QStringLiteral("AVB reference indexing or resolution was cancelled."));
			return result;
		}
		QVector<AvbObjectKey> pending;
		QVector<QSet<ObjectHandle>> visited(m_sources.size());
		QVector<QSet<ObjectHandle>> roots(m_sources.size());
		QSet<qsizetype> checkedSources;
		const auto checkSource = [&](qsizetype source)
		{
			if (checkedSources.contains(source))
				return;
			checkedSources.insert(source);
			if (m_sources[source]->outcome != ParsedSource::Outcome::Complete)
				issue(result, AvbReferenceIssue::Kind::IncompleteSource, {source, 0},
					  QStringLiteral("The bin was not completely read. Its reference coverage is not established."));
		};
		const auto addRoot = [&](AvbObjectKey key)
		{
			if (!roots[key.source].contains(key.object))
			{
				roots[key.source].insert(key.object);
				result.roots.append(key);
				pending.append(key);
			}
		};
		for (const auto &scope : scopes)
		{
			if (cancellation.cancelled())
				break;
			if (scope.source < 0 || scope.source >= m_sources.size() || !m_sources[scope.source] ||
				m_sources[scope.source]->container != ParsedSource::Container::Avb)
			{
				issue(result, AvbReferenceIssue::Kind::InvalidSelection, {scope.source, 0}, QStringLiteral("The selected bin is not available in this reference index."));
				continue;
			}
			checkSource(scope.source);
			if (scope.kind == AvbScope::Kind::EntireBin)
			{
				for (const auto &object : m_sources[scope.source]->objects)
				{
					if (cancellation.cancelled())
						break;
					addRoot({scope.source, object.handle});
				}
			}
			else
			{
				for (const auto handle : scope.sequences)
				{
					if (cancellation.cancelled())
						break;
					const AvbObjectKey key{scope.source, handle};
					if (std::any_of(m_sequences.cbegin(), m_sequences.cend(), [&](const auto &sequence)
									{ return sequence.key == key; }))
						addRoot(key);
					else
						issue(result, AvbReferenceIssue::Kind::InvalidSelection, key, QStringLiteral("The object is not a listed sequence in this bin."));
				}
			}
		}

		while (!pending.isEmpty())
		{
			if (cancellation.cancelled())
			{
				result.cancelled = true;
				issue(result, AvbReferenceIssue::Kind::Cancelled, {}, QStringLiteral("AVB reference resolution was cancelled."));
				break;
			}
			const auto key = pending.takeLast();
			if (visited[key.source].contains(key.object))
				continue;
			visited[key.source].insert(key.object);
			checkSource(key.source);
			const auto &source = *m_sources[key.source];
			const auto &index = m_indices[key.source];
			const auto found = index.objects.constFind(key.object);
			if (found == index.objects.cend())
			{
				issue(result, AvbReferenceIssue::Kind::UnresolvedReference, key, QStringLiteral("Referenced AVB object is absent."));
				continue;
			}
			const auto &object = source.objects[*found];
			if (!object.avb || !object.avb->interpretationComplete)
				issue(result, AvbReferenceIssue::Kind::IncompleteSource, key, QStringLiteral("The referenced object's full layout is not established."));
			if (object.avb && object.avb->classId != "MULD" &&
				property(object.properties, QStringLiteral("MediaFileDescriptor.length")))
			{
				// Physical/tape descriptors do not need a managed-file identity.
				// A multiple descriptor delegates to its children, each checked here.
				const auto *locator = property(object.properties, QStringLiteral("MediaDescriptor.locator"));
				const auto target = locator ? index.objects.constFind(locator->decoded.toULongLong()) : index.objects.cend();
				if (target == index.objects.cend() || !source.objects[*target].avb || source.objects[*target].avb->classId != "MSML")
					issue(result, AvbReferenceIssue::Kind::UnsupportedIdentity, key,
						  QStringLiteral("File descriptor has no established MSML identity route. A path-only or absent locator does not prove empty media usage."),
						  locator ? locator->locator : PropertyLocator{});
			}

			if (object.avb && object.avb->classId == "MSML")
			{
				const auto *full = property(object.properties, QStringLiteral("MSMLocator.mob_id"));
				if (full && full->decoded.toByteArray().size() == 32 && !terminal(full->decoded.toByteArray()))
					result.media.append({key, full - object.properties.constData(), full->decoded.toByteArray(), {}});
				else if (std::any_of(object.properties.cbegin(), object.properties.cend(), [](const auto &field)
									 { return field.locator.name == QLatin1String("MSMLocator.mob_id"); }))
					issue(result, AvbReferenceIssue::Kind::UnsupportedIdentity, key, QStringLiteral("MSML full identity is not usable or conflicts; its legacy words do not override it."));
				else if (!object.avb->interpretationComplete)
					issue(result, AvbReferenceIssue::Kind::UnsupportedIdentity, key, QStringLiteral("MSML layout is incomplete; unread fields may contain a modern identity, so legacy words cannot be selected."));
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
							result.media.append({key, word0 - object.properties.constData(), {}, legacy});
						else
							issue(result, AvbReferenceIssue::Kind::UnsupportedIdentity, key, QStringLiteral("MSML records no usable file identity."), word0->locator);
					}
					else
						issue(result, AvbReferenceIssue::Kind::UnsupportedIdentity, key, QStringLiteral("MSML file identity could not be read."));
				}
			}

			if (object.avb && object.avb->classId == "SCLP" && !property(object.properties, QStringLiteral("SourceClip.mob_id")))
				issue(result, AvbReferenceIssue::Kind::UnsupportedIdentity, key,
					  QStringLiteral("Legacy-only SourceClip identity or terminal meaning needs verification; no modern identity was invented."));
			if (object.avb && object.avb->classId == "ASPI")
			{
				const bool recorded = std::any_of(object.properties.cbegin(), object.properties.cend(), [](const auto &field)
												  { return field.locator.name == QLatin1String("AudioSuitePluginEffect.mob_id") ||
														   field.locator.name.startsWith(QLatin1String("AudioSuitePluginEffect.legacy_word")); });
				if (recorded && !property(object.properties, QStringLiteral("AudioSuitePluginEffect.mob_id")))
					issue(result, AvbReferenceIssue::Kind::UnsupportedIdentity, key,
						  QStringLiteral("Recorded AudioSuite source-master identity is legacy-only, unreadable or conflicting; dependency coverage is not established."));
			}

			for (const auto r : index.outgoing.value(key.object))
			{
				if (cancellation.cancelled())
					break;
				const auto &edge = source.relationships[r];
				QVector<AvbObjectKey> targets;
				if (edge.referenceEncoding == QLatin1String("AVB.ObjectIndex"))
				{
					const auto recorded = edge.recordedReference.toULongLong();
					if (!recorded)
						continue; // Native index zero explicitly encodes no object.
					if (edge.target && index.objects.contains(edge.target))
						targets.append({key.source, edge.target});
				}
				else if (edge.referenceEncoding == QLatin1String("AVB.MobId.MaterialLE"))
				{
					const auto id = edge.recordedReference.toByteArray();
					if (const auto end = terminal(id))
					{
						result.terminals.append({key, r, *end});
						continue;
					}
					targets = m_mobs.value(id);
					if (targets.size() > 1)
						issue(result, AvbReferenceIssue::Kind::AmbiguousReference, key,
							  QStringLiteral("Multiple recorded objects have this MobID. All candidate edges remain available; no first-wins selection."), edge.locator);
				}
				else
				{
					issue(result, AvbReferenceIssue::Kind::UnresolvedReference, key, QStringLiteral("Reference encoding is not supported by the AVB resolver."), edge.locator);
					continue;
				}
				if (targets.isEmpty())
					issue(result, AvbReferenceIssue::Kind::UnresolvedReference, key,
						  QStringLiteral("Reference has no established target in the loaded bins; this is not a missing-media diagnosis."), edge.locator);
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
			issue(result, AvbReferenceIssue::Kind::Cancelled, {}, QStringLiteral("AVB reference resolution was cancelled."));
		}
		result.complete = !result.cancelled && result.issues.isEmpty();
		return result;
	}
}
