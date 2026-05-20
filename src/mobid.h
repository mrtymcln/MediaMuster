#pragma once

#include <QByteArray>
#include <QString>
#include <algorithm>

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

/// True when the formatted MobId is "all zeros and dots" — Avid
/// leaves this when no real Id was assigned. Surfaced as a bad
/// UMID; such files risk vanishing on the next consolidate.
inline bool isAllZero(const QString &formatted)
{
	if (formatted.isEmpty())
		return false;
	return std::all_of(formatted.cbegin(), formatted.cend(), [](QChar c)
	                   { return c == QLatin1Char('0') || c == QLatin1Char('.'); });
}
} // namespace MobId