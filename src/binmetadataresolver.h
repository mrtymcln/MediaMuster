#pragma once

#include <QHash>
#include <QString>
#include <QVector>

struct AvbBin;
struct AvbMob;
struct MediaFile;

// Resolves optional bin-derived names without replacing scanner evidence.
// Reloading bins retracts only values previously supplied by this resolver.
class BinMetadataResolver
{
public:
	void setBins(const QVector<AvbBin> &bins);
	// Returns whether displayed name/bin values changed.
	bool apply(MediaFile &file) const;

private:
	struct Metadata
	{
		void merge(const AvbMob &mob);
		QString clipName;
		QString originalBin;
		QString originalBinUid;
		bool nameConflict = false;
		bool binConflict = false;
	};
	QHash<QString, Metadata> m_metadata;
};
