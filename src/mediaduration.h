#pragma once

#include <QtGlobal>
#include <array>
#include <limits>

/// Recorded units per second. Keep the original fraction, including unreduced rates.
struct MediaRate
{
	qint32 numerator = 0;
	qint32 denominator = 0;

	bool valid() const { return numerator > 0 && denominator > 0; }
	double value() const { return valid() ? double(numerator) / denominator : 0.0; }
	bool sameRate(const MediaRate &other) const
	{
		return valid() && other.valid() &&
			   qint64(numerator) * other.denominator == qint64(other.numerator) * denominator;
	}
};

/// One selected duration, never a sum of all files associated with a master.
/// Length differences do not invalidate identity or association.
struct MediaDuration
{
	enum class Source
	{
		Unknown,
		Descriptor,		///< Stored essence length in the selected descriptor's units.
		FileTrack,		///< File-track fallback; may include a held frame.
		ClipReference,	///< Referenced timeline length; not proof of full stored length.
		LegacyHeuristic ///< Graphless recovery with uncertain ownership/rate.
	};

	qint64 units = 0;	   ///< Original count in the declared clock: samples or frames/edit units.
	MediaRate rate;		   ///< Units per second; never a rounded Frame Rate label or integer sample rate.
	MediaRate displayRate; ///< Frame rate used only when rendering duration as timecode.
	Source source = Source::Unknown;

	bool known() const { return units > 0 && rate.valid() && source != Source::Unknown; }

	/// Round only at the presentation boundary. Exact integer arithmetic keeps
	/// sample precision and avoids overflow/float rounding even for large lengths.
	/// Four 32-bit limbs suffice: a signed 63-bit length times two 31-bit factors.
	/// This also works on MSVC, where an unsigned __int128 is unavailable.
	qint64 displayFrames() const
	{
		if (!known() || !displayRate.valid())
			return 0;
		if (rate.sameRate(displayRate))
			return units;
		std::array<quint32, 4> product{quint32(units), quint32(quint64(units) >> 32), 0, 0};
		for (quint32 factor : {quint32(rate.denominator), quint32(displayRate.numerator)})
		{
			quint64 carry = 0;
			for (auto &limb : product)
			{
				const quint64 next = quint64(limb) * factor + carry;
				limb = quint32(next);
				carry = next >> 32;
			}
		}
		const quint64 divisor = quint64(rate.numerator) * quint64(displayRate.denominator);
		quint64 remainder = 0, result = 0;
		if (product[2] == 0 && product[3] == 0)
		{
			const quint64 value = (quint64(product[1]) << 32) | product[0];
			result = value / divisor;
			remainder = value % divisor;
			if (result > quint64(std::numeric_limits<qint64>::max()))
				return 0;
		}
		else
		{
			for (int bit = 127; bit >= 0; --bit)
			{
				remainder = (remainder << 1) | ((product[size_t(bit / 32)] >> (bit % 32)) & 1u);
				if (remainder >= divisor)
				{
					if (bit >= 63)
						return 0; // Unrepresentable display count; retain the exact source value.
					remainder -= divisor;
					result |= quint64(1) << bit;
				}
			}
		}
		if (remainder >= (divisor + 1) / 2)
		{
			if (result == quint64(std::numeric_limits<qint64>::max()))
				return 0;
			++result;
		}
		return qint64(result);
	}
};

/// A separately recorded material/master track, never substituted for file duration.
struct ClipTrackDuration
{
	quint32 trackId = 0;
	MediaDuration duration;
	bool dropFrame = false;
};
