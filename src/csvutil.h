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

	/// Neutralise spreadsheet formula injection. Prefixing a single
	/// quote forces it to be read as text.
	inline QString neutralise(const QString &field)
	{
		if (field.isEmpty())
			return field;
		const QChar c = field.front();
		if (c == QLatin1Char('=') || c == QLatin1Char('+') || c == QLatin1Char('-') ||
			c == QLatin1Char('@') || c == QLatin1Char('\t') || c == QLatin1Char('\r'))
			return QLatin1Char('\'') + field;
		return field;
	}

	/// Wraps the field in quotes after neutralising any formula lead-in and
	/// escaping internal quotes. Use this for every string column.
	inline QString quoted(const QString &field)
	{
		return QLatin1Char('"') + escape(neutralise(field)) + QLatin1Char('"');
	}
} // namespace CsvUtil