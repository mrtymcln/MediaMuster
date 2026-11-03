#include "audioformat_p.h"

#include <QtEndian>
#include <cstring>
#include <limits>

namespace Bella::Detail
{
	namespace
	{
		bool audioTag(QByteArrayView bytes, qsizetype offset, const char *tag)
		{
			return offset >= 0 && offset <= bytes.size() && bytes.size() - offset >= 4 &&
				   std::memcmp(bytes.data() + offset, tag, 4) == 0;
		}

	}

	std::optional<AudioFormatFraction> exactAiffFrequency(const std::array<quint8, 10> &bytes)
	{
		const quint16 signExponent = qFromBigEndian<quint16>(bytes.data());
		const quint16 exponent = signExponent & 0x7fff;
		quint64 significand = qFromBigEndian<quint64>(bytes.data() + 2);
		if ((signExponent & 0x8000) || exponent == 0x7fff || significand == 0)
			return std::nullopt;
		// The stored integer bit distinguishes normal numbers from subnormals.
		// Pseudo-denormals and unnormal encodings are not supported AIFF numbers.
		const bool integerBit = (significand & (quint64{1} << 63)) != 0;
		if ((exponent == 0 && integerBit) || (exponent != 0 && !integerBit))
			return std::nullopt;
		int power = int(exponent == 0 ? 1 : exponent) - 16383 - 63;
		while ((significand & 1) == 0)
		{
			significand >>= 1;
			++power;
		}
		if (power >= 0)
		{
			if (power >= 64 || significand > (std::numeric_limits<quint64>::max() >> power))
				return std::nullopt;
			return AudioFormatFraction{significand << power, 1};
		}
		if (power < -63)
			return std::nullopt;
		return AudioFormatFraction{significand, quint64{1} << -power};
	}

	AudioFormatValues decodeWaveSummaryFormat(QByteArrayView bytes, QStringList &notes)
	{
		AudioFormatValues result;
		// The traversal established the complete fmt chunk and its 16-byte prefix.
		const quint16 format = qFromLittleEndian<quint16>(bytes.data());
		const quint32 frequency = qFromLittleEndian<quint32>(bytes.data() + 4);
		const quint16 storage = qFromLittleEndian<quint16>(bytes.data() + 14);
		if (frequency != 0)
			result.frequency = AudioFormatFrequency{frequency};
		else
			notes.append(QStringLiteral("WAVE fmt sample frequency is zero."));
		if (format == 1 || format == 3 || format == 0xfffe)
		{
			if (storage != 0)
				result.storageBits = storage;
			else
				notes.append(QStringLiteral("Audio bit depth is unavailable because the recorded sample storage width is zero."));
		}

		quint16 subtype = format;
		std::optional<quint16> precision;
		if (format == 0xfffe)
		{
			if (bytes.size() < 18)
			{
				notes.append(QStringLiteral("WAVE extensible format has no complete extension-size field; precision and encoding are unavailable."));
				return result;
			}
			const quint16 extension = qFromLittleEndian<quint16>(bytes.data() + 16);
			if (extension < 22 || extension > bytes.size() - 18)
			{
				notes.append(QStringLiteral("WAVE extensible format has an incomplete required extension; precision and encoding are unavailable."));
				return result;
			}
			constexpr std::array<quint8, 16> pcmGuid = {1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
			constexpr std::array<quint8, 16> floatGuid = {3, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
			if (std::memcmp(bytes.data() + 24, pcmGuid.data(), pcmGuid.size()) == 0)
				subtype = 1;
			else if (std::memcmp(bytes.data() + 24, floatGuid.data(), floatGuid.size()) == 0)
				subtype = 3;
			else
			{
				notes.append(QStringLiteral("WAVE extensible subtype is unsupported; storage width does not establish sample precision."));
				return result;
			}
			precision = qFromLittleEndian<quint16>(bytes.data() + 18);
		}
		else
			precision = storage;

		if (subtype == 1)
		{
			result.encoding = AudioFormatEncoding::Pcm;
		}
		else if (subtype == 3)
		{
			result.encoding = AudioFormatEncoding::IeeeFloat;
		}
		else
		{
			notes.append(QStringLiteral("WAVE encoding is unsupported for sample precision and compression naming."));
			return result;
		}
		if (storage == 0 || storage > 64 || *precision == 0 || *precision > storage || *precision > 64 ||
			(subtype == 3 && storage != 32 && storage != 64))
		{
			if (storage != 0)
				notes.append(QStringLiteral("WAVE sample precision is invalid or unsupported for its storage width and encoding."));
		}
		else
			result.bits = *precision;
		return result;
	}

	AudioFormatValues decodeAiffSummaryCommon(QByteArrayView bytes, bool compressed, QStringList &notes)
	{
		AudioFormatValues result;
		std::array<quint8, 10> frequency{};
		std::memcpy(frequency.data(), bytes.data() + 8, frequency.size());
		const AudioFormatFrequency source{frequency};
		if (exactAiffFrequency(frequency))
			result.frequency = source;
		else
			notes.append(QStringLiteral("AIFF COMM sample frequency is invalid or outside the supported exact unsigned-64 fraction range."));

		const qint16 precision = qFromBigEndian<qint16>(bytes.data() + 6);
		bool integer = !compressed;
		bool floating = false;
		std::optional<quint32> requiredWidth;
		if (compressed)
		{
			if (bytes.size() < 22)
			{
				notes.append(QStringLiteral("AIFF-C COMM encoding identifier is incomplete; sample precision and encoding are unavailable."));
				return result;
			}
			integer = audioTag(bytes, 18, "NONE") || audioTag(bytes, 18, "twos") ||
					  audioTag(bytes, 18, "sowt") || audioTag(bytes, 18, "in24") || audioTag(bytes, 18, "in32");
			floating = audioTag(bytes, 18, "fl32") || audioTag(bytes, 18, "FL32") ||
					   audioTag(bytes, 18, "fl64") || audioTag(bytes, 18, "FL64");
			if (audioTag(bytes, 18, "in24"))
				requiredWidth = 24;
			else if (audioTag(bytes, 18, "in32") || audioTag(bytes, 18, "fl32") || audioTag(bytes, 18, "FL32"))
				requiredWidth = 32;
			else if (audioTag(bytes, 18, "fl64") || audioTag(bytes, 18, "FL64"))
				requiredWidth = 64;
			result.storageBits = requiredWidth;
		}
		if (integer)
		{
			result.encoding = AudioFormatEncoding::Pcm;
		}
		else if (floating)
		{
			result.encoding = AudioFormatEncoding::IeeeFloat;
		}
		else
		{
			notes.append(QStringLiteral("AIFF-C encoding is unsupported for sample precision and compression naming."));
			return result;
		}
		if (precision <= 0 || precision > 64 || (requiredWidth && quint32(precision) != *requiredWidth))
			notes.append(QStringLiteral("AIFF COMM sample precision is invalid or unsupported for its encoding."));
		else
			result.bits = quint32(precision);
		return result;
	}

}
