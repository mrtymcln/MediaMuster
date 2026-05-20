#pragma once

#include <QString>

namespace Theme
{
inline QString monoFont()
{
#ifdef Q_OS_MAC
	return QStringLiteral("Menlo");
#else // Q_OS_WIN
	return QStringLiteral("Consolas");
#endif
}
inline int monoFontSize()
{
	return 12;
}
} // namespace Theme