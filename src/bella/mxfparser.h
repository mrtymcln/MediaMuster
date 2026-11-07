#pragma once

#include "mobid.h"

#include <QString>
#include <QStringList>
#include <array>
#include <atomic>
#include <optional>

class QFile;

namespace Bella
{
	enum class MxfReadResult
	{
		NotRead,
		Completed,
		Malformed,
		Unsupported,
		IoError,
		Changed,
		Cancelled
	};

	enum class MxfCheckResult
	{
		NotChecked,
		Verified,
		Unverified,
		Changed
	};

	enum class MxfKind
	{
		Unknown,
		Audio,
		Video
	};

	enum class MxfType
	{
		Unknown,
		Media,
		Precompute
	};

	enum class MxfPrecomputeCategory
	{
		Unknown,
		RenderedEffects,
		TitlesAndMatteKeys
	};

	enum class MxfProjectSelection
	{
		Unavailable,
		Conflict,
		Preface,
		OwnFile
	};

	enum class MxfProjectKey
	{
		None,
		Pj,
		ProjName,
		UserPj
	};

	enum class MxfNumbering
	{
		Unavailable,
		NotRequired,
		MissingNdfFallback,
		RecordedNdf,
		RecordedDf
	};

	enum class MxfRasterOrigin
	{
		Stored,
		Sampled,
		Display,
		VerifiedProxy
	};

	enum class MxfDepthRepresentation
	{
		Unknown,
		Integer,
		HalfFloat,
		Float,
		S2_14Fixed,
		Fixed10_6,
		Fixed12_4
	};

	enum class MxfCodingState
	{
		Unavailable,
		Absent,
		Present
	};

	enum class MxfNamingFallback
	{
		None,
		Pcm,
		UncompressedAlpha8
	};

	struct MxfRate
	{
		qint32 numerator = 0;
		qint32 denominator = 0;
	};

	struct MxfDimensions
	{
		qint64 width = 0;
		qint64 height = 0;
	};

	struct MxfRaster
	{
		MxfDimensions dimensions;
		MxfRasterOrigin origin = MxfRasterOrigin::Stored;
	};

	struct MxfBitDepth
	{
		std::optional<quint32> bits{};
		MxfDepthRepresentation representation = MxfDepthRepresentation::Unknown;
		std::optional<quint32> rawComponentDepth{};
		std::optional<std::array<quint8, 16>> pixelLayout{};
	};

	struct MxfCompression
	{
		MxfCodingState codingState = MxfCodingState::Unavailable;
		std::array<quint8, 16> codingLabel{}; // Meaningful only for Present.
		std::optional<MxfDimensions> namingGeometry{};
		std::optional<quint8> frameLayout{};
		std::optional<quint32> horizontalSubsampling{};
		std::optional<quint32> verticalSubsampling{};
		MxfNamingFallback namingFallback = MxfNamingFallback::None;
	};

	struct MxfFileInformation
	{
		std::optional<MobId> fileMobId{};
		QString project{};
		MxfProjectSelection projectSelection = MxfProjectSelection::Unavailable;
		MxfProjectKey projectKey = MxfProjectKey::None;
		MxfKind kind = MxfKind::Unknown;
		std::optional<std::array<quint8, 16>> descriptorClass{};
		std::optional<qint64> containerDuration{};
		std::optional<MxfRate> sampleRate{};
		bool frameRateQualified = false;
		std::optional<MxfRate> projectEditRate{};
		MxfNumbering numbering = MxfNumbering::Unavailable;
		std::optional<MxfRate> audioSamplingRate{};
		std::optional<MxfRaster> resolution{};
		MxfBitDepth bitDepth{};
		MxfCompression compression{};
	};

	struct MxfAssociatedClipInformation
	{
		std::optional<MobId> masterMobId{};
		QString clipName{};
		MxfType type = MxfType::Unknown;
		MxfPrecomputeCategory precomputeCategory = MxfPrecomputeCategory::Unknown;
	};

	struct MxfReport
	{
		QString path;
		MxfReadResult reading = MxfReadResult::NotRead;
		MxfCheckResult sizeDateCheck = MxfCheckResult::NotChecked;
		MxfFileInformation fileInformation;
		MxfAssociatedClipInformation associatedClipInformation;
		// Reason text; combine with path when displaying/logging each message.
		QStringList problemNotes;
	};

	/// Reads the supported initial metadata header and returns only owned facts.
	/// The caller keeps the shared stop flag alive for this synchronous call.
	[[nodiscard]] MxfReport parseMxf(const QString &path, const std::atomic_bool &stopRequested,
									 bool includePrecomputeDetails);

	/// Checks identity in the caller's already-open binary file, without reopening
	/// or closing it. Uses the same supported layout as the path reader; verifies
	/// every supplied master independently of the table's selected clip.
	[[nodiscard]] bool mxfIdentityMatches(QFile &source, const QString &expectedFileId,
										  const QStringList &expectedMasterIds);
} // namespace Bella
