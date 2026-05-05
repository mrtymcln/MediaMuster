#pragma once

#include <QByteArray>
#include <QString>

// Shared 32-byte MOB ID / UMID formatting; PMR/MDB/AVB all route through here so
// the hex strings are byte-for-byte identical across sources.
// Output: 4 groups of 16 lowercase hex chars, dot-separated.
//   060a2b3401010105.01010f1013000000.a4bb7f1311399006.6d01ce4ff0f5d57a

namespace MobId
{
	// Caller guarantees at least 32 valid bytes at `raw`.
	inline QString format(const unsigned char *raw)
	{
		static constexpr char kHex[] = "0123456789abcdef";
		QString out;
		out.reserve(67); // 4 * 16 + 3 dots
		for (int g = 0; g < 4; ++g)
		{
			if (g > 0)
				out.append(QLatin1Char('.'));
			for (int b = 0; b < 8; ++b)
			{
				const unsigned char byte = raw[g * 8 + b];
				out.append(QLatin1Char(kHex[byte >> 4]));
				out.append(QLatin1Char(kHex[byte & 0x0F]));
			}
		}
		return out;
	}

	// Empty if shorter than 32 bytes.
	inline QString format(const QByteArray &raw)
	{
		if (raw.size() < 32)
			return {};
		return format(reinterpret_cast<const unsigned char *>(raw.constData()));
	}

	// All-zero MOB/UMID = Avid never wrote one. Handles 4×16, legacy 4×8,
	// and a 50-zero catch-all for other variants.
	inline bool isAllZero(const QString &formattedMobOrUmid)
	{
		static const QString kAllZero64 =
			QStringLiteral("0000000000000000.0000000000000000."
						   "0000000000000000.0000000000000000");
		static const QString kAllZero32 =
			QStringLiteral("00000000.00000000.00000000.00000000");
		return (formattedMobOrUmid == kAllZero64 ||
				formattedMobOrUmid == kAllZero32 ||
				formattedMobOrUmid.count('0') > 50);
	}
}
