#pragma once

#include <QByteArray>
#include <QString>
#include <algorithm>
#include <array>
#include <cstring>

/// A MobId (Material Object Id / SMPTE UMID) is the 32-byte unique
/// key Avid embeds in every MXF and AAF asset, binding essence to
/// master clip, sub clip, and bin across PMR, MDB, and AVB sources.
/// Canonical hex form:
///
///   060a2b3401010105.01010f1013000000.a4bb7f1311399006.6d01ce4ff0f5d57a
///
/// Every compare goes through this formatter so MobIds from any
/// source compare byte-for-byte.

namespace MobId
{
	inline constexpr int kRawSize = 32;

	// MARK: - Formatting

	/// Renders 32 raw bytes as the canonical dotted hex form above.
	/// Caller must guarantee at least kRawSize valid bytes at `raw`.
	inline QString format(const unsigned char *raw)
	{
		static constexpr char kHex[] = "0123456789abcdef";
		QString out;
		out.reserve(2 * kRawSize + 3);
		for (int i = 0; i < kRawSize; ++i)
		{
			if (i > 0 && (i % 8) == 0)
				out.append(QLatin1Char('.'));
			const unsigned char byte = raw[i];
			out.append(QLatin1Char(kHex[byte >> 4]));
			out.append(QLatin1Char(kHex[byte & 0x0F]));
		}
		return out;
	}

	/// Same, but takes a QByteArray. Returns empty if the buffer
	/// is shorter than kRawSize.
	inline QString format(const QByteArray &raw)
	{
		if (raw.size() < kRawSize)
			return {};
		return format(reinterpret_cast<const unsigned char *>(raw.constData()));
	}

	// MARK: - Validity

	/// True when the formatted MobId is 'all zeros and dots'; Avid
	/// leaves this when no real Id was assigned. Surfaced as a bad
	/// UMID; such files risk vanishing on the next consolidate.
	inline bool isAllZero(const QString &formatted)
	{
		if (formatted.isEmpty())
			return false;
		return std::all_of(formatted.cbegin(), formatted.cend(),
						   [](QChar c)
						   { return c == QLatin1Char('0') || c == QLatin1Char('.'); });
	}

	// MARK: - Encoding conversion

	/// Swap the middle fields (bytes 16..23) of a logical 32-byte MOB,
	/// converting between little- and big-endian material fields. AVB
	/// scalars follow the container's byte order and are normalized by its
	/// reader before reaching this helper. The two encodings share every
	/// other byte; only these differ:
	///   bytes 16..19: a u32  (swap [16] with [19], [17] with [18])
	///   bytes 20..21: a u16  (swap [20] with [21])
	///   bytes 22..23: a u16  (swap [22] with [23])
	/// The swap is its own inverse, so one function covers both directions.
	/// `src` and `dst` may be the same buffer. Caller guarantees kRawSize
	/// valid bytes at each. Single source of truth for the byte layout:
	/// both toPmrForm (hex) and the AVB parser (raw bytes) route through here.
	inline void swapMiddleFields(const unsigned char *src, unsigned char *dst)
	{
		if (src != dst)
			std::memcpy(dst, src, kRawSize);
		std::swap(dst[16], dst[19]);
		std::swap(dst[17], dst[18]);
		std::swap(dst[20], dst[21]);
		std::swap(dst[22], dst[23]);
	}

	/// Re-encode formatted MobId hex from little-endian material fields to
	/// PMR/MDB form via swapMiddleFields. Bridges a UMID extracted from an
	/// MXF header (tag 0x4401) into a MOB ID that matches PMR/MDB-keyed
	/// lookup tables. Returns empty if `avbFormHex` isn't 32 bytes.
	inline QString toPmrForm(const QString &avbFormHex)
	{
		if (avbFormHex.isEmpty())
			return {};

		// Strip the dot separators; require exactly 64 hex chars.
		QString clean = avbFormHex;
		clean.remove(QLatin1Char('.'));
		if (clean.size() != 64)
			return {};
		const QByteArray raw = QByteArray::fromHex(clean.toLatin1());
		if (raw.size() != kRawSize)
			return {};

		std::array<unsigned char, kRawSize> swapped;
		swapMiddleFields(reinterpret_cast<const unsigned char *>(raw.constData()), swapped.data());
		return format(swapped.data());
	}
} // namespace MobId
