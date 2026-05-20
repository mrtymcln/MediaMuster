#pragma once

#include <QString>
#include <QHash>
#include <QVector>

// MARK: - PmrEntry

/// One row from a parsed `msmFMID.pmr`. Each PMR file entry maps a
/// media filename to its Avid MOB IDs.
struct PmrEntry
{
	QString mobId;            ///< Canonical hex form of the file MOB.
	QString compositionMobId; ///< Canonical hex form of the master clip MOB
	                          ///< from the paired COMP record; shared by all
	                          ///< V01/A01/A02 relatives of the same clip.
	QString fileName;
	QString project;
};

// MARK: - PmrParser

/// Reads the Persistent Media Record which Avid writes
/// alongside MXF files. Flat filename > MobId index; consulted
/// instead of walking every MXF header.
class PmrParser
{
public:
	/// Read and parse the PMR at `pmrFilePath`. Returns an empty
	/// vector on any failure (missing file, truncated, implausible
	/// pair count) — `qWarning` logs the reason. Never throws.
	static QVector<PmrEntry> parse(const QString &pmrFilePath);

	// MARK: - Lookup tables

	using ProjectMap = QHash<QString, QVector<PmrEntry>>;

	/// Two filename-keyed lookups built in one pass:
	///
	///   `primary`  — NFC-normalised, lower-cased filename.
	///   `fallback` — primary minus extension, dots → underscores.
	///
	/// The fallback covers Avid's habit of renaming files on import,
	/// where `myclip.tail.mxf` on disk shows up as `myclip_tail` in
	/// the PMR. See `pmrkey.h` for the exact rules.
	struct ProjectMaps
	{
		ProjectMap primary;
		ProjectMap fallback;
	};

	/// Builds both lookup maps from a single PMR parse.
	static ProjectMaps buildFileMapWithFallback(const QString &pmrFilePath);
};