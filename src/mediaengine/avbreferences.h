#pragma once

// Resolves Whole Bin references from one temporary bin graph, keeping the edges
// that explain each inclusion. No file access or display-value selection occurs here.

#include "scanmodel.h"
#include <QHash>

namespace MediaEngine
{
	struct AvbResolvedEdge
	{
		ObjectHandle origin = 0;
		ObjectHandle target = 0;
		qsizetype relationshipIndex = -1; ///< Index in this bin's retained relationships.
	};

	struct AvbMediaReference
	{
		ObjectHandle objectHandle = 0;
		qsizetype propertyIndex = -1; ///< Index of the full identity, or first legacy word.
		QByteArray fileMobId;		  ///< Canonical 32-byte FileMobId, material numeric fields little-endian.
		QByteArray legacyId;		  ///< Eight explicitly legacy bytes; never truncate a modern ID into this field.
	};

	struct AvbResolutionIssue
	{
		enum class Kind
		{
			InvalidSource,
			IncompleteSource,
			UnresolvedReference,
			AmbiguousReference,
			UnsupportedIdentity,
			Cancelled
		};
		Kind kind;
		ObjectHandle objectHandle = 0;
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
		ObjectHandle objectHandle = 0;
		qsizetype relationshipIndex = -1;
		Kind kind;
	};

	struct AvbReferenceResult
	{
		QVector<ObjectHandle> roots;
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
		explicit AvbReferenceIndex(QSharedPointer<const ParsedSource> source,
								   const Cancellation &cancellation);
		const QSharedPointer<const ParsedSource> &source() const { return m_source; }
		AvbReferenceResult resolveReferences(const Cancellation &cancellation) const;

	private:
		QSharedPointer<const ParsedSource> m_source;
		QHash<ObjectHandle, qsizetype> m_objectIndices;
		QHash<ObjectHandle, QVector<qsizetype>> m_outgoing;
		QHash<QByteArray, QVector<ObjectHandle>> m_objectsByMobId;
		QVector<ObjectHandle> m_listedCompositions;
		QVector<AvbResolutionIssue> m_membershipIssues;
		bool m_indexComplete = false;
	};
}
