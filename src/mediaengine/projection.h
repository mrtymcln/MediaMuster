#pragma once

// Gives understood source properties their application meaning. The original
// ParsedSource remains the authority: projections retain its object references
// and observations, and never merge physical files or choose a display winner.

#include "scanmodel.h"
#include "mediaduration.h"
#include <initializer_list>

namespace MediaEngine
{
	struct ProjectedFile
	{
		QString fileMobId;
		QStringList masterMobIds;
		QStringList filenames; // PMR-local names, not guesses from clip names.
		QVector<ObjectReference> objects;
		MediaEvidence evidence;
	};

	struct Projection
	{
		QVector<ProjectedFile> files;
		QVector<ProjectedFile> masters; // Editorial facts; never a substitute for a file descriptor.
		QStringList diagnostics;
	};

	Projection projectPmr(const ParsedSource &source, const Cancellation &cancellation);
	Projection projectMdb(const ParsedSource &source, const Cancellation &cancellation);
	Projection projectMxf(const ParsedSource &source, const Cancellation &cancellation);
	Projection projectOmf(const ParsedSource &source, const Cancellation &cancellation);

	// MXF wire UMIDs and database IDs have different material-byte conventions.
	QString canonicalMxfId(const QByteArray &bytes);
	QString canonicalDatabaseId(const QByteArray &bytes, bool bigEndian = false);
	// Interpret unlabelled text for display without changing the source property.
	RawProperty withInferredText(const RawProperty &property, qsizetype prefixBytes = 0, bool allowMacRoman = false);
	MetadataObservation observation(const ParsedSource &source, const AvidObject &object,
									const RawProperty &property, const QVariant &value,
									EvidenceBasis basis = EvidenceBasis::Recorded, const QString &explanation = {});
	void observe(ProjectedFile &file, MediaProperty field, const ParsedSource &source,
				 const AvidObject &object, const RawProperty &property, const QVariant &value,
				 EvidenceBasis basis = EvidenceBasis::Recorded, const QString &explanation = {});
	void recordPropertyCoverage(ProjectedFile &file, MediaProperty field, const ParsedSource &source,
								const AvidObject &object, std::initializer_list<const char *> names,
								bool completeObject);
	QVariantMap rateValue(MediaRate rate);
	MediaRate mediaRate(const QVariant &value);
	QVariantMap durationValue(const MediaDuration &duration);
	MediaDuration mediaDuration(const QVariant &value);
	void selectEffectMetadata(MediaEvidence &evidence);
	void appendEvidence(MediaEvidence &target, const MediaEvidence &source, bool eligible = true);
}
