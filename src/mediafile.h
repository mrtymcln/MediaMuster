#pragma once

#include "avidprecompute.h"
#include "mediaduration.h"
#include "mediaevidence.h"

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QVector>
#include <QMetaType>
#include <cmath>
#include <tuple>

// MARK: - MediaFile

namespace Canon
{
	struct ScanResult;
	struct ParsedSource;
}

/// One physical file in the scan inventory, identified by its path.
/// File and master MOB IDs connect it to Avid records, bins and relatives;
/// either ID may be unknown. Display helpers are shared with the table and CSV.
struct MediaFile
{
	// MARK: Identity
	KelpieId kelpieId = 0; ///< Physical row identity within this scan session only.
	MediaEvidence evidence;
	MediaScanStamp scanStamp;
	// Immutable scan receipt owns every original property and source graph.
	// This row's current path/identity fields continue to follow moves and copies.
	QSharedPointer<const Canon::ScanResult> canonScan;
	/// Bin evidence remains inspectable after its current fallback is retracted.
	QVector<QSharedPointer<const Canon::ParsedSource>> canonAvbSources;
	QStringList masterMobIds; ///< All established associations; the scalar below is compatibility only.
	QString masterMobIdDisplay() const
	{
		return masterMobIds.isEmpty() ? masterMobId : masterMobIds.join(QStringLiteral("; "));
	}

	// Full IDs use PMR/MDB field order. The scanner converts MXF header IDs
	// before storing them; database, OMF and AVB readers already use this order.
	QString fileMobId;	 ///< Avid file MobId recovered from databases or media metadata.
	QString masterMobId; ///< Master MOB — the master clip's MOB (AAF MasterMob);
						 ///< V01/A01/A02 relatives share this.

	// MARK: MDB and PMR metadata

	QString clipName;

	/// Higher-ranked recovered names replace lower-ranked ones. A media
	/// material-package name outranks MDB; agreeing loaded bins fill gaps.
	/// Source-package names describe imports/tapes and belong in sourceFileName.
	/// Unknown clip names stay blank rather than falling back to filenames.
	enum class ClipNameSource
	{
		None = 0,
		Avb = 1,
		Mdb = 2,
		MaterialPackage = 3,
	};
	ClipNameSource clipNameSource = ClipNameSource::None;

	/// Recorded project name, recovered from PMR, then MDB, then readable
	/// media metadata when still missing. Empty means unknown, independently
	/// of whether the folder's PMR currently lists this file.
	QString project;
	QString originalBin;			 ///< The recorded import-time _ORG_BIN, from media metadata or a bin reference.
	bool originalBinFromAvb = false; ///< Loaded-bin fallback; cleared when its supporting bins change.

	// MARK: MXF or MDB technical metadata

	QString codec;				   ///< "Avid DNx SQ (DNxHD 145)", "PCM Audio", etc.
	QString resolution;			   ///< "1920x1080". Video only; audio rows stay blank.
	QString frameRate;			   ///< Display label: "23.976", "25". Video only; audio rows stay blank.
	MediaRate frameRateRatio;	   ///< Original video fraction; never recovered from the display label.
	QString bitDepth;			   ///< "10-bit", "24-bit".
	QString sampleFormat;		   ///< Internal numeric representation; not a table/CSV column.
	int sampleRate = 0;			   ///< Whole-Hz compatibility value; prefer sampleRateRatio when available.
	MediaRate sampleRateRatio;	   ///< Original audio sampling fraction, separate from duration's unit rate.
	QByteArray sampleRateEncoding; ///< Original AIFF 80-bit rate, retained even when no exact fraction fits.
	int channels = 0;			   ///< Audio only.
	/// File duration with exact source units/rate and provenance. Master and
	/// sibling lengths may differ; association never depends on equal duration.
	MediaDuration duration;
	QVector<ClipTrackDuration> clipDurations; ///< Separate per-track clip values, not an aggregate.
	/// Nominal timecode base for duration rendering (24, 25, 30, 60...).
	/// Parser-derived; 0 = unknown (durationDisplay falls back to the frameRate
	/// display string, and shows blank when neither is available).
	int timecodeBase = 0;
	/// Drop-frame numbering (29.97/59.94). The stored frame count stays unchanged.
	bool dropFrame = false;
	QString sourceFilePath; ///< Path Avid recorded when the media was first imported.
	QString sourceFileName;
	QString sourceContainer; ///< "QTFF", "MXF", "MOV", etc.
	bool isImported = false;

	// MARK: Precompute detail (table, CSV and filtering when enabled)

	/// For a Precompute row: the effect Avid's catalogue knows the clip name
	/// by ("Color Correction"), else the raw token; its palette category
	/// ("Image"; "A / B" when ambiguous; "unknown" when
	/// unknown — a user-typed title, an unregistered plug-in, a renamed
	/// template); the sequence the render belongs to (as Avid wrote it, spaces
	/// as underscores). Empty on every Media row.
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

	/// Local PMR membership and database readability, independent of project
	/// metadata and sequence usage. Recovering a name from MDB or a header
	/// does not make an unlisted file Listed.
	enum class DbStatus : int
	{
		Listed,		 ///< The parsed PMR names this file.
		NoReference, ///< PMR misses the file; no database check failed.
		NoDatabase,	 ///< No PMR index is available to check.
		DbUnreadable ///< A present database could not be read reliably.
	};
	DbStatus dbStatus = DbStatus::Listed;

	/// The one "No Database" filter tab covers both couldn't-check states;
	/// the tooltip says which.
	bool isNoDatabase() const
	{
		return dbStatus == DbStatus::NoDatabase || dbStatus == DbStatus::DbUnreadable;
	}
	/// No project name was recovered; this says nothing about database
	/// membership or whether a sequence uses the file.
	bool hasNoProject() const { return project.isEmpty(); }

	/// A recovered file or master MOB ID is all zeros. Missing IDs alone do
	/// not set this flag; it is not a general identifier-validity check.
	bool isInvalidUmid = false;
	bool isNonPortable = false; ///< Filename falls outside the scanner's character allowlist.
	bool isQuarantined = false; ///< Scanner-confirmed MXF quarantine location.

	// MARK: Status words

	/// Shared database-status labels and explanations. Missing and unreadable
	/// databases use the same visible label but retain different explanations.
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
					QStringLiteral("No reference to this file in the folder's Avid databases "
								   "(msmFMID.pmr / msmMMOB.mdb) — copied in or created since Avid "
								   "last indexed the folder, or its records were removed. Media "
								   "Composer re-indexes it at next launch.")};
		case DbStatus::NoDatabase:
			return {QStringLiteral("No Database"),
					QStringLiteral("This folder has no Avid file index (msmFMID.pmr), so references "
								   "could not be checked. Normal for other seats' folders on shared "
								   "storage, for Interplay / MediaCentral, and for Quarantined Files.")};
		case DbStatus::DbUnreadable:
			return {QStringLiteral("No Database"),
					QStringLiteral("A database in this folder exists but could not be read "
								   "(corrupt, truncated, or an unsupported older version). Media "
								   "Composer rebuilds it at relaunch.")};
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

	/// "Project" column / sidebar / CSV string: the project, or "No project"
	/// when nothing names one. The words live here so every consumer agrees.
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

	/// "Kind" column / CSV string. One definition site so the table,
	/// the sort, and the export can't drift apart.
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

	/// "Type" column / CSV string; same single-site rule as kindDisplay.
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

	/// "Date Created" column AND CSV string — one format for both, with
	/// time-of-day (the two hand-rolled formats drifted apart once).
	/// Blank when the filesystem records no birth time; an unknown is
	/// never substituted.
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

	/// Experimental Clip Duration column: preserve per-track distinctions, never sum.
	QString clipDurationDisplay() const
	{
		QStringList values;
		for (const auto &track : clipDurations)
		{
			if (!track.duration.known())
				continue;
			MediaFile display;
			display.duration = track.duration;
			display.timecodeBase = qRound(track.duration.displayRate.value());
			display.dropFrame = track.dropFrame;
			QString text = display.durationDisplay();
			if (text.isEmpty())
				text = QStringLiteral("%1 units @ %2/%3").arg(track.duration.units).arg(track.duration.rate.numerator).arg(track.duration.rate.denominator);
			values.append(QStringLiteral("Track %1: %2").arg(track.trackId).arg(text));
		}
		return values.join(QStringLiteral("; "));
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
