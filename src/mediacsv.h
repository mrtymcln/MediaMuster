#pragma once

#include "mediafile.h"

#include <QString>
#include <QVector>

// MARK: - MediaCsv
/// One header line, one line per MediaFile: 19 columns, or 23 with
/// experimental precompute details. The explicit export schema follows
/// the table's default order, then adds database status and MOB IDs;
/// it is independent of table presentation and model code. Tests keep
/// the headings and emitted values aligned.

namespace MediaCsv
{
	struct Options
	{
		/// Adds four precompute detail columns after the always-present Type.
		bool includePrecomputeDetails = false;
	};

	/// Column headings in emission order, using the same options as rows.
	QString headerLine(Options options = {});

	/// One CSV line for `f`, newline included. Every string column goes
	/// through CsvUtil::quoted (spreadsheet-formula injection is
	/// neutralised there). Kind, Type, precompute details, Sample Rate, Duration, Size (MB)
	/// and Date Created route through MediaFile's display helpers so the export and
	/// the table can't disagree. Clip Name and Codec are written raw: the
	/// codec column MUST be (codecDisplay carries the debug raw-hex
	/// toggle, which belongs on screen and not in an export), and the clip
	/// name is byte-identical to its helper today.
	QString rowLine(const MediaFile &f, Options options = {});

	/// Writes header + rows to `path`. False on any I/O failure.
	/// Emits a UTF-8 BOM: Excel on Windows assumes the legacy ANSI code
	/// page for a BOM-less CSV and renders non-Latin clip/bin names as
	/// mojibake. Numbers and other readers ignore it.
	bool write(const QString &path, const QVector<MediaFile> &rows, Options options = {});
} // namespace MediaCsv
