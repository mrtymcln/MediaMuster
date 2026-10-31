#pragma once

#include "mobid.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <array>
#include <atomic>
#include <optional>
#include <variant>

namespace Bella
{
	enum class MdbReadResult
	{
		NotRead,
		Completed,
		Malformed,
		Unsupported,
		IoError,
		Changed,
		Cancelled
	};
	enum class MdbCheckResult
	{
		NotChecked,
		Verified,
		Unverified,
		Changed
	};
	enum class MdbKind
	{
		Unknown,
		Audio,
		Video
	};
	enum class MdbType
	{
		Unknown,
		Media,
		Precompute
	};
	enum class MdbPrecomputeCategory
	{
		Unknown,
		RenderedEffects,
		TitlesAndMatteKeys
	};
	enum class MdbRateEncoding
	{
		ExactEditRate,
		Rational
	};
	enum class MdbRasterOrigin
	{
		Stored,
		Display,
		VerifiedProxy
	};
	enum class MdbProjectSelection
	{
		Unavailable,
		Conflict,
		OwnFile,
		ImmediateSources
	};
	enum class MdbProjectKey
	{
		None,
		Pj,
		ProjName,
		UserPj,
		Mixed
	};
	enum class MdbNumbering
	{
		Unavailable,
		NotRequired,
		MissingNdfFallback,
		RecordedNdf,
		RecordedDf
	};
	enum class MdbDepthRepresentation
	{
		Unknown,
		Integer,
		HalfFloat,
		Float,
		S2_14Fixed,
		Fixed10_6,
		Fixed12_4
	};
	enum class MdbCodingState
	{
		Unavailable,
		Absent,
		Present
	};
	enum class MdbAudioEncoding
	{
		Unknown,
		Pcm,
		IeeeFloat
	};

	struct MdbRateWords
	{
		qint32 numerator = 0;
		qint32 denominator = 0;
	};

	struct MdbRate
	{
		MdbRateWords words;
		MdbRateEncoding encoding = MdbRateEncoding::Rational;
	};

	struct MdbDimensions
	{
		qint64 width = 0;
		qint64 height = 0;
	};

	struct MdbRaster
	{
		MdbDimensions dimensions;
		MdbRasterOrigin origin = MdbRasterOrigin::Stored;
	};

	// The variant identifies the original PCMA, WAVE or AIFF representation.
	using MdbAudioSourceValue = std::variant<MdbRateWords, quint32, std::array<quint8, 10>>;

	struct MdbComponentArray
	{
		std::array<quint8, 8> values{};
		quint8 count = 0;
	};

	struct MdbBitDepth
	{
		std::optional<quint32> bits{};
		std::optional<quint32> storageBits{};
		MdbDepthRepresentation representation = MdbDepthRepresentation::Unknown;
		std::optional<qint32> rawComponentWidth{};
		std::optional<MdbComponentArray> pixelLayout{};
		std::optional<MdbComponentArray> pixelStructure{};
	};

	struct MdbCompression
	{
		MdbCodingState codingState = MdbCodingState::Unavailable;
		std::array<quint8, 16> codingLabel{}; // Meaningful only for Present.
		std::optional<std::array<quint8, 4>> legacyCompression{};
		std::optional<qint64> resolutionId{};
		std::optional<MdbDimensions> namingGeometry{};
		std::optional<quint16> frameLayout{};
		std::optional<qint32> horizontalSubsampling{};
		std::optional<qint32> verticalSubsampling{};
		MdbAudioEncoding audioEncoding = MdbAudioEncoding::Unknown;
	};

	struct MdbFileRecord
	{
		MobId fileMobId;
		QString project{};
		MdbProjectSelection projectSelection = MdbProjectSelection::Unavailable;
		MdbProjectKey projectKey = MdbProjectKey::None;
		MdbKind kind = MdbKind::Unknown;
		std::optional<std::array<quint8, 4>> descriptorClass{};
		bool ownDescriptorQualified = false;
		std::optional<qint64> length{};
		std::optional<MdbRate> sampleRate{};
		std::optional<MdbRate> audioEditRate{};
		MdbNumbering numbering = MdbNumbering::Unavailable;
		std::optional<MdbAudioSourceValue> audioFrequency{};
		std::optional<MdbRaster> resolution{};
		MdbBitDepth bitDepth{};
		MdbCompression compression{};
		QVector<qsizetype> directMasterRecords{}; // Positions in this report's masters.
		bool masterLinksComplete = false;
	};

	struct MdbMasterRecord
	{
		MobId masterMobId;
		QString clipName{};
		QString originalBin{};
		MdbType type = MdbType::Unknown;
		MdbPrecomputeCategory precomputeCategory = MdbPrecomputeCategory::Unknown;
	};

	struct MdbCatalogue
	{
		QVector<MdbFileRecord> files;
		QVector<MdbMasterRecord> masters;
		bool fileReferencesComplete = false;
	};

	struct MdbReport
	{
		QString path;
		MdbReadResult reading = MdbReadResult::NotRead;
		MdbCheckResult sizeDateCheck = MdbCheckResult::NotChecked;
		MdbCatalogue catalogue;
		// Reason text; combine with path when displaying/logging each message.
		QStringList problemNotes;
	};

	/// One owned capture, two parsing stages, then owned values only.
	/// The caller keeps the shared stop flag alive until this call returns.
	[[nodiscard]] MdbReport parseMdb(const QString &path, const std::atomic_bool &stopRequested,
									 bool includePrecomputeDetails);
}
