#pragma once

#include <QDateTime>
#include <QHash>
#include <QString>
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
	/// The essence file's modification time when Avid indexed it, as the
	/// raw u32 Avid wrote (0 when absent). Two spellings exist — MC 2025
	/// writes Unix seconds UTC; older folders hold Mac 1904-epoch seconds in
	/// the writing machine's LOCAL time — so compare through
	/// PmrParser::trailerMatchesModified, never by subtraction. The
	/// scanner's staleness check: a file whose mtime no longer matches is
	/// one this record describes only by name, so its header is read instead
	/// of trusting the database's technical facts.
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

	/// Does a PMR trailer name the same instant as a file's modification
	/// time? True when the trailer reads as Unix UTC seconds within ±2 s of
	/// `onDisk` (MC 2025), OR as Mac 1904-epoch seconds in this machine's
	/// local time at that instant (older MC; measured on a 2018–19 folder:
	/// every trailer = mtime + 2,082,844,800 + the AEST/AEDT offset). A
	/// trailer of 0 is "unknown" and matches nothing. A PMR written in another
	/// time zone won't match the second form — which only costs a header read.
	[[nodiscard]] static bool trailerMatchesModified(quint32 trailer, const QDateTime &onDisk);

	// MARK: - Lookup tables

	/// Filename-keyed lookup: PmrKey::primary (NFC-normalised, lower-cased)
	/// onto the records carrying that name.
	///
	/// ONE key, not two. A looser second key - extension dropped, remaining
	/// dots turned to underscores - was carried from the prototype on the
	/// belief that Avid renames files on import. It does not: an import
	/// produces a NEW file whose name Avid generates from the track and MOB
	/// (`A01.E6968417_1BD321BD32270A.mxf`), and the source name is kept in a
	/// separate field, so there is no spelling to reconcile. Measured over
	/// 2,412 real files in four projects - 2,298 of them imports, and 2,397
	/// carrying the dotted shape the loose key existed to repair - it matched
	/// nothing the exact name had missed. Removed 2026-08-28.
	using ProjectMap = QHash<QString, QVector<PmrEntry>>;

	/// Builds the lookup map from a single PMR parse. `ok` as in parse().
	[[nodiscard]] static ProjectMap buildFileMap(const QString &pmrFilePath, bool *ok = nullptr);
};