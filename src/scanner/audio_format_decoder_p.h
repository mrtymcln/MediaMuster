#pragma once

#include <QByteArrayView>
#include <QStringList>
#include <QtGlobal>
#include <array>
#include <optional>
#include <variant>

namespace Bella::Detail
{
	// MARK: - Audio format values

	enum class AudioFormatEncoding
	{
		Pcm,
		IeeeFloat
	};

	using AudioFormatFrequency = std::variant<quint32, std::array<quint8, 10>>;

	struct AudioFormatFraction
	{
		quint64 numerator = 0;
		quint64 denominator = 0;
	};

	struct AudioFormatValues
	{
		std::optional<AudioFormatFrequency> frequency;
		std::optional<quint32> bits;
		std::optional<quint32> storageBits;
		std::optional<AudioFormatEncoding> encoding;
	};

	// MARK: - Summary decoding

	// Strict MDB/OMF summary interpretation. The caller establishes the complete
	// bounded fmt/COMM payload and its required 16/18-byte prefix before calling.
	// Native audio retains its separately approved precision/frequency policies.
	[[nodiscard]] AudioFormatValues decodeWaveSummaryFormat(QByteArrayView bytes, QStringList &notes);
	[[nodiscard]] AudioFormatValues decodeAiffSummaryCommon(QByteArrayView bytes, bool compressed, QStringList &notes);

	// Positive canonical AIFF extended80 values representable as an exact U64 pair.
	[[nodiscard]] std::optional<AudioFormatFraction> exactAiffFrequency(const std::array<quint8, 10> &bytes);
}
