#pragma once

#include <QString>
#include <QHash>
#include <QVector>

// MARK: - PmrEntry

/// One row from a parsed `msmFMID.pmr`. Each PMR file entry maps a
/// media filename to its Avid MOB IDs.
struct PmrEntry
{
	QString mobId;		 ///< Canonical hex form of the file MOB.
	QString masterMobId; ///< Canonical hex form of the master clip MOB
						 ///< from the paired MASTER record; shared by all
						 ///< V01/A01/A02 relatives of the same clip.
	QString fileName;	 ///< From the UTF-8 record set when the PMR has one (MC 2025
						 ///< does); else the MacRoman set, decoded.
	QString project;	 ///< MacRoman in every PMR section — Avid writes '?' for
						 ///< characters outside it, and no database holds a better copy.
	/// The essence file's modification time when Avid indexed it, Unix
	/// seconds (0 when absent). Equals the file's st_mtime on 360/360 real
	/// files. The scanner's staleness check: a file whose mtime no longer
	/// matches is the one this record describes only by name, so its header
	/// is read instead of trusting the database's technical facts.
	quint32 fileModifiedSecs = 0;
};

// MARK: - PmrParser

/// Reads the Persistent Media Record which Avid writes alongside
/// MXF files. A flat filename-to-MobId index, consulted instead of
/// walking every MXF header.
class PmrParser
{
public:
	/// Read and parse the PMR at `pmrFilePath`. Returns an empty vector when
	/// the file is missing, too small, an unsupported version, or claims an
	/// implausible pair count. A mid-body truncation, or a record boundary
	/// missing the Avid MOB prefix (desync), instead returns the entries
	/// parsed so far. The reason is logged to the lcPmr category. Never throws.
	///
	/// `ok` (optional) reports whether the file parsed cleanly end to end:
	/// false on every failure above, including a desync bail that still
	/// returns partial entries. Callers use it to tell "readable database,
	/// entry genuinely absent" from "database can't vouch for anything".
	[[nodiscard]] static QVector<PmrEntry> parse(const QString &pmrFilePath, bool *ok = nullptr);

	// MARK: - Lookup tables

	using ProjectMap = QHash<QString, QVector<PmrEntry>>;

	/// Two filename-keyed lookups built in one pass:
	///
	///   `primary`  is the NFC-normalised, lower-cased filename.
	///   `fallback` is primary minus the extension, dots as underscores.
	///
	/// The fallback covers Avid's habit of renaming files on import,
	/// where `myclip.tail.mxf` on disk shows up as `myclip_tail` in
	/// the PMR. See `pmrkey.h` for the exact rules.
	struct ProjectMaps
	{
		ProjectMap primary;
		ProjectMap fallback;
	};

	/// Builds both lookup maps from a single PMR parse. `ok` as in parse().
	[[nodiscard]] static ProjectMaps buildFileMapWithFallback(const QString &pmrFilePath,
															  bool *ok = nullptr);
};