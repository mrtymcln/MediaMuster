#pragma once

#include <QLocale>
#include <QString>
#include <QtGlobal>

namespace Format
{
	// MARK: - Thousands separators

	inline QString count(qint64 n)
	{
		// UI and worker callers each reuse their own locale instance.
		static thread_local const QLocale loc(QLocale::English, QLocale::UnitedKingdom);
		return loc.toString(n);
	}

	// MARK: - Bytes

	inline QString bytes(qint64 b)
	{
		if (b < 0)
			b = 0;
		constexpr qint64 kKilobyte = 1000;
		constexpr qint64 kMegabyte = kKilobyte * 1000;
		constexpr qint64 kGigabyte = kMegabyte * 1000;
		constexpr qint64 kTerabyte = kGigabyte * 1000;

		if (b >= kTerabyte)
			return QStringLiteral("%1 TB").arg(b / double(kTerabyte), 0, 'f', 1);
		if (b >= kGigabyte)
			return QStringLiteral("%1 GB").arg(b / double(kGigabyte), 0, 'f', 1);
		if (b >= kMegabyte)
			return QStringLiteral("%1 MB").arg(b / double(kMegabyte), 0, 'f', 1);
		if (b >= kKilobyte)
			return QStringLiteral("%1 KB").arg(b / double(kKilobyte), 0, 'f', 0);
		return QStringLiteral("%1 B").arg(b);
	}
} // namespace Format
