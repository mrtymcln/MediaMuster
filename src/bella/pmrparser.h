#pragma once

#include "mobid.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <optional>

namespace Bella
{
	enum class PmrReadResult
	{
		NotRead,
		Completed,
		NoUsableSet, // No usable set selected; diagnostics can include a rejected alternative.
		Malformed,
		Unsupported,
		IoError,
		Changed,
		Cancelled
	};

	enum class PmrCheckResult
	{
		NotChecked,
		Verified,
		Unverified,
		Changed
	};

	struct PmrEntry
	{
		MobId fileMobId;
		QString filename;
		QString project;
		std::optional<MobId> masterMobId;
	};

	struct PmrSet
	{
		qint32 version = 0;
		QVector<PmrEntry> entries;
	};

	struct PmrStamp
	{
		std::optional<qint64> sizeBytes;
		QDateTime modified;
	};

	struct PmrSource
	{
		QString path;
		PmrStamp capturedStamp;
	};

	struct PmrRecords
	{
		// One wholly validated set; unsupported content permits positive references only.
		std::optional<PmrSet> knownSet;
		bool canUsePmrMetadata = false;
		// Format coverage only; the scanner still qualifies source and physical matches.
		bool referenceCoverageComplete = false;
	};

	struct PmrReport
	{
		PmrSource source;
		PmrReadResult reading = PmrReadResult::NotRead;
		PmrCheckResult sizeDateCheck = PmrCheckResult::NotChecked;
		PmrRecords records;
		// Reason text only; include source.path when displaying/logging each diagnostic.
		QStringList diagnostics;
	};

	/// Reads once into temporary memory and returns owned records and qualifications.
	/// The caller owns the shared stop flag for the duration of this synchronous call.
	[[nodiscard]] PmrReport parsePmr(const QString &path, const std::atomic_bool &stopRequested);

	namespace Detail
	{
		/// Pure comparison; missing/negative size or invalid date cannot verify equality.
		[[nodiscard]] PmrCheckResult comparePmrStamps(const PmrStamp &before, const PmrStamp &after);
	}
}
