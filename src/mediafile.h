#pragma once

#include "precompute.h"
#include "mediaduration.h"
#include "mediaevidence.h"
#include "mediametadatastatus.h"
#include "opscanreceipt.h"

#include <QString>
#include <QDateTime>
#include <QMetaType>
#include <cmath>
#include <optional>
#include <tuple>

// MARK: - MediaFile

/// One physical file in the scan inventory, identified by its path.
/// FileMobId and MasterMobId connect it to Avid records, bins and relatives;
/// either ID may be unknown. Display helpers are shared with the table and CSV.
struct MediaFile
{
	// MARK: Identity
	KelpieId kelpieId = 0; ///< Physical row identity within this scan session only.
	MediaMetadataStatus metadataStatus;
	MediaScanStamp scanStamp;
	/// Frozen operation checks; absence means they were never captured.
	std::optional<OpScanReceipt> operationScanReceipt;
	// Full IDs use PMR/MDB field order. The scanner converts MXF header IDs
	// before storing them; database, OMF and AVB readers already use this order.
	QString fileMobId;	 ///< FileMobId recovered from databases or media metadata.
	QString masterMobId; ///< Selected MasterMobId, shared by related essence files.

	// MARK: MDB and PMR metadata

	/// MediaEngine's shared policy chooses the name and captures its source in metadataStatus.
	/// Source-package names do not establish a master clip name or a recorded source path.
	/// Unknown clip names stay blank rather than falling back to filenames.
	QString clipName;

	/// Selected project name under MediaEngine's shared policy. Empty means unknown,
	/// independently of whether the folder's PMR currently lists this file.
	QString project;
	QString originalBin; ///< The recorded import-time _ORG_BIN, from scanned media metadata.

	// MARK: MXF or MDB technical metadata

	QString compression;		   ///< "Avid DNx SQ (DNxHD 145)", "PCM Audio", etc.
	QString resolution;			   ///< "1920x1080". Video only; audio rows stay blank.
	QString frameRate;			   ///< Display label: "23.976", "25". Video only; audio rows stay blank.
	MediaRate frameRateRatio;	   ///< Original video fraction; never recovered from the display label.
	QString bitDepth;			   ///< "10-bit", "24-bit".
	int sampleRate = 0;			   ///< Whole-Hz compatibility value; prefer sampleRateRatio when available.
	MediaRate sampleRateRatio;	   ///< Original audio sampling fraction, separate from duration's unit rate.
	/// File duration with exact source units/rate and provenance. Master and
	/// sibling lengths may differ; association never depends on equal duration.
	MediaDuration duration;
	/// Nominal timecode base for duration rendering (24, 25, 30, 60...).
	/// Parser-derived; 0 = unknown (durationDisplay falls back to the frameRate
	/// display string, and shows blank when neither is available).
	int timecodeBase = 0;
	/// Drop-frame numbering (29.97/59.94). The stored frame count stays unchanged.
	bool dropFrame = false;
	QString sourceFilePath; ///< Selected recorded source/import path or URL; distinct from the current location.

	// MARK: Precompute detail (table, CSV and filtering when enabled)

	/// Precompute details derived from the clip name and effect catalogue.
	/// Unknown effects keep their token; sequence spelling follows the recorded name.
	/// Empty for ordinary media.
	QString effect;
	QString effectCategory;
	QString effectSequence;
	using PrecomputeCategory = AvidPrecompute::Category;
	PrecomputeCategory precomputeCategory = PrecomputeCategory::Unknown;

	// MARK: Filesystem

	QString mediaFilePath;
	QString fileName;
	QString volumeName;
	QString volumePath;
	/// Containing folder name, e.g. "1", "MartyiMac.2", "Interview",
	/// "Quarantined Files", or "OMFI MediaFiles".
	QString mediaFolderName;
	/// Set from the accepted OMFI tree, independently of database contents.
	/// Selects the OMF reader and preserve-structure transfer destination.
	/// Rebalance explicitly excludes this family.
	bool omfEra = false;
	qint64 sizeBytes = 0;
	/// Filesystem creation (birth) time. Invalid when the file system
	/// doesn't record one — displayed blank, never substituted.
	QDateTime created;
	/// Filesystem modification time, retained for database freshness and
	/// file-operation checks rather than displayed in the table or CSV.
	QDateTime modified;

	// MARK: Classification

	/// Audio or video essence — unknown until metadata identifies it.
	enum class Kind : int
	{
		Unknown = -1,
		Video = 0,
		Audio = 1
	};
	Kind kind = Kind::Unknown;

	/// Master-clip media or a precompute — the "Type" column.
	/// Unknown when the usage metadata has not established either value.
	using Type = MediaType;
	Type type = Type::Unknown;

	/// Local physical-file references, independent of metadata completeness,
	/// database freshness and sequence usage. A recovered name or project alone
	/// does not establish membership in a local PMR or MDB.
	enum class DbStatus : int
	{
		Listed,		 ///< A validated local PMR or MDB entry references this physical file.
		NoReference, ///< A complete local check found no match; no valid reference was established.
		NoDatabase,	 ///< No local PMR or MDB is available to check.
		DbUnreadable ///< Local reference checks are unresolved or have not yet been performed.
	};
	DbStatus dbStatus = DbStatus::Listed;

	/// Missing databases and unresolved checks share this tab; tooltips distinguish them.
	bool isNoDatabase() const
	{
		return dbStatus == DbStatus::NoDatabase || dbStatus == DbStatus::DbUnreadable;
	}
	bool isNoReference() const
	{
		return dbStatus == DbStatus::NoReference;
	}
	/// No project name was recovered; this says nothing about database
	/// membership or whether a sequence uses the file.
	bool hasNoProject() const { return project.isEmpty(); }

	bool isNonPortable = false; ///< Filename falls outside the scanner's character allowlist.
	bool isQuarantined = false; ///< Scanner-confirmed MXF quarantine location.

	// MARK: Status words

	/// Shared database-status labels and explanations. Missing databases and
	/// unresolved checks share a visible label but retain different explanations.
	struct DbStatusText
	{
		QString label;
		QString why;
	};
	static DbStatusText dbStatusText(DbStatus s)
	{
		switch (s)
		{
		case DbStatus::Listed:
			return {QStringLiteral("Listed"), {}};
		case DbStatus::NoReference:
			return {QStringLiteral("No Reference"),
					QStringLiteral("A complete check of a local PMR or MDB database found no reference "
								   "to this physical file. Other database problems are reported in Console. "
								   "This does not establish database freshness or sequence usage.")};
		case DbStatus::NoDatabase:
			return {QStringLiteral("No Database"),
					QStringLiteral("No local PMR or MDB database is available in this folder, "
								   "so physical-file references could not be checked.")};
		case DbStatus::DbUnreadable:
			return {QStringLiteral("No Database"),
					QStringLiteral("The local PMR or MDB reference checks could not be completed reliably, "
								   "or have not yet been performed. Database presence alone does not "
								   "establish whether this physical file is referenced.")};
		}
		return {};
	}
	DbStatusText dbStatusText() const { return dbStatusText(dbStatus); }

	/// Why a row says "No project" — the matching sentence for hasNoProject().
	static QString noProjectWhy()
	{
		return QStringLiteral("Nothing names a project for this file — not the folder's PMR entry, "
							  "not the file's own header. Avid writes the project into both when it "
							  "creates media; some ingest tools and older media leave it blank.");
	}

	/// Shared table, sidebar and CSV label; an empty name displays as "No project".
	QString projectDisplay() const
	{
		return project.isEmpty() ? QStringLiteral("No project") : project;
	}

	// MARK: Derived display

	// The table, CSV and filters share these labels. Unknown effect names
	// stay selectable even when a renamed clip no longer carries a token.
	QString effectDisplay() const
	{
		if (type != Type::Precompute)
			return {};
		return effect.isEmpty() ? QStringLiteral("unknown") : effect;
	}

	QString effectCategoryDisplay() const
	{
		if (type != Type::Precompute)
			return {};
		return effectCategory.isEmpty() ? QStringLiteral("unknown") : effectCategory;
	}

	QString precomputeCategoryDisplay() const
	{
		if (type != Type::Precompute)
			return {};
		switch (precomputeCategory)
		{
		case PrecomputeCategory::RenderedEffects:
			return QStringLiteral("Rendered Effects");
		case PrecomputeCategory::TitlesAndMatteKeys:
			return QStringLiteral("Titles and Matte Keys");
		case PrecomputeCategory::Unknown:
			return QStringLiteral("unknown");
		}
		return QStringLiteral("unknown");
	}

	/// Shared Kind label for the table, sorting and CSV.
	QString kindDisplay() const
	{
		switch (kind)
		{
		case Kind::Audio:
			return QStringLiteral("Audio");
		case Kind::Video:
			return QStringLiteral("Video");
		case Kind::Unknown:
			return QString{};
		}
		return QString{};
	}

	/// Shared Type label for the table and CSV.
	QString typeDisplay() const
	{
		switch (type)
		{
		case Type::Media:
			return QStringLiteral("Media");
		case Type::Precompute:
			return QStringLiteral("Precompute");
		case Type::Unknown:
			return QStringLiteral("\u2014");
		}
		return QStringLiteral("\u2014");
	}

	/// Decimal MB for table/CSV display; sorting uses the exact byte count.
	QString sizeMBDisplay() const { return QString::number(sizeBytes / 1'000'000.0, 'f', 1); }

	/// Shared Date Created format; blank when the filesystem records no birth time.
	QString createdDisplay() const
	{
		return created.isValid() ? created.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
								 : QString();
	}

	/// Preserve unknown clip names as blank in the table, sort and CSV.
	/// The on-disk filename is a separate fact, not a fallback clip name.
	const QString &clipNameDisplay() const
	{
		return clipName;
	}

	/// Numeric audio rate for presentation; calculations use the original fraction.
	double sampleRateHz() const
	{
		return sampleRateRatio.valid() ? sampleRateRatio.value() : qMax(0, sampleRate);
	}

	/// Audio sample rate shared by the table and CSV; unknown rates stay blank.
	QString sampleRateDisplay() const
	{
		const double rate = sampleRateHz();
		return rate > 0 ? QStringLiteral("%1 kHz").arg(rate / 1000.0, 0, 'g', 10) : QString();
	}

	/// Recorded timecode base, or the rounded Frame Rate when valid. 0 means unknown.
	int effectiveTimecodeBase() const
	{
		if (timecodeBase > 0)
			return timecodeBase;
		const double rate = frameRate.toDouble();
		return (rate >= 1.0 && rate < 1000.0) ? static_cast<int>(std::round(rate)) : 0;
	}

	/// Numeric HH, MM, SS, FF for display and sorting. Hour -1 means unknown.
	std::tuple<qint64, int, int, int> durationTimecode() const
	{
		const qint64 durationFrames = duration.displayFrames();
		if (durationFrames <= 0)
			return {-1, 0, 0, 0};
		const int base = effectiveTimecodeBase();
		if (base < 1)
			return {-1, 0, 0, 0};

		qint64 minutes;
		int secs, frames;
		if (dropFrame && (base == 30 || base == 60))
		{
			// Skip 2 frame numbers per minute (4 at base 60), except every tenth minute.
			const int dropPerMin = base / 15;
			const qint64 perTenMin = qint64(base) * 600 - 9 * dropPerMin;
			const qint64 perMin = qint64(base) * 60 - dropPerMin;
			const qint64 tenBlocks = durationFrames / perTenMin;
			qint64 rem = durationFrames % perTenMin;
			qint64 frameInMin;
			if (rem < perMin + dropPerMin)
			{
				// First minute of each ten-minute block keeps all its frames.
				minutes = tenBlocks * 10;
				frameInMin = rem;
			}
			else
			{
				rem -= perMin + dropPerMin;
				minutes = tenBlocks * 10 + 1 + rem / perMin;
				frameInMin = rem % perMin + dropPerMin;
			}
			secs = static_cast<int>(frameInMin / base);
			frames = static_cast<int>(frameInMin % base);
		}
		else
		{
			frames = static_cast<int>(durationFrames % base);
			const qint64 totalSecs = durationFrames / base;
			secs = static_cast<int>(totalSecs % 60);
			minutes = totalSecs / 60;
		}
		return {minutes / 60, static_cast<int>(minutes % 60), secs, frames};
	}

	/// Duration as timecode; drop-frame uses semicolons. Unknown stays blank.
	QString durationDisplay() const
	{
		const auto [hours, minutes, secs, frames] = durationTimecode();
		if (hours < 0)
			return {};
		const int base = effectiveTimecodeBase();
		const QChar sep = dropFrame && (base == 30 || base == 60) ? QLatin1Char(';') : QLatin1Char(':');
		// Separate placeholders keep separators from merging with numbered %N markers.
		return QStringLiteral("%1%2%3%4%5%6%7")
			.arg(hours, 2, 10, QChar('0'))
			.arg(sep)
			.arg(minutes, 2, 10, QChar('0'))
			.arg(sep)
			.arg(secs, 2, 10, QChar('0'))
			.arg(sep)
			.arg(frames, 2, 10, QChar('0'));
	}
};

Q_DECLARE_METATYPE(MediaFile)
