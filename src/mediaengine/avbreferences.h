#pragma once

// Builds a reference graph from already-read bins. Choosing sequences walks
// that graph; it does not reread files, merge physical copies or choose metadata
// for display. The result keeps the edges needed to explain each inclusion.

#include "scanmodel.h"
#include <QHash>

namespace MediaEngine
{
	struct AvbObjectRef
	{
		qsizetype sourceIndex = -1; ///< Index in AvbReferenceIndex::sources(), not a filename.
		ObjectHandle objectHandle = 0;
		bool operator==(const AvbObjectRef &other) const
		{
			return sourceIndex == other.sourceIndex && objectHandle == other.objectHandle;
		}
	};

	struct AvbSequenceEntry
	{
		AvbObjectRef key;
		QString name;
		QByteArray mobId;
		bool userPlaced = false;
		PropertyLocator membershipEvidence;
	};

	struct AvbSelection
	{
		enum class Kind
		{
			EntireBin,
			SelectedSequences
		};
		qsizetype sourceIndex = -1;
		Kind kind = Kind::EntireBin;
		QVector<ObjectHandle> selectedSequences; ///< Explicitly empty means no sequences; never whole-bin fallback.
	};

	struct AvbResolvedEdge
	{
		AvbObjectRef origin;
		AvbObjectRef target;
		qsizetype relationshipIndex = -1; ///< Index in the origin source's retained relationships.
	};

	struct AvbMediaReference
	{
		AvbObjectRef objectKey;
		qsizetype propertyIndex = -1; ///< Index of the full identity, or first legacy word.
		QByteArray mobId;		 ///< Canonical 32-byte identity, material numeric fields little-endian.
		QByteArray legacyId;	 ///< Eight explicitly legacy bytes; never truncate a modern ID into this field.
	};

	struct AvbResolutionIssue
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
		AvbObjectRef objectKey;
		PropertyLocator propertyLocator;
		QString explanation;
	};

	struct AvbTerminalReference
	{
		enum class Kind
		{
			Null,
			Filler
		};
		AvbObjectRef objectKey;
		qsizetype relationshipIndex = -1;
		Kind kind;
	};

	struct AvbReferenceResult
	{
		QVector<AvbObjectRef> roots;
		QVector<AvbResolvedEdge> edges;
		QVector<AvbMediaReference> mediaReferences;
		QVector<AvbTerminalReference> terminalReferences;
		QVector<AvbResolutionIssue> issues;
		bool coverageComplete = false; ///< Reference coverage, not filter eligibility or proof that physical files exist.
		bool cancelled = false;
	};

	class AvbReferenceIndex
	{
	public:
		explicit AvbReferenceIndex(QVector<QSharedPointer<const ParsedSource>> sources,
								   const Cancellation &cancellation);
		const QVector<QSharedPointer<const ParsedSource>> &sources() const { return m_sources; }
		const QVector<AvbSequenceEntry> &sequences() const { return m_sequences; }
		AvbReferenceResult resolveReferences(const QVector<AvbSelection> &scopes, const Cancellation &cancellation) const;

	private:
		struct SourceObjectIndex
		{
			QHash<ObjectHandle, qsizetype> objectIndices;
			QHash<ObjectHandle, QVector<qsizetype>> outgoing;
		};
		QVector<QSharedPointer<const ParsedSource>> m_sources;
		QVector<SourceObjectIndex> m_sourceIndices;
		QHash<QByteArray, QVector<AvbObjectRef>> m_objectsByMobId;
		QVector<AvbSequenceEntry> m_sequences;
		bool m_indexComplete = false;
	};
}
