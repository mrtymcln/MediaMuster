#pragma once

#include "avidprecompute.h"

#include <QString>
#include <QDateTime>
#include <QVector>
#include <QMetaType>
#include <cmath>

// MARK: - MediaFile

/// One physical file in the scan inventory, identified by its path.
/// File and master MOB IDs connect it to Avid records, bins and relatives;
/// either ID may be unknown. Display helpers are shared with the table and CSV.
struct MediaFile
{
	// MARK: Identity

	QString mobId;		 ///< Avid file MOB ID recovered from databases or media metadata.
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

	QString codec;		///< "Avid DNx SQ (DNxHD 145)", "PCM Audio", etc.
	QString resolution; ///< "1920x1080". Video only; audio rows stay blank.
	QString fps;		///< "23.976", "25". Video only; audio rows stay blank.
	QString bitDepth;	///< "10-bit", "24-bit".
	int sampleRate = 0; ///< Audio only.
	int channels = 0;	///< Audio only.
	/// Frames at the clip's edit rate — video and audio alike (the Avid-bin
	/// timecode model). 0 = unknown.
	qint64 durationFrames = 0;
	/// Nominal timecode base for duration rendering (24, 25, 30, 60...).
	/// Parser-derived; 0 = unknown (durationDisplay falls back to the fps
	/// display string, and shows blank when neither is available).
	int timecodeBase = 0;
	/// Drop-frame material (29.97/59.94 families). Affects duration
	/// RENDERING only — the frame count itself never changes.
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

	QString filePath;
	QString fileName;
	QString volumeName;
	QString volumePath;
	/// Managed media folder name: an MXF numbered/workstation folder,
	/// Quarantined Files, the OMFI root, or a shared OMF workstation folder.
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

	/// Scan decisions carried between database lookup and the header pass.
	/// A usable database alone does not establish that its metadata is current.
	bool needsHeaderRead = false;
	bool databaseMetadataCurrent = false;

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
	enum class Type : int
	{
		Unknown = -1,
		Media = 0,
		Precompute = 1
	};
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
			return QStringLiteral("\u2014");
		}
		return QStringLiteral("\u2014");
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

	/// Audio sample rate shared by the table and CSV; unknown rates stay blank.
	QString sampleRateDisplay() const
	{
		return sampleRate > 0 ? QStringLiteral("%1 kHz").arg(sampleRate / 1000.0, 0, 'g', 10) : QString();
	}

	/// Timecode base used for duration rendering AND sorting: the
	/// parser-derived base, or one derived from the fps display string
	/// (demo data, MDB-only rows). 0 = unknown. A sub-1 rate stays
	/// unknown — rounded to 0 it would integer-divide-by-zero (SIGFPE)
	/// in the timecode arithmetic; no sane video runs under 1 fps.
	/// Bounds mirror the parser's clamp (mxfparser.cpp): a garbage rate
	/// >= 1000 the parser refused must not be resurrected from the
	/// display string into a nonsense duration.
	int effectiveTimecodeBase() const
	{
		if (timecodeBase > 0)
			return timecodeBase;
		const double rate = fps.toDouble();
		return (rate >= 1.0 && rate < 1000.0) ? static_cast<int>(std::round(rate)) : 0;
	}

	/// Timecode duration — HH:MM:SS:FF at the clip's edit rate, matching
	/// what the Avid bin shows, for audio as much as video. Never wall
	/// clock. Drop-frame material counts SMPTE drop-frame style and renders
	/// with Avid's semicolon separators. Empty when the frame count or the
	/// rate is unknown — an unknown is never coerced into a guess.
	QString durationDisplay() const
	{
		if (durationFrames <= 0)
			return {};
		const int base = effectiveTimecodeBase();
		if (base < 1)
			return {};

		qint64 minutes;
		int secs, frames;
		QChar sep(':');
		if (dropFrame && (base == 30 || base == 60))
		{
			// SMPTE drop-frame: 2 frame NUMBERS per minute are skipped (4
			// at base 60) except every tenth minute. Only the rendering
			// changes; the stored frame count is untouched.
			sep = QLatin1Char(';');
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
		// Sequential markers, sep filled in order: reusing one %N marker for
		// every separator would merge with neighbouring substituted digits
		// ("%5" + "30" parses as marker %53).
		return QStringLiteral("%1%2%3%4%5%6%7")
			.arg(minutes / 60, 2, 10, QChar('0'))
			.arg(sep)
			.arg(minutes % 60, 2, 10, QChar('0'))
			.arg(sep)
			.arg(secs, 2, 10, QChar('0'))
			.arg(sep)
			.arg(frames, 2, 10, QChar('0'));
	}
};

Q_DECLARE_METATYPE(MediaFile)
