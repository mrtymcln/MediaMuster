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
	QString fileName;	 ///< From the UTF-8 record set when present; otherwise decoded from MBCS.
	QString project;	 ///< MBCS project text, decoded with the MacRoman/UTF-8
						 ///< compatibility policy. Not stored in version 1 records.
	/// Modification time when indexed, as Unix UTC or Mac 1904-epoch local
	/// seconds; zero means absent. PmrParser::trailerMatchesModified handles
	/// both forms and Avid's one-hour clock exception. A mismatch requires a
	/// header read to check the database metadata.
	quint32 fileModifiedSecs = 0;
};

// MARK: - PmrIndex

/// PMR records keyed by PmrKey::primary: NFC-normalised, lower-case filenames.
/// Names retain their punctuation and extension. The PMR links filenames to
/// MOB IDs; the MDB supplies metadata for those IDs.
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
	/// returns partial entries, an unsupported extension or unconsumed bytes.
	/// Success requires EOF after the base set or its optional version-16
	/// Unicode set; their record counts need not match. Callers use it to tell "readable database,
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
