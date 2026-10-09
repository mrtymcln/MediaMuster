#pragma once

// Turns established format facts into readable Avid compression names. Readers
// keep their original properties and evidence; this helper does no reading and
// does not choose between a database and a media header.

#include "mediaduration.h"

#include <QByteArray>
#include <QPair>
#include <QString>
#include <optional>

namespace Canon::Detail
{
	struct CompressionFacts
	{
		QByteArray codingLabel;
		QString descriptorClass;
		// The OMF/MDB caller validates the original property types and lengths.
		QByteArray legacyCompression;
		std::optional<qint64> legacyResolution;
		bool codingAbsent = false; // Established absence, not an unreadable label.
		QPair<qint64, qint64> geometry;
		MediaRate rate;
		std::optional<qint64> layout;
		std::optional<qint64> depth;
		std::optional<qint64> horizontal;
		std::optional<qint64> vertical;
		QString bitDepth;
		QString sampleFormat;
		// Supplied only after the reader establishes a sole alpha component.
		std::optional<qint64> alphaDepth;
	};

	struct CompressionNames
	{
		QString compression;
		QString newDnx;
		QString oldDnx;
		QString reallyOldDnx;
		bool legacyIdentifiersUsed = false;
	};

	// Unknown or ambiguous details stay unnamed. DNx retains its separately
	// verified profile and exact historical-operating-point rules.
	CompressionNames compressionNames(const CompressionFacts &facts);
}
