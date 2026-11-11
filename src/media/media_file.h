#pragma once

#include "scanner/matching.h"
#include "media_evidence.h"

#include <QString>
#include <QDateTime>
#include <QMetaType>
#include <optional>
#include <tuple>

// MARK: - Precompute category

// App-facing categories; Bella supplies the classification.
namespace AvidPrecompute
{
	enum class Category
	{
		Unknown,
		RenderedEffects,
		TitlesAndMatteKeys
	};
}

// MARK: - Bella row values

struct BellaRowValues
{
	std::optional<Bella::MatchDimensions> resolution;
	std::optional<Bella::MatchFrameRate> frameRate;
	std::optional<Bella::MatchAudioFrequency> sampleRate;
	Bella::MatchBitDepth bitDepth;
	Bella::FileTiming timing;
	// Exact interpreted rates and timecode components prepared by the Bella
	// adapter. The common table and exporter need no Bella runtime linkage.
	std::optional<Bella::TimingRate> frameRateValue;
	std::optional<Bella::TimingRate> sampleRateValue;
	std::tuple<qint64, int, int, int> durationTimecode{-1, 0, 0, 0};
};

// MARK: - MediaFile

/// One physical file in the scan inventory, identified by its path.
/// FileMobId and MasterMobId connect it to Avid records, bins and relatives;
/// either ID may be unknown. Display helpers are shared with the table and CSV.
struct MediaFile
{
	// MARK: Identity
	KelpieId kelpieId = 0; ///< Physical row identity within this scan session only.
	MediaScanStamp scanStamp;
	/// Frozen operation checks; absence means they were never captured.
	std::optional<OperationScanReceipt> operationScanReceipt;
	// Full IDs use PMR/MDB field order. The scanner converts MXF header IDs
	// before storing them; database, OMF and AVB readers already use this order.
	QString fileMobId;	 ///< FileMobId recovered from databases or media metadata.
	QString masterMobId; ///< Selected MasterMobId, shared by related essence files.

	// MARK: MDB and PMR metadata

	/// Associated-master name selected by the active scanner's source rules.
	/// Source-package names do not establish a master clip name or a recorded source path.
	/// Unknown clip names stay blank rather than falling back to filenames.
	QString clipName;

	/// Selected project name under the active scanner's source rules. Empty means unknown,
	/// independently of whether the folder's PMR currently lists this file.
	QString project;
	QString originalBin; ///< Recorded original-bin association from the selected master.

	// MARK: MXF or MDB technical metadata

	QString compression; ///< "DNx HQX [DNxHD 175x]", "PCM", etc.
	std::optional<BellaRowValues> bellaValues;

	// MARK: Precompute detail (table, CSV and filtering when enabled)

	/// Precompute details derived from the clip name and effect catalogue.
	/// Unknown effects keep their token.
	/// Empty for ordinary media.
	QString effect;
	QString effectCategory;
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
	/// Filesystem modification time, retained for physical source-change and
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
			return {};
		}
		return {};
	}

	/// Filter choices retain a selectable Unknown group when the table cell is blank.
	QString precomputeCategoryFilterLabel() const
	{
		const auto label = precomputeCategoryDisplay();
		return type == Type::Precompute && label.isEmpty() ? QStringLiteral("unknown") : label;
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
			return QStringLiteral("unknown");
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
			return QStringLiteral("unknown");
		}
		return {};
	}

	/// Decimal MB for table/CSV display; sorting uses the exact byte count.
	QString sizeMBDisplay() const
	{
		return sizeBytes < 0
				   ? QString{}
				   : QString::number(sizeBytes / 1'000'000.0, 'f', 1);
	}

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

	QString resolutionDisplay() const
	{
		if (!bellaValues)
			return {};
		const auto &dimensions = bellaValues->resolution;
		return dimensions ? QStringLiteral("%1x%2").arg(dimensions->width).arg(dimensions->height) : QString{};
	}

	QString frameRateDisplay() const
	{
		if (!bellaValues)
			return {};
		const auto &rate = bellaValues->frameRateValue;
		if (!rate)
			return {};
		auto label = QString::number(double(rate->numerator) / rate->denominator, 'f', 3);
		while (label.endsWith(QLatin1Char('0')))
			label.chop(1);
		if (label.endsWith(QLatin1Char('.')))
			label.chop(1);
		return label;
	}

	std::optional<quint32> bellaDepthBits() const
	{
		if (!bellaValues)
			return std::nullopt;
		const auto &depth = bellaValues->bitDepth;
		switch (depth.representation)
		{
		case Bella::MatchDepthRepresentation::Fixed10_6:
			return 10;
		case Bella::MatchDepthRepresentation::Fixed12_4:
			return 12;
		default:
			return depth.bits;
		}
	}

	QString bitDepthDisplay() const
	{
		const auto bits = bellaDepthBits();
		return bits ? QStringLiteral("%1-bit").arg(*bits) : QString{};
	}

	/// Numeric audio rate for presentation; calculations use the original fraction.
	double sampleRateHz() const
	{
		if (bellaValues)
		{
			const auto &rate = bellaValues->sampleRateValue;
			return rate ? double(rate->numerator) / rate->denominator : 0.0;
		}
		return 0.0;
	}

	/// Audio sample rate shared by the table and CSV; unknown rates stay blank.
	QString sampleRateDisplay() const
	{
		const double rate = sampleRateHz();
		return rate > 0 ? QStringLiteral("%1 kHz").arg(rate / 1000.0, 0, 'g', 10) : QString();
	}

	/// Numeric HH, MM, SS, FF for display and sorting. Hour -1 means unknown.
	std::tuple<qint64, int, int, int> durationTimecode() const
	{
		return bellaValues ? bellaValues->durationTimecode : std::tuple<qint64, int, int, int>{-1, 0, 0, 0};
	}

	/// Duration as timecode; drop-frame uses semicolons. Unknown stays blank.
	QString durationDisplay() const
	{
		const auto [hours, minutes, secs, frames] = durationTimecode();
		if (hours < 0)
			return {};
		const bool df = bellaValues && bellaValues->timing.display &&
						bellaValues->timing.display->mode == Bella::TimecodeMode::DropFrame;
		const QChar sep = df ? QLatin1Char(';') : QLatin1Char(':');
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
