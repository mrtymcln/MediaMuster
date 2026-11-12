#pragma once

#include "media/media_file_record.h"

#include <QString>
#include <QVector>

// MARK: - MediaCsv
/// CSV export with one record per physical file. CSV-only fields follow the table fields.

namespace MediaCsv
{
	struct Options
	{
		/// Adds precompute details after Bit Depth.
		bool includePrecomputeDetails = false;
	};

	/// Column headings in emission order, using the same options as rows.
	QString headerLine(Options options = {});

	/// One CSV record, including its terminal newline. Quotes and neutralises free-text fields;
	/// uses the same display values as the table.
	QString rowLine(const MediaFileRecord &f, Options options = {});

	/// Writes and commits the file; false on I/O failure.
	/// Includes a UTF-8 BOM so Excel recognises non-Latin names.
	bool write(const QString &path, const QVector<MediaFileRecord> &rows, Options options = {});
} // namespace MediaCsv
