#pragma once

#include "mobid.h"

#include <QString>
#include <QStringList>
#include <array>
#include <atomic>
#include <optional>
#include <variant>

class QFile;

namespace Bella
{
	enum class OmfReadResult
	{
		NotRead,
		NotPresent,
		Completed,
		Incomplete,
		Malformed,
		Unsupported,
		IoError,
		Cancelled
	};
	enum class OmfCheckResult
	{
		NotChecked,
		Verified,
		Unverified,
		Changed
	};
	enum class OmfKind
	{
		Unknown,
		Audio,
		Video
	};
	enum class OmfType
	{
		Unknown,
		Media,
		Precompute
	};
	enum class OmfPrecomputeCategory
	{
		Unknown,
		RenderedEffects,
		TitlesAndMatteKeys
	};
	enum class OmfAudioEncoding
	{
		Unknown,
		Pcm,
		IeeeFloat
	};
	enum class OmfProjectSelection
	{
		Unavailable,
		Conflict,
		OwnFile,
		ImmediateSources
	};
	enum class OmfProjectKey
	{
		None,
		Pj,
		ProjName,
		UserPj,
		Mixed
	};
	enum class OmfRateEncoding
	{
		ExactEditRate,
		Rational
	};
	enum class OmfDurationClockSource
	{
		Unavailable,
		DescriptorSampleRate,
		SummaryFrequency
	};
	enum class OmfNumbering
	{
		Unavailable,
		NotRequired,
		MissingNdfFallback,
		RecordedNdf,
		RecordedDf
	};
	enum class OmfRasterOrigin
	{
		Stored,
		Display,
		VerifiedProxy
	};
	enum class OmfDepthRepresentation
	{
		Unknown,
		Integer,
		HalfFloat,
		Float,
		S2_14Fixed,
		Fixed10_6,
		Fixed12_4
	};
	enum class OmfCodingState
	{
		Unavailable,
		Absent,
		Present
	};

	struct OmfNativeRate
	{
		qint32 numerator = 0;
		qint32 denominator = 0;
	};

	struct OmfNativeAudioInformation
	{
		OmfKind kind = OmfKind::Unknown;
		std::optional<qint64> sampleFrameCount{};
		std::optional<OmfNativeRate> samplingRate{};
		std::optional<quint16> bitDepth{};
		OmfAudioEncoding encoding = OmfAudioEncoding::Unknown;
	};

	struct OmfRateWords
	{
		qint32 numerator = 0;
		qint32 denominator = 0;
	};

	struct OmfRate
	{
		OmfRateWords words;
		OmfRateEncoding encoding = OmfRateEncoding::Rational;
	};

	using OmfAudioSourceValue = std::variant<OmfRateWords, quint32, std::array<quint8, 10>>;

	struct OmfDimensions
	{
		qint64 width = 0;
		qint64 height = 0;
	};

	struct OmfRaster
	{
		OmfDimensions dimensions;
		OmfRasterOrigin origin = OmfRasterOrigin::Stored;
	};

	struct OmfBitDepth
	{
		std::optional<quint32> bits{};
		OmfDepthRepresentation representation = OmfDepthRepresentation::Unknown;
		std::optional<qint32> rawComponentWidth{};
	};

	struct OmfCompression
	{
		OmfCodingState codingState = OmfCodingState::Unavailable;
		std::array<quint8, 16> codingLabel{}; // Meaningful only for Present.
		std::optional<std::array<quint8, 4>> legacyCompression{};
		std::optional<qint64> resolutionId{};
		std::optional<OmfDimensions> namingGeometry{};
		std::optional<quint16> frameLayout{};
		std::optional<qint32> horizontalSubsampling{};
		std::optional<qint32> verticalSubsampling{};
		OmfAudioEncoding audioEncoding = OmfAudioEncoding::Unknown;
		std::optional<quint16> alphaDepth{};
	};

	struct OmfFileInformation
	{
		std::optional<MobId> fileMobId{};
		QString project{};
		OmfProjectSelection projectSelection = OmfProjectSelection::Unavailable;
		OmfProjectKey projectKey = OmfProjectKey::None;
		OmfKind kind = OmfKind::Unknown;
		std::optional<std::array<quint8, 4>> descriptorClass{};
		std::optional<qint64> length{};
		std::optional<OmfRate> sampleRate{};
		std::optional<OmfRate> audioEditRate{};
		OmfDurationClockSource durationClockSource = OmfDurationClockSource::Unavailable;
		OmfNumbering numbering = OmfNumbering::Unavailable;
		std::optional<OmfAudioSourceValue> audioFrequency{};
		std::optional<OmfRaster> resolution{};
		OmfBitDepth bitDepth;
		OmfCompression compression;
	};

	struct OmfAssociatedClipInformation
	{
		std::optional<MobId> masterMobId{};
		QString clipName{};
		QString originalBin{};
		OmfType type = OmfType::Unknown;
		OmfPrecomputeCategory precomputeCategory = OmfPrecomputeCategory::Unknown;
	};

	struct OmfReport
	{
		QString path;
		OmfReadResult nativeAudioReading = OmfReadResult::NotRead;
		OmfReadResult omfReading = OmfReadResult::NotRead;
		OmfCheckResult sizeDateCheck = OmfCheckResult::NotChecked;
		OmfNativeAudioInformation nativeAudioInformation;
		OmfFileInformation fileInformation;
		OmfAssociatedClipInformation associatedClipInformation;
		QStringList problemNotes;
	};

	/// Owns one open physical file; returns only independently qualified owned
	/// facts.
	[[nodiscard]] OmfReport parseOmf(const QString &path, const std::atomic_bool &stopRequested,
									 bool includePrecomputeDetails);

	/// Checks identity in the caller's already-open binary file, without reopening
	/// or closing it. Uses the same supported layout as the path reader; verifies
	/// every supplied master independently of the table's selected clip.
	[[nodiscard]] bool omfIdentityMatches(QFile &source, const QString &expectedFileId,
										  const QStringList &expectedMasterIds);
} // namespace Bella
