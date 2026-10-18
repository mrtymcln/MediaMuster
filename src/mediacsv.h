#pragma once

#include "mediafile.h"

#include <QString>
#include <QVector>

// MARK: - MediaCsv
/// One header and one line per physical file, in fixed export order.
/// The caller selects optional columns; rearranging the table does not affect CSV order.

namespace MediaCsv
{
	struct Options
	{
		/// Adds four precompute detail columns after the always-present Type.
		bool includePrecomputeDetails = false;
	};

	/// Column headings in emission order, using the same options as rows.
	QString headerLine(Options options = {});

	/// One CSV line, including its newline. Quotes and neutralises free-text fields;
	/// uses the same display values as the table.
	QString rowLine(const MediaFile &f, Options options = {});

	/// Writes and commits the file; false on I/O failure.
	/// Includes a UTF-8 BOM so Excel recognises non-Latin names.
	bool write(const QString &path, const QVector<MediaFile> &rows, Options options = {});
} // namespace MediaCsv
