#pragma once

#include "mediafile.h"

#include <QString>
#include <QVector>

// MARK: - MediaCsv
/// One header line and one line per physical MediaFile. Optional columns carry
/// precompute details and separately recovered per-track clip durations. The explicit export schema follows
/// the established export order, ending with database status, MOB IDs, KelpieId
/// and the OmfScan family boolean;
/// dragging table columns does not change export order. The caller snapshots
/// both gates from the table model. Tests compare headings with the default
/// table layout for all four gate combinations and check emitted values.

namespace MediaCsv
{
	struct Options
	{
		/// Adds four precompute detail columns after the always-present Type.
		bool includePrecomputeDetails = false;
		bool includeClipDuration = false;
	};

	/// Column headings in emission order, using the same options as rows.
	QString headerLine(Options options = {});

	/// One CSV line for `f`, newline included. Every string column goes
	/// through CsvUtil::quoted (spreadsheet-formula injection is
	/// neutralised there). Kind, Type, precompute details, Sample Rate, Duration, Size (MB)
	/// and Date Created route through MediaFile's display helpers so the export and
	/// the table can't disagree. Clip Name and Codec use their stored text,
	/// matching the table's displayed values.
	QString rowLine(const MediaFile &f, Options options = {});

	/// Writes header + rows to `path`. False on any I/O failure.
	/// Emits a UTF-8 BOM: Excel on Windows assumes the legacy ANSI code
	/// page for a BOM-less CSV and renders non-Latin clip/bin names as
	/// mojibake. Numbers and other readers ignore it.
	bool write(const QString &path, const QVector<MediaFile> &rows, Options options = {});
} // namespace MediaCsv
