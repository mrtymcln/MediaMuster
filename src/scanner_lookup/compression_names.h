#pragma once

#include "media/media_rate.h"

#include <QByteArray>
#include <QPair>
#include <QString>
#include <optional>

namespace Bella::Lookup
{
	// MARK: - Compression facts

	struct CompressionFacts
	{
		QByteArray codingLabel;
		QString descriptorClass;
		// OMF and MDB callers check the original property types and lengths.
		QByteArray legacyCompressionIdentifier;
		std::optional<qint64> legacyResolutionId;
		bool isCodingPropertyAbsent = false; // A missing label, rather than a failed read.
		QPair<qint64, qint64> rasterSize;
		MediaRate namingRate;
		std::optional<qint64> frameLayout;
		std::optional<qint64> rawComponentDepth;
		std::optional<qint64> horizontalSubsampling;
		std::optional<qint64> verticalSubsampling;
		QString bitDepthText;
		QString sampleFormatText;
		// Set only when the reader finds a single alpha component.
		std::optional<qint64> alphaChannelBitDepth;
	};

	// MARK: - Compression names

	struct CompressionNames
	{
		QString compressionName;
		QString newDnx;
		QString oldDnx;
		QString reallyOldDnx;
		bool usedLegacyCompressionIdentifiers = false;
	};

	// MARK: - Compression lookup

	// Uses facts the reader has already checked; no file I/O or source selection.
	// Unknown details stay blank. Numbered DNx names need an exact operating point.
	[[nodiscard]] CompressionNames compressionNames(const CompressionFacts &facts);
}
