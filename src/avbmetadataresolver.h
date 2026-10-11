#pragma once

#include <QHash>
#include <QString>
#include <QVector>
#include "mediaevidence.h"

struct AvbBin;
struct AvbComposition;
struct MediaFile;
namespace MediaEngine
{
	struct ParsedSource;
}

// Resolves optional bin-derived names without replacing scanner evidence.
// Reloading bins retracts only values previously supplied by this resolver.
// Matches master IDs exactly in the shared PMR/MDB representation.
class AvbMetadataResolver
{
public:
	/// Scanner output already reflects its selected evidence; transfers and
	/// other callers can require projection after changing that evidence.
	enum class RowProjection
	{
		Required,
		AlreadyApplied
	};

	void setBins(const QVector<AvbBin> &bins);
	// Returns whether semantic row values changed, including derived effects.
	bool applyTo(MediaFile &file, RowProjection projection = RowProjection::Required) const;

private:
	struct MasterMobMetadata
	{
		void merge(const AvbComposition &mob, const QSharedPointer<const MediaEngine::ParsedSource> &source);
		QString clipName;
		QString originalBinName;
		QString originalBinUid;
		bool clipNameConflict = false;
		bool originalBinConflict = false;
		QVector<MetadataObservation> clipNameObservations;
		QVector<MetadataObservation> originalBinObservations;
		QVector<QSharedPointer<const MediaEngine::ParsedSource>> avbSources;
	};
	QHash<QString, MasterMobMetadata> m_metadataByMasterMobId;
};
