#pragma once

#include <QString>
#include <QHash>
#include <QVector>
#include <QByteArray>

// MARK: - MdbRecord

/// One record per MOB, built from a parsed `msmMMOB.mdb`. The MDB is
/// Avid's per-folder bookkeeping for the MXF files in that folder.
/// We extract what we need then feed it into MediaFile downstream.
struct MdbRecord
{
	QByteArray mobId;		 ///< 32-byte raw MOB ID.
	QString mobIdHex;		 ///< Same MOB in canonical hex form (see mobid.h).
	QString clipName;		 ///< The record's own name, read from its header.
							 ///< Exact: 360/360 against MaterialPackage names on
							 ///< a real folder. Ranked below the MaterialPackage
							 ///< name — see MediaFile::ClipNameSource.
	QString bin;			 ///< Original import bin name (_ORG_BIN).
							 ///< Mapped to MediaFile::originalBin downstream.
	QString sourceFilePath;	 ///< Path Avid recorded when the media was first imported,
							 ///< e.g. '/Users/.../Desktop/my amazing clip 88.mov'.
	QString sourceFileName;	 ///< Basename of sourceFilePath.
	QString sourceContainer; ///< 'QTFF', 'MXF', 'MOV', etc.
	bool isImported = false;
};

// MARK: - MdbParser

/// Reads `msmMMOB.mdb`, the per-folder Avid media database.
///
/// Not Bento (that is `.avb`) and not AAF: Avid's own flat object stream,
/// where each named object ends with a fixed header closed by its 32-byte
/// MOB ID, and an object's attributes are written before it. The framing is
/// self-checking — a member count that must match its handles and land exactly
/// on the MOB — so records are recognised outright, never by proximity.
/// See `mdbparser.cpp` for the byte layout and how it was verified.
class MdbParser
{
public:
	/// Parse the MDB at `mdbFilePath`. Returns an empty vector on
	/// any failure (missing, truncated, unreadable).
	///
	/// `ok` (optional) is false when the file couldn't be opened, is too
	/// small to be a real MDB, or isn't recognisably one — no Avid
	/// property-dictionary fingerprint near the front AND no MOB ID
	/// anywhere (see the validity gate in the .cpp). Note the weaker
	/// guarantee than the PMR's: the MDB is mined by marker scanning, so a
	/// damaged-but-recognisable file may still "parse" to few or zero
	/// records with ok=true. ok=false is proof of failure; ok=true is not
	/// proof of integrity.
	[[nodiscard]] static QVector<MdbRecord> parse(const QString &mdbFilePath, bool *ok = nullptr);

	/// MOB-hex-keyed lookup table, for joining against PMR entries
	/// and MediaFile rows during the scan.
	using RecordMap = QHash<QString, MdbRecord>;

	/// Build the lookup map directly. Equivalent to calling `parse`
	/// and indexing the result by `mobIdHex`. `ok` as in parse().
	[[nodiscard]] static RecordMap buildMobMap(const QString &mdbFilePath, bool *ok = nullptr);
};