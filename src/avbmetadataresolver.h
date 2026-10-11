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
	void setBins(const QVector<AvbBin> &bins);
	// Returns whether semantic row values changed, including derived effects.
	bool applyTo(MediaFile &file) const;

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
