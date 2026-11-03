#include "timing.h"
#include "audioformat_p.h"
#include "mdbparser.h"
#include "mxfparser.h"
#include "omfparser.h"

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

		bool dropFrameClock(TimingRate rate, quint16 base) noexcept
		{
			return (base == 30 && sameRate(rate, {30000, 1001})) || (base == 60 && sameRate(rate, {60000, 1001}));
		}

		std::optional<TimingRate> literalRate(qint32 numerator, qint32 denominator) noexcept
		{
			if (numerator <= 0 || denominator <= 0)
				return std::nullopt;
			return TimingRate{quint64(numerator), quint64(denominator)};
		}

		template <typename Rate>
		std::optional<TimingRate> legacyRate(const std::optional<Rate> &rate, bool interpretAvid) noexcept
		{
			if (!rate)
				return std::nullopt;
			using Encoding = decltype(rate->encoding);
			if (interpretAvid && rate->encoding == Encoding::ExactEditRate)
				return interpretAvidEditRate(rate->words.numerator, rate->words.denominator);
			return literalRate(rate->words.numerator, rate->words.denominator);
		}

		std::optional<TimingRate> mxfRate(const std::optional<MxfRate> &rate) noexcept
		{
			return rate ? literalRate(rate->numerator, rate->denominator) : std::nullopt;
		}

		QByteArrayView descriptorTag(const std::optional<std::array<quint8, 4>> &tag) noexcept
		{
			return tag ? QByteArrayView(reinterpret_cast<const char *>(tag->data()), 4) : QByteArrayView{};
		}

		// The readers establish these unit contexts separately from the Kind column.
		bool audioDescriptor(QByteArrayView tag) noexcept
		{
			return tag == "PCMA" || tag == "AIFD" || tag == "WAVD" || tag == "MPGA" || tag == "WAVE";
		}

		bool pictureDescriptor(QByteArrayView tag) noexcept
		{
			return tag == "CDCI" || tag == "RGBA" || tag == "MPGI" || tag == "JPED" || tag == "TIFD";
		}

		template <typename Numbering>
		std::optional<TimecodeInputs> displayFor(std::optional<TimingRate> clock, std::optional<quint16> nominalBase,
												 Numbering numbering) noexcept
		{
			if (!clock || !nominalBase || *nominalBase == 0)
				return std::nullopt;
			TimecodeMode mode;
			switch (numbering)
			{
			case Numbering::NotRequired:
			case Numbering::MissingNdfFallback:
			case Numbering::RecordedNdf:
				mode = TimecodeMode::NonDropFrame;
				break;
			case Numbering::RecordedDf:
				if (!dropFrameClock(*clock, *nominalBase))
					return std::nullopt;
				mode = TimecodeMode::DropFrame;
				break;
			case Numbering::Unavailable:
			default:
				return std::nullopt;
			}
			return TimecodeInputs{*clock, *nominalBase, mode};
		}

		std::optional<TimingRate> summaryFrequency(const std::optional<OmfAudioSourceValue> &frequency)
		{
			if (!frequency)
				return std::nullopt;
			if (const auto *hz = std::get_if<quint32>(&*frequency))
				return *hz ? std::optional<TimingRate>(TimingRate{*hz, 1}) : std::nullopt;
			if (const auto *extended = std::get_if<std::array<quint8, 10>>(&*frequency))
				if (const auto exact = Detail::exactAiffFrequency(*extended))
					return TimingRate{exact->numerator, exact->denominator};
			return std::nullopt;
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

	FileTiming timingFor(const MdbFileRecord &file, std::optional<quint16> nominalBase)
	{
		if (!file.ownDescriptorQualified)
			return {};
		const auto tag = descriptorTag(file.descriptorClass);
		const bool picture = pictureDescriptor(tag);
		const auto rate = legacyRate(file.sampleRate, picture);
		FileTiming result;
		if (file.length && rate)
			result.duration = makeExactDuration(*file.length, *rate);
		std::optional<TimingRate> clock;
		if (audioDescriptor(tag))
			clock = legacyRate(file.audioEditRate, true);
		else if (picture)
			clock = rate;
		result.display = displayFor(clock, nominalBase, file.numbering);
		return result;
	}

	FileTiming timingFor(const MxfFileInformation &file, std::optional<quint16> nominalBase)
	{
		const auto rate = mxfRate(file.sampleRate);
		FileTiming result;
		if (file.containerDuration && rate)
			result.duration = makeExactDuration(*file.containerDuration, *rate);
		std::optional<TimingRate> clock;
		if (file.kind == MxfKind::Audio)
			clock = mxfRate(file.projectEditRate);
		else if (file.kind == MxfKind::Video && file.frameRateQualified)
			clock = rate;
		result.display = displayFor(clock, nominalBase, file.numbering);
		return result;
	}

	FileTiming timingFor(const OmfFileInformation &file, std::optional<quint16> nominalBase)
	{
		const auto tag = descriptorTag(file.descriptorClass);
		const bool picture = pictureDescriptor(tag);
		const auto sampleClock = legacyRate(file.sampleRate, picture);
		std::optional<TimingRate> rate;
		switch (file.durationClockSource)
		{
		case OmfDurationClockSource::DescriptorSampleRate:
			rate = sampleClock;
			break;
		case OmfDurationClockSource::SummaryFrequency:
			rate = summaryFrequency(file.audioFrequency);
			break;
		case OmfDurationClockSource::Unavailable:
			break;
		}
		FileTiming result;
		if (file.length && rate)
			result.duration = makeExactDuration(*file.length, *rate);
		std::optional<TimingRate> clock;
		if (audioDescriptor(tag))
			clock = legacyRate(file.audioEditRate, true);
		else if (picture)
			clock = sampleClock;
		result.display = displayFor(clock, nominalBase, file.numbering);
		return result;
	}

	FileTiming timingFor(const OmfNativeAudioInformation &file)
	{
		FileTiming result;
		if (file.sampleFrameCount && file.samplingRate)
			if (const auto rate = literalRate(file.samplingRate->numerator, file.samplingRate->denominator))
				result.duration = makeExactDuration(*file.sampleFrameCount, *rate);
		return result;
	}

	QString formatDuration(const ExactDuration &duration, TimingRate displayRate, quint16 nominalBase,
						   TimecodeMode mode)
	{
		if (!validRate(displayRate) || nominalBase == 0)
			return {};
		const qint64 base = nominalBase;
		if (mode == TimecodeMode::DropFrame)
		{
			if (!dropFrameClock(displayRate, nominalBase))
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
