#pragma once

#include <QLatin1Char>
#include <QString>
#include <QStringLiteral>

namespace CsvUtil
{
// MARK: - Escaping

/// Doubles every literal `"` inside the field to escape it.
inline QString escape(QString field)
{
	field.replace(QLatin1Char('"'), QStringLiteral("\"\""));
	return field;
}

/// Wraps the field in quotes after escaping any internal ones.
/// Use this for every string column — clip names, paths, codecs —
/// not just the ones we know contain commas.
inline QString quoted(const QString &field)
{
	return QLatin1Char('"') + escape(field) + QLatin1Char('"');
}
} // namespace CsvUtil