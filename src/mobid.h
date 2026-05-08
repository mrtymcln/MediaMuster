#pragma once

#include <QByteArray>
#include <QString>
#include <algorithm>

// A MobId (Material Object Id / SMPTE UMID) is the 32-byte unique key Avid embeds
// in every MXF and AAF asset. It binds an essence file to its master clip, sub clip,
// and bin entry across PMR, MDB, and AVB sources — so we render it to a single
// canonical hex form here to keep comparisons byte-for-byte stable regardless of
// where the bytes were read from:
//   060a2b3401010105.01010f1013000000.a4bb7f1311399006.6d01ce4ff0f5d57a

namespace MobId
{
	inline constexpr int kRawSize = 32;
	// Caller guarantees at least kRawSize valid bytes at `raw`.
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

	inline QString format(const QByteArray &raw)
	{
		if (raw.size() < kRawSize)
			return {};
		return format(reinterpret_cast<const unsigned char *>(raw.constData()));
	}

	// MOB/UMID with zeros is what Avid leaves when no real Id was assigned.
	// Accepts any width by checking the whole string is just '0's and group separators.
	inline bool isAllZero(const QString &formatted)
	{
		if (formatted.isEmpty())
			return false;
		return std::all_of(formatted.cbegin(), formatted.cend(), [](QChar c)
						   { return c == QLatin1Char('0') || c == QLatin1Char('.'); });
	}
}