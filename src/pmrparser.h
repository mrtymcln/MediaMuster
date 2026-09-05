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
						 ///< V01/A01/A02 relatives of the same clip. Empty for
						 ///< version 1 (stored in the MOB database) or a null master.
	QString fileName;	 ///< From the UTF-8 record set when the PMR has one (MC 2025
						 ///< does); else the MacRoman set, decoded.
	QString project;	 ///< MBCS project text, decoded with the MacRoman/UTF-8
						 ///< compatibility policy. Not stored in version 1 records.
	/// The essence file's modification time when Avid indexed it, as the
	/// raw u32 Avid wrote (0 when absent). Two spellings exist — MC 2025
	/// writes Unix seconds UTC; older folders hold Mac 1904-epoch seconds in
	/// the writing machine's LOCAL time. PmrParser::trailerMatchesModified
	/// handles both spellings and Avid's exact one-hour clock exception. The
	/// scanner's staleness check: a file whose mtime no longer matches is
	/// one this record describes only by name, so its header is read instead
	/// of trusting the database's technical facts.
	quint32 fileModifiedSecs = 0;
};

// MARK: - PmrIndex

/// What a parsed `msmFMID.pmr` is, in one type: a filename-keyed lookup —
/// PmrKey::primary (NFC-normalised, lower-cased) onto the records carrying
/// that name. "Index" is the right word and belongs to the PMR specifically:
/// this is the ONLY Avid file in a media folder that ties a filename to its
/// MOBs. Its neighbour msmMMOB.mdb holds no filenames at all (see MdbParser).
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
using PmrIndex = QHash<QString, QVector<PmrEntry>>;

// MARK: - PmrParser

/// Reads the Persistent Media Record which Avid writes alongside media.
/// A flat filename-to-MobId index, consulted instead of walking every header.
///
/// Follows MC 26.8's recovered version branches: signed versions < 9,
/// with 8-byte OMF IDs through version 7 and AAF IDs in version 8. The
/// version-1 record omits project/master. The accepted 0/negative version
/// words share the OMF layout; this does not establish historical releases
/// of those versions. Either byte order is normalized into the same keys.
/// The optional version-16 Unicode section is a complete preferred set,
/// whose count and identities may differ from the first section.
class PmrParser
{
public:
	/// Read and parse the PMR at `pmrFilePath`. Returns an empty vector when
	/// the file is missing, too small or has an unsupported header. A malformed
	/// or truncated body can return recovery entries, including a final FILE
	/// whose master/timestamp was incomplete. A malformed Unicode set returns
	/// the MBCS recovery entries. The reason is logged to the lcPmr category.
	/// A complete 32-byte ID is preserved without inventing a prefix rule;
	/// an all-zero file identity is rejected and a null master remains empty.
	///
	/// `ok` (optional) reports whether the file parsed cleanly end to end:
	/// false on every failure above, including a truncation that still
	/// returns partial entries. Callers use it to tell "readable database,
	/// entry genuinely absent" from "database can't vouch for anything".
	[[nodiscard]] static QVector<PmrEntry> parse(const QString &pmrFilePath, bool *ok = nullptr);

	/// Does a PMR trailer agree with the file's modification time under the
	/// supported clock rules? Try Unix UTC seconds and Mac 1904-epoch seconds
	/// in this machine's local time at that instant. Either candidate matches
	/// within ±2 seconds (filesystem granularity), or EXACTLY ±3600 seconds
	/// (Avid's recovered clock exception; 3599 and 3601 do not match).
	/// A zero trailer or invalid date never matches. A writing-machine time
	/// zone difference outside these rules costs a header read; no arbitrary
	/// UTC offset is guessed.
	[[nodiscard]] static bool trailerMatchesModified(quint32 trailer, const QDateTime &onDisk);

	/// Builds the index from a single PMR parse. `ok` as in parse().
	[[nodiscard]] static PmrIndex buildFileMap(const QString &pmrFilePath, bool *ok = nullptr);
};
