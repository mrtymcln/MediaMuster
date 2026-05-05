#pragma once

#include <QString>

// Mono font for the log console only; the rest of the UI uses native styling.
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

    inline int monoFontSize() { return 12; }
}
