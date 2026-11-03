#include "timing.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <numeric>

namespace Bella
{
	namespace
	{

		bool validRate(TimingRate rate) noexcept
		{
			return rate.numerator > 0 && rate.denominator > 0;
		}

		bool sameRate(TimingRate left, TimingRate right) noexcept
		{
			const auto leftCommon = std::gcd(left.numerator, left.denominator);
			const auto rightCommon = std::gcd(right.numerator, right.denominator);
			return left.numerator / leftCommon == right.numerator / rightCommon &&
				   left.denominator / leftCommon == right.denominator / rightCommon;
		}

		// A nonnegative signed64 count times two unsigned64 fraction words needs at
		// most 191 bits. Fixed working words retain the approved exact AIFF frequency
		// range without an arbitrary-precision dependency or compiler-specific types.
		using Product = std::array<quint32, 6>;

		Product productOf(quint64 value) noexcept
		{
			return {quint32(value), quint32(value >> 32), 0, 0, 0, 0};
		}

		void multiply(Product &product, quint64 factor) noexcept
		{
			Product result{};
			for (std::size_t half = 0; half < 2; ++half)
			{
				const auto word = quint32(factor >> (half * 32));
				quint64 carry = 0;
				for (std::size_t i = 0; i + half < result.size(); ++i)
				{
					const quint64 next = quint64(product[i]) * word + result[i + half] + carry;
					result[i + half] = quint32(next);
					carry = next >> 32;
				}
			}
			product = result;
		}

		int compare(const Product &left, const Product &right) noexcept
		{
			for (std::size_t i = left.size(); i-- > 0;)
				if (left[i] != right[i])
					return left[i] > right[i] ? 1 : -1;
			return 0;
		}

		void shiftIn(Product &value, quint32 bit) noexcept
		{
			for (auto &word : value)
			{
				const quint32 next = word >> 31;
				word = (word << 1) | bit;
				bit = next;
			}
		}

		void subtract(Product &left, const Product &right) noexcept
		{
			quint64 borrow = 0;
			for (std::size_t i = 0; i < left.size(); ++i)
			{
				const quint64 subtrahend = quint64(right[i]) + borrow;
				const quint64 original = left[i];
				left[i] = quint32(original - subtrahend);
				borrow = original < subtrahend;
			}
		}

		bool fitsUnsigned64(const Product &value) noexcept
		{
			return std::all_of(value.begin() + 2, value.end(), [](quint32 word)
							   { return word == 0; });
		}

		std::optional<qint64> roundedQuotient(const Product &product, const Product &divisor) noexcept
		{
			quint64 quotient = 0;
			bool roundUp = false;
			if (fitsUnsigned64(product) && fitsUnsigned64(divisor))
			{
				const quint64 value = (quint64(product[1]) << 32) | product[0];
				const quint64 denominator = (quint64(divisor[1]) << 32) | divisor[0];
				quotient = value / denominator;
				roundUp = value % denominator >= denominator / 2 + denominator % 2;
			}
			else
			{
				Product remainder{};
				for (int bit = 191; bit >= 0; --bit)
				{
					shiftIn(remainder, (product[std::size_t(bit / 32)] >> (bit % 32)) & 1u);
					if (compare(remainder, divisor) >= 0)
					{
						if (bit >= 63)
							return std::nullopt;
						subtract(remainder, divisor);
						quotient |= quint64(1) << bit;
					}
				}
				// The divisor is at most 128 bits. Doubling its smaller remainder fits.
				shiftIn(remainder, 0);
				roundUp = compare(remainder, divisor) >= 0;
			}
			const auto maximum = quint64(std::numeric_limits<qint64>::max());
			if (quotient > maximum || (quotient == maximum && roundUp))
				return std::nullopt;
			return qint64(quotient + (roundUp ? 1 : 0));
		}

	} // namespace

	std::optional<TimingRate> interpretAvidEditRate(qint32 numerator, qint32 denominator) noexcept
	{
		if (numerator <= 0 || denominator <= 0)
			return std::nullopt;
		for (qint64 multiple = 1; multiple <= 10; ++multiple)
		{
			if (qint64(numerator) * 1000 == qint64(denominator) * 23976 * multiple)
				return TimingRate{quint64(24000 * multiple), 1001};
			if (qint64(numerator) * 100 == qint64(denominator) * 2997 * multiple)
				return TimingRate{quint64(30000 * multiple), 1001};
		}
		return TimingRate{quint64(numerator), quint64(denominator)};
	}

	std::optional<ExactDuration> makeExactDuration(qint64 count, TimingRate calculationRate) noexcept
	{
		if (count < 0 || !validRate(calculationRate))
			return std::nullopt;
		return ExactDuration{count, calculationRate};
	}

	std::optional<qint64> displayFrameCount(const ExactDuration &duration, TimingRate displayRate) noexcept
	{
		if (duration.count < 0 || !validRate(duration.rate) || !validRate(displayRate))
			return std::nullopt;
		if (sameRate(duration.rate, displayRate))
			return duration.count;

		if (duration.count == 0)
			return qint64{0};
		Product numerator = productOf(quint64(duration.count));
		multiply(numerator, duration.rate.denominator);
		multiply(numerator, displayRate.numerator);
		Product denominator = productOf(duration.rate.numerator);
		multiply(denominator, displayRate.denominator);
		return roundedQuotient(numerator, denominator);
	}

	QString formatDuration(const ExactDuration &duration, TimingRate displayRate, quint16 nominalBase,
						   TimecodeMode mode)
	{
		if (!validRate(displayRate) || nominalBase == 0)
			return {};
		const qint64 base = nominalBase;
		if (mode == TimecodeMode::DropFrame)
		{
			if (!((base == 30 && sameRate(displayRate, {30000, 1001})) ||
				  (base == 60 && sameRate(displayRate, {60000, 1001}))))
				return {};
		}
		else if (mode != TimecodeMode::NonDropFrame)
			return {};
		const auto count = displayFrameCount(duration, displayRate);
		if (!count)
			return {};

		qint64 minutes = 0;
		qint64 framesInMinute = 0;
		if (mode == TimecodeMode::DropFrame)
		{
			const qint64 dropped = base / 15;
			const qint64 fullMinute = base * 60;
			const qint64 shortMinute = fullMinute - dropped;
			const qint64 tenMinutes = fullMinute * 10 - dropped * 9;
			minutes = (*count / tenMinutes) * 10;
			framesInMinute = *count % tenMinutes;
			if (framesInMinute >= fullMinute)
			{
				framesInMinute -= fullMinute;
				minutes += 1 + framesInMinute / shortMinute;
				framesInMinute = framesInMinute % shortMinute + dropped;
			}
		}
		else
		{
			minutes = (*count / base) / 60;
			framesInMinute = *count % (base * 60);
		}
		const auto pattern =
			mode == TimecodeMode::DropFrame ? QStringLiteral("%1;%2;%3;%4") : QStringLiteral("%1:%2:%3:%4");
		return pattern.arg(minutes / 60, 2, 10, QLatin1Char('0'))
			.arg(minutes % 60, 2, 10, QLatin1Char('0'))
			.arg(framesInMinute / base, 2, 10, QLatin1Char('0'))
			.arg(framesInMinute % base, 2, 10, QLatin1Char('0'));
	}

} // namespace Bella
