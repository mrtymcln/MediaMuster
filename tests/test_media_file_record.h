#pragma once

#include "media/media_file_record.h"

// UI fixtures contain Bella's already selected values. They neither invoke a
// reader nor manufacture source evidence for presentation-only tests.
// MARK: - Media row fixtures

namespace TestMediaFile
{
	inline BellaRowValues &values(MediaFileRecord &file)
	{
		if (!file.bellaValues)
			file.bellaValues.emplace();
		return *file.bellaValues;
	}

	inline void setFrameRate(MediaFileRecord &file, quint64 numerator, quint64 denominator = 1)
	{
		values(file).frameRateValue = numerator && denominator
										  ? std::optional<Bella::TimingRate>{{numerator, denominator}}
										  : std::nullopt;
	}

	inline void setSampleRate(MediaFileRecord &file, quint64 rate)
	{
		values(file).sampleRateValue = rate
										   ? std::optional<Bella::TimingRate>{{rate, 1}}
										   : std::nullopt;
	}

	inline void setBitDepth(MediaFileRecord &file, std::optional<quint32> bits)
	{
		values(file).bitDepth.bits = bits;
		values(file).bitDepth.representation = Bella::MatchDepthRepresentation::Integer;
	}

	inline void setDuration(MediaFileRecord &file, qint64 count, quint16 base = 25, bool drop = false)
	{
		auto &row = values(file);
		const Bella::TimingRate rate = drop ? Bella::TimingRate{quint64(base) * 1000, 1001}
											: Bella::TimingRate{base, 1};
		row.timing.duration = Bella::makeExactDuration(count, rate);
		row.timing.display = base ? std::optional<Bella::TimecodeInputs>{{rate, base,
																		  drop ? Bella::TimecodeMode::DropFrame : Bella::TimecodeMode::NonDropFrame}}
								  : std::nullopt;
		row.durationTimecode = row.timing.duration && row.timing.display
								   ? Bella::durationTimecode(*row.timing.duration, rate, base, row.timing.display->mode).value_or(std::tuple<qint64, int, int, int>{-1, 0, 0, 0})
								   : std::tuple<qint64, int, int, int>{-1, 0, 0, 0};
	}
} // namespace TestMediaFile
