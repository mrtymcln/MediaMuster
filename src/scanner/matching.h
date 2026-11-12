#pragma once

#include "mdb_parser.h"
#include "mxf_parser.h"
#include "media_labels.h"
#include "omf_parser.h"
#include "pmr_parser.h"
#include "timing.h"

#include <QHash>
#include <QSet>
#include <QVector>
#include <array>
#include <functional>
#include <optional>
#include <variant>

namespace Bella
{

	// MARK: - Metadata categories and values

	enum class MatchKind
	{
		Unknown,
		Audio,
		Video
	};
	enum class MatchType
	{
		Unknown,
		Media,
		Precompute
	};
	enum class MatchPrecomputeCategory
	{
		Unknown,
		RenderedEffects,
		TitlesAndMatteKeys
	};
	enum class MatchRateMeaning
	{
		Literal,
		AvidExactEditRate
	};

	struct MatchRateWords
	{
		qint32 numerator = 0;
		qint32 denominator = 0;
	};

	struct MatchFrameRate
	{
		MatchRateWords words;
		MatchRateMeaning meaning = MatchRateMeaning::Literal;
	};

	using MatchAudioFrequency = std::variant<MatchRateWords, quint32, std::array<quint8, 10>>;

	struct MatchDimensions
	{
		qint64 width = 0;
		qint64 height = 0;
	};

	enum class MatchDepthRepresentation
	{
		Unknown,
		Integer,
		HalfFloat,
		Float,
		S2_14Fixed,
		Fixed10_6,
		Fixed12_4
	};

	struct MatchBitDepth
	{
		std::optional<quint32> bits;
		std::optional<quint32> storageBits;
		MatchDepthRepresentation representation = MatchDepthRepresentation::Unknown;
	};

	// MARK: - Matched metadata and result

	struct MatchedMetadata
	{
		std::optional<MobId> fileMobId;
		std::optional<MobId> masterMobId;
		QString project;
		QString clipName;
		QString originalBin;
		MatchKind kind = MatchKind::Unknown;
		MatchType type = MatchType::Unknown;
		MatchPrecomputeCategory precomputeCategory = MatchPrecomputeCategory::Unknown;
		std::optional<MatchDimensions> resolution;
		std::optional<MatchFrameRate> frameRate;
		std::optional<TimingRate> exactFrameRate;
		std::optional<MatchAudioFrequency> sampleRate;
		MatchBitDepth bitDepth;
		FileTiming timing;
		CompressionNames compression;
		EffectNames effects;
	};

	enum class LocalReference
	{
		Unresolved,
		Listed,
		NoReference,
		NoDatabase
	};

	struct MatchResult
	{
		MatchedMetadata metadata;
		LocalReference localReference = LocalReference::Unresolved;
		bool needsHeader = false;
	};

	// MARK: - Matching inputs and indexes

	using HeaderInput = std::variant<std::monostate, std::reference_wrapper<const MxfReport>,
									 std::reference_wrapper<const OmfReport>>;

	class MatchContext
	{
	public:
		const QVector<PmrReport> &pmrSources() const noexcept;
		const QVector<MdbReport> &mdbSources() const noexcept;

	private:
		struct Position
		{
			qsizetype source;
			qsizetype record;
		};
		struct FolderSources
		{
			QVector<qsizetype> pmrs;
			QVector<qsizetype> mdbs;
		};
		QVector<PmrReport> m_pmrs;
		QVector<MdbReport> m_mdbs;
		QSet<QString> m_completedFolders;
		QHash<QString, FolderSources> m_folders;
		QHash<QString, QHash<QString, QVector<Position>>> m_pmrNames;
		QHash<MobId, QVector<Position>> m_files;
		QHash<MobId, QVector<Position>> m_masters;

		friend MatchContext prepareMatchContext(QVector<PmrReport>, QVector<MdbReport>, QSet<QString>);
		friend MatchResult matchFile(const MatchContext &, const QString &, bool, HeaderInput, bool);
	};

	// MARK: - Matching API

	// All eligible database work is settled before handing over these collections.
	// A discovered, uncalled source has its path and otherwise-default NotRead report.
	[[nodiscard]] MatchContext prepareMatchContext(QVector<PmrReport> pmrSources, QVector<MdbReport> mdbSources,
												   QSet<QString> completedFolders);

	// No file I/O. The caller qualifies the physical entry and freezes optional demand.
	// A supplied header reply belongs to physicalPath and remains alive for this call.
	// All returned values own their data, independently of the context/header lifetime.
	[[nodiscard]] MatchResult matchFile(const MatchContext &context, const QString &physicalPath,
										bool physicalFactsEligible, HeaderInput header, bool includePrecomputeDetails);
}
