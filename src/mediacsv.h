#pragma once

#include "mediafile.h"

#include <QString>
#include <QVector>

// MARK: - MediaCsv
/// The CSV shape of the media table: one header line, one line per
/// MediaFile, 20 columns. Lives outside the window class so it can be
/// tested — the header string and the field-emission chain are two
/// parallel lists that must stay column-for-column aligned, and nothing
/// but a test can see that from the inside.

namespace MediaCsv
{
	/// The 20 column headings, in emission order.
	QString headerLine();

	/// One CSV line for `f`, newline included. Every string column goes
	/// through CsvUtil::quoted (spreadsheet-formula injection is
	/// neutralised there). Kind, Type, Duration, Size (MB) and Date
	/// Created route through MediaFile's display helpers so the export and
	/// the table can't disagree. Clip Name and Codec are written raw: the
	/// codec column MUST be (codecDisplay carries the debug raw-hex
	/// toggle, which belongs on screen and not in an export), and the clip
	/// name is byte-identical to its helper today.
	QString rowLine(const MediaFile &f);

	/// Writes header + rows to `path`. False on any I/O failure.
	/// Emits a UTF-8 BOM: Excel on Windows assumes the legacy ANSI code
	/// page for a BOM-less CSV and renders non-Latin clip/bin names as
	/// mojibake. Numbers and other readers ignore it.
	bool write(const QString &path, const QVector<MediaFile> &rows);
} // namespace MediaCsv