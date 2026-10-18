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
		constexpr qint64 KB = 1000;
		constexpr qint64 MB = KB * 1000;
		constexpr qint64 GB = MB * 1000;
		constexpr qint64 TB = GB * 1000;

		if (b >= TB)
			return QStringLiteral("%1 TB").arg(b / double(TB), 0, 'f', 1);
		if (b >= GB)
			return QStringLiteral("%1 GB").arg(b / double(GB), 0, 'f', 1);
		if (b >= MB)
			return QStringLiteral("%1 MB").arg(b / double(MB), 0, 'f', 1);
		if (b >= KB)
			return QStringLiteral("%1 KB").arg(b / double(KB), 0, 'f', 0);
		return QStringLiteral("%1 B").arg(b);
	}
} // namespace Format
