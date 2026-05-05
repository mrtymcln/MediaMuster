#pragma once

#include <QLatin1Char>
#include <QString>
#include <QStringLiteral>

// Shared helpers for writing CSV output. Currently used by the main table.
// Could return orphan reports and Interplay sync reports in a future release?

namespace CsvUtil
{
    // Escape a single field for inclusion in a CSV row. Doubles any
    // embedded double-quote characters so that wrapping the field in
    // outer quotes produces valid CSV per RFC 4180. Does not add the
    // surrounding quotes; the caller decides whether to quote
    // unconditionally or only when needed.
    inline QString escape(QString field)
    {
        field.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return field;
    }

    // Escape and wrap in double quotes in one step.
    inline QString quoted(const QString &field)
    {
        return QLatin1Char('"') + escape(field) + QLatin1Char('"');
    }
}