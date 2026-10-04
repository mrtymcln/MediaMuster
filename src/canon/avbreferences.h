#pragma once

// Builds a reference graph from already-read bins. Choosing sequences walks
// that graph; it does not reread files, merge physical copies or choose metadata
// for display. The result keeps the edges needed to explain each inclusion.

#include "scanmodel.h"
#include <QHash>

namespace Canon
{
	struct AvbObjectKey
	{
		qsizetype source = -1; ///< Index in AvbReferenceIndex::sources(), not a filename.
		ObjectHandle object = 0;
		bool operator==(const AvbObjectKey &other) const
		{
			return source == other.source && object == other.object;
		}
	};

	struct AvbSequence
	{
		AvbObjectKey key;
		QString name;
		QByteArray mobId;
		bool userPlaced = false;
		PropertyLocator membership;
	};

	struct AvbScope
	{
		enum class Kind
		{
			EntireBin,
			SelectedSequences
		};
		qsizetype source = -1;
		Kind kind = Kind::EntireBin;
		QVector<ObjectHandle> sequences; ///< Explicitly empty means no sequences; never whole-bin fallback.
	};

	struct AvbResolvedEdge
	{
		AvbObjectKey origin;
		AvbObjectKey target;
		qsizetype relationship = -1; ///< Index in the origin source's retained relationships.
	};

	struct AvbMediaReference
	{
		AvbObjectKey locator;
		qsizetype property = -1; ///< Index of the full identity, or first legacy word.
		QByteArray mobId;		 ///< Canonical 32-byte identity, material numeric fields little-endian.
		QByteArray legacyId;	 ///< Eight explicitly legacy bytes; never truncate a modern ID into this field.
	};

	struct AvbReferenceIssue
	{
		enum class Kind
		{
			InvalidSelection,
			IncompleteSource,
			UnresolvedReference,
			AmbiguousReference,
			UnsupportedIdentity,
			Cancelled
		};
		Kind kind;
		AvbObjectKey object;
		PropertyLocator property;
		QString explanation;
	};

	struct AvbTerminalReference
	{
		enum class Kind
		{
			Null,
			Filler
		};
		AvbObjectKey object;
		qsizetype relationship = -1;
		Kind kind;
	};

	struct AvbResolution
	{
		QVector<AvbObjectKey> roots;
		QVector<AvbResolvedEdge> edges;
		QVector<AvbMediaReference> media;
		QVector<AvbTerminalReference> terminals;
		QVector<AvbReferenceIssue> issues;
		bool complete = false; ///< Reference coverage, not filter eligibility or proof that physical files exist.
		bool cancelled = false;
	};

	class AvbReferenceIndex
	{
	public:
		explicit AvbReferenceIndex(QVector<QSharedPointer<const ParsedSource>> sources,
								   const Cancellation &cancellation);
		const QVector<QSharedPointer<const ParsedSource>> &sources() const { return m_sources; }
		const QVector<AvbSequence> &sequences() const { return m_sequences; }
		AvbResolution resolve(const QVector<AvbScope> &scopes, const Cancellation &cancellation) const;

	private:
		struct SourceIndex
		{
			QHash<ObjectHandle, qsizetype> objects;
			QHash<ObjectHandle, QVector<qsizetype>> outgoing;
		};
		QVector<QSharedPointer<const ParsedSource>> m_sources;
		QVector<SourceIndex> m_indices;
		QHash<QByteArray, QVector<AvbObjectKey>> m_mobs;
		QVector<AvbSequence> m_sequences;
		bool m_complete = false;
	};
}
