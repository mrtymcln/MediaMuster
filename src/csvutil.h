#pragma once

#include <QLatin1Char>
#include <QString>
#include <QStringLiteral>

namespace CsvUtil
{
    inline QString escape(QString field)
    {
        field.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return field;
    }

    inline QString quoted(const QString &field)
    {
        return QLatin1Char('"') + escape(field) + QLatin1Char('"');
    }
}