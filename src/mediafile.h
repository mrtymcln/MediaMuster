#pragma once

#include "avidprecompute.h"

#include <QString>
#include <QDateTime>
#include <QVector>
#include <QMetaType>
#include <cmath>

// Plain data types shared across the app. No logic beyond a
// couple of derived-display helpers; everything else operates
// on these as inputs.

// MARK: - MediaFile

/// One row of the main table. `filePath` is the primary identity;
/// `mobId` and `masterMobId` are secondary identities used by the
/// bin filter to match against .avb references. Both can legitimately
/// be empty for unreferenced files.
struct MediaFile
{
	// MARK: Identity

	QString mobId;		 ///< Avid MOB ID for this essence file (from MDB/PMR).
	QString masterMobId; ///< Master MOB — the master clip's MOB (AAF MasterMob);
						 ///< V01/A01/A02 relatives share this.

	// MARK: MDB and PMR metadata

	QString clipName;

	/// Where `clipName` came from, ranked. The scanner only ever replaces a
	/// name with one from a STRICTLY better source, which is what makes the
	/// ladder hold no matter what order the passes run in: pass 1 reads the
	/// MDB, pass 2 the MXF header and then the MDB again after the UMID
	/// re-join, so a rung can arrive before or after a better one.
	///
	///   MaterialPackage — the master-clip name Avid itself displays. Exact,
	///                     per-file, and present in 1212 of 1212 real files
	///                     surveyed, so this is the normal answer.
	///   Mdb             — the record's own name in msmMMOB.mdb. Also exact
	///                     (360/360 against MaterialPackage names on a real
	///                     folder), and the only name available when the MXF
	///                     header can't be read at all.
	///   Avb             — a matching master clip in the loaded bins. Used
	///                     only when the scanner has no name and those bins
	///                     agree; removed when that supporting bin is unloaded.
	///   None            — genuinely unknown. The Clip Name cell stays blank;
	///                     the filename is NOT substituted (user ruling
	///                     2026-08-14). See clipNameDisplay().
	///
	/// An MXF SourcePackage name is deliberately NOT a rung. It is the name of
	/// what the media came FROM — the imported file or the tape — not a name
	/// Avid gave the clip: measured across those same 1212 files it was the
	/// source filename ("Avid DNx SQ.mov") on 1191 of them. MediaMuster
	/// already carries that datum in `sourceFileName`, and letting it into
	/// this column would put a filename back in the Clip Name cell, which is
	/// the exact thing the ruling removed.
	enum class ClipNameSource
	{
		None = 0,
		Avb = 1,
		Mdb = 2,
		MaterialPackage = 3,
	};
	ClipNameSource clipNameSource = ClipNameSource::None;

	/// The project the media was created in. Avid writes it into the PMR entry
	/// AND the file's own header at creation, and reads it back from the file
	/// when it rebuilds a folder's databases — so the scanner takes the PMR's
	/// copy, else the header's, and leaves this EMPTY when neither names one
	/// (see projectDisplay / hasNoProject). Independent of dbStatus: a file
	/// can be unlisted in this folder's databases and still name its project.
	QString project;
	QString originalBin;			 ///< The recorded import-time _ORG_BIN, from media metadata or a bin reference.
	bool originalBinFromAvb = false; ///< Loaded-bin fallback; cleared when its supporting bins change.

	// MARK: MXF or MDB technical metadata

	QString codec;		///< "Avid DNx SQ (DNxHD 145)", "PCM Audio", etc.
	QString codecHex;	///< Raw hex of the MXF essence container label.
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
	/// as underscores); and the +N render instance. Empty / 0 on every Media row.
	QString effect;
	QString effectCategory;
	QString effectSequence;
	int effectInstance = 0;
	using PrecomputeCategory = AvidPrecompute::Category;
	PrecomputeCategory precomputeCategory = PrecomputeCategory::Unknown;

	// MARK: Filesystem

	QString filePath;
	QString fileName;
	QString extension;
	QString volumeName;
	QString volumePath;
	/// The numbered subfolder under Avid MediaFiles/MXF. OMF-era: the flat
	/// legacy root has no numbered folders, so its rows carry the root's own
	/// name, "OMFI MediaFiles" — which the rebalancer's folder-name rule
	/// rejects, keeping OMF media out of its scope.
	QString mxfFolder;
	/// OMF-era: legacy media — read by OmfParser, preserved by the copy
	/// engine to "OMFI MediaFiles". Decided once, in the scanner
	/// (isOmfEraRow): an .omf extension, Avid's OMFI root, or a folder
	/// whose own databases carry 12-byte omfi:UIDs. The folder's name is
	/// not the rule — Avid's bundled slate folder is "Avid_MediaFiles",
	/// and an archive added by hand can be called anything.
	bool omfEra = false;
	qint64 sizeBytes = 0;
	/// Filesystem creation (birth) time. Invalid when the file system
	/// doesn't record one — displayed blank, never substituted.
	QDateTime created;
	/// Filesystem modification time — what Finder shows as "Date Modified".
	/// The "Date Modified" column; also what the scanner's staleness check
	/// compares against the PMR's recorded mtime (see PmrEntry).
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

	/// Whether this folder's Avid databases (msmFMID.pmr / msmMMOB.mdb) list
	/// the file — a folder-level fact stamped per row. One enum value, so a
	/// row is exactly one of these by construction. Kept apart from `project`
	/// on purpose: Avid treats "which project made this" as a property of the
	/// media and "is it indexed here" as the state of this folder's databases
	/// right now, and a file can be unlisted yet still name its project.
	enum class DbStatus : int
	{
		Listed,		 ///< The folder's PMR names the file.
		NoReference, ///< Databases present and readable, but no reference to this
					 ///< file: copied in or created since Avid last indexed the
					 ///< folder, or its records were removed. Avid re-indexes at launch.
		NoDatabase,	 ///< No msmFMID.pmr here — no index to check against: other seats'
					 ///< folders on shared storage, Interplay / MediaCentral, Quarantined
					 ///< Files, a deleted-and-not-yet-rebuilt database, read-only volumes.
		DbUnreadable ///< A database exists but could not be read (corrupt, truncated,
					 ///< or an unsupported older version); Avid rebuilds it at relaunch.
	};
	DbStatus dbStatus = DbStatus::Listed;

	/// The one "No Database" filter tab covers both couldn't-check states;
	/// the tooltip says which.
	bool isNoDatabase() const
	{
		return dbStatus == DbStatus::NoDatabase || dbStatus == DbStatus::DbUnreadable;
	}
	/// Nothing names a project for this file — not the PMR entry, not the
	/// file's own header. Not a database state: an unlisted file usually still
	/// knows its project (the header carries it), so the two are independent.
	bool hasNoProject() const { return project.isEmpty(); }

	/// The file's or its clip's MOB ID is all zeros — Avid never assigned a
	/// real identity, so the media can't be tracked or relinked reliably.
	/// (A UMID is what a MOB ID is; the name keeps the domain word.) Never
	/// seen in 1,155 Avid-written files; catches third-party MXF. MDVx ships
	/// the same filter as "Bad UMID".
	bool isInvalidUmid = false;
	bool isNonPortable = false; ///< Filename has Avid illegal chars.
	bool isQuarantined = false; ///< Lives in Avid's "Quarantined Files" folder. Stamped by
								///< the scanner, which knows the folder; the filter only
								///< reads it (it used to re-guess from a path substring).

	// MARK: Status words

	/// The ONE place that says what each database status is called and why
	/// it happens. The filter tab, the table tooltip, the sidebar, and the CSV
	/// all read this, so they cannot drift. `label` doubles as the CSV value;
	/// the two couldn't-check states share a label (one tab) and differ in `why`.
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

	/// "Size (MB)" column / CSV string — derived from sizeBytes on every
	/// call (decimal MB, 1000-based, matching Format::bytes). A stored
	/// twin field used to sit beside sizeBytes; deriving means the two
	/// can never disagree. Sorting compares sizeBytes directly.
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

	/// "Date Modified" column AND CSV string, same format as createdDisplay.
	QString modifiedDisplay() const
	{
		return modified.isValid() ? modified.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
								  : QString();
	}

	/// "Clip Name" column string. Blank when no clip name is known: the
	/// filename is a fact about the disk, not a name Avid gave the clip, and
	/// substituting it made an unknown look like an answer (user ruling
	/// 2026-08-14 — same promise createdDisplay() already makes). The old
	/// fallback also disagreed with the CSV export, which has always written
	/// the raw field, and it showed the name WITH its extension where the
	/// Stage-1 seed showed it without.
	///
	/// The sort compares this same string, so order and display can never
	/// disagree. Returns a reference to the member — no copy per comparison.
	const QString &clipNameDisplay() const
	{
		return clipName;
	}

	/// "Codec" column string: the resolved codec name, or the raw
	/// essence-label hex when the debug toggle is on and hex exists.
	/// The toggle lives on the table model; callers pass it in.
	const QString &codecDisplay(bool rawHex) const
	{
		return (rawHex && !codecHex.isEmpty()) ? codecHex : codec;
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

// MARK: - VolumeInfo

/// One mounted volume the user might want to scan. Built by
/// VolumeManager from QStorageInfo plus Avid-aware heuristics.
struct VolumeInfo
{
	QString name;
	QString path;
	qint64 totalBytes = 0;
	qint64 usedBytes = 0;
	QString volumeType;		   ///< "Internal", "Network", or "Nexis".
	bool hasAvidMedia = false; ///< True if `<path>/Avid MediaFiles` exists.
};

// MARK: - ProjectSummary

struct ProjectSummary
{
	QString name;			///< projectDisplay() — real name, or "No project".
	bool hasProject = true; ///< false for the one "No project" row.
	int videoCount = 0;
	int audioCount = 0;
	int unknownKindCount = 0;
	qint64 totalBytes = 0;
	QVector<QString> bins;
};

// MARK: - Metatype registration

Q_DECLARE_METATYPE(MediaFile)
Q_DECLARE_METATYPE(VolumeInfo)
