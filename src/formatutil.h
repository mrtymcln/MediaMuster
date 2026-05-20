#pragma once

#include <QString>
#include <QtGlobal>

/// Byte-count formatting. Everything goes through the same helper
/// so the same value renders identically in the UI.

namespace Format
{
// MARK: - Bytes

inline QString bytes(qint64 b)
{
	if (b < 0)
		b = 0;

	constexpr qint64 KB = 1024;
	constexpr qint64 MB = KB * 1024;
	constexpr qint64 GB = MB * 1024;
	constexpr qint64 TB = GB * 1024;

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

/// Returns an em-dash for zero.
inline QString bytesSigned(qint64 b)
{
	if (b == 0)
		return QStringLiteral("—");
	const auto sign = b > 0 ? QStringLiteral("+") : QStringLiteral("−");
	return sign + bytes(qAbs(b));
}
} // namespace Format