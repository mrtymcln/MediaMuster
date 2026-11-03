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
