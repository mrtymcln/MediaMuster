#pragma once

#include <QByteArray>
#include <QString>
#include <algorithm>
#include <array>
#include <cstring>

/// Share one lowercase dotted text form when matching 32-byte MobIds.
/// Byte-order conversion is separate: formatting alone preserves the input bytes.
namespace MobId
{
	inline constexpr int kRawSize = 32;

	/// Requires at least kRawSize readable bytes at raw.
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

	/// Uses the first kRawSize bytes; returns empty if there aren't enough.
	inline QString format(const QByteArray &raw)
	{
		if (raw.size() < kRawSize)
			return {};
		return format(reinterpret_cast<const unsigned char *>(raw.constData()));
	}

	/// Callers supply formatted MobIds; this checks the zero pattern, not validity.
	/// Any nonempty string of zeros and dots passes, even without a full MobId.
	inline bool isAllZero(const QString &formatted)
	{
		if (formatted.isEmpty())
			return false;
		return std::all_of(formatted.cbegin(), formatted.cend(),
						   [](QChar c)
						   { return c == QLatin1Char('0') || c == QLatin1Char('.'); });
	}

	/// Readers can disagree on the byte order of the material fields at bytes 16..23.
	/// Swapping their 32-, 16- and 16-bit values twice restores the input.
	/// Requires kRawSize readable bytes at src and writable bytes at dst.
	/// Buffers may be identical, but must not partly overlap.
	/// Wrapped OMF MobIds must keep their byte order when matching.
	inline void swapMaterialByteOrder(const unsigned char *src, unsigned char *dst)
	{
		if (src != dst)
			std::memcpy(dst, src, kRawSize);
		std::swap(dst[16], dst[19]);
		std::swap(dst[17], dst[18]);
		std::swap(dst[20], dst[21]);
		std::swap(dst[22], dst[23]);
	}

	/// Use when matching needs the alternate material byte order.
	/// This swaps each time; it cannot tell which representation the input uses.
	/// Expects 64 hex digits with optional dots; length checks don't validate the digits.
	/// Keep wrapped OMF MobIds out of this conversion when matching.
	inline QString swapMaterialByteOrder(const QString &mobIdHex)
	{
		if (mobIdHex.isEmpty())
			return {};

		QString clean = mobIdHex;
		clean.remove(QLatin1Char('.'));
		if (clean.size() != 64)
			return {};
		const QByteArray raw = QByteArray::fromHex(clean.toLatin1());
		if (raw.size() != kRawSize)
			return {};

		std::array<unsigned char, kRawSize> swapped;
		swapMaterialByteOrder(reinterpret_cast<const unsigned char *>(raw.constData()), swapped.data());
		return format(swapped.data());
	}
} // namespace MobId
