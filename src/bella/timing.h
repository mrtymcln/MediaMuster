#pragma once

#include <QString>
#include <QtGlobal>
#include <optional>

namespace Bella
{

	// Exact unsigned64 fraction, including MDB/OMF AIFF frequency recovery.
	// Units per second. The readers retain the original source words separately.
	struct TimingRate
	{
		quint64 numerator = 0;
		quint64 denominator = 0;
	};

	struct ExactDuration
	{
		qint64 count = 0;
		TimingRate rate;
	};

	enum class TimecodeMode
	{
		NonDropFrame,
		DropFrame
	};

	struct TimecodeInputs
	{
		TimingRate displayRate;
		quint16 nominalBase = 0;
		TimecodeMode mode = TimecodeMode::NonDropFrame;
	};

	struct FileTiming
	{
		std::optional<ExactDuration> duration;
		std::optional<TimecodeInputs> display;
	};

	struct MdbFileRecord;
	struct MxfFileInformation;
	struct OmfFileInformation;
	struct OmfNativeAudioInformation;

	// One already-selected eligible reader value. No source lookup or new reads.
	// The caller qualifies the nominal base; a missing base withholds display,
	// independently of exact Duration. Native audio supplies exact Duration only.
	[[nodiscard]] FileTiming timingFor(const MdbFileRecord &, std::optional<quint16> nominalBase);
	[[nodiscard]] FileTiming timingFor(const MxfFileInformation &, std::optional<quint16> nominalBase);
	[[nodiscard]] FileTiming timingFor(const OmfFileInformation &, std::optional<quint16> nominalBase);
	[[nodiscard]] FileTiming timingFor(const OmfNativeAudioInformation &);

	// Only for a qualified Avid ExactEditRate video/editing clock, never an audio
	// sampling frequency or a generic Rational. Other positive fractions stay literal.
	[[nodiscard]] std::optional<TimingRate> interpretAvidEditRate(qint32 numerator, qint32 denominator) noexcept;

	// Keeps the original count, including zero. Invalid inputs return no value.
	[[nodiscard]] std::optional<ExactDuration> makeExactDuration(qint64 count, TimingRate calculationRate) noexcept;

	// Converts exact duration to display frames, rounding once, positive halves up.
	// Zero is a value; invalid inputs or an unrepresentable result return no value.
	[[nodiscard]] std::optional<qint64> displayFrameCount(const ExactDuration &duration, TimingRate displayRate) noexcept;

	// The caller has already established the applicable clock, nominal base and mode.
	// Returns an empty string when they cannot form a supported label. Hours do not wrap.
	[[nodiscard]] QString formatDuration(const ExactDuration &duration, TimingRate displayRate, quint16 nominalBase,
										 TimecodeMode mode);

} // namespace Bella
