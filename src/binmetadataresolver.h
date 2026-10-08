#pragma once

#include <QHash>
#include <QString>
#include <QVector>
#include "mediaevidence.h"

struct AvbBin;
struct AvbMob;
struct MediaFile;
namespace Canon
{
	struct ParsedSource;
}

// Resolves optional bin-derived names without replacing scanner evidence.
// Reloading bins retracts only values previously supplied by this resolver.
// Matches master IDs exactly in the shared PMR/MDB representation.
class BinMetadataResolver
{
public:
	void setBins(const QVector<AvbBin> &bins);
	// Returns whether semantic row values changed, including derived effects.
	bool apply(MediaFile &file) const;

private:
	struct MasterMobMetadata
	{
		void merge(const AvbMob &mob, const QSharedPointer<const Canon::ParsedSource> &source);
		QString clipName;
		QString originalBin;
		QString originalBinUid;
		bool nameConflict = false;
		bool binConflict = false;
		QVector<MetadataObservation> names;
		QVector<MetadataObservation> bins;
		QVector<QSharedPointer<const Canon::ParsedSource>> sources;
	};
	QHash<QString, MasterMobMetadata> m_metadataByMasterMobId;
};
