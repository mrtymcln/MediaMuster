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
	QByteArray mobId;      ///< 32-byte raw MOB ID.
	QString mobIdHex;      ///< Same MOB in canonical hex form (see mobid.h).
	QString clipName;      ///< Clip name as displayed in the Avid bin.
	QString bin;           ///< Original import bin name (_ORG_BIN marker).
	                       ///< Mapped to MediaFile::originalBin downstream.
	QString startTimecode; ///< Start TC e.g. "09:59:50:00".
	QString sourceFilePath;  ///< Path Avid recorded when the media was first imported,
	                         ///< e.g. "/Users/.../Desktop/my amazing clip 88.mov".
	QString sourceFileName;  ///< Basename of sourceFilePath.
	QString sourceContainer; ///< "QTFF", "MXF", "MOV", etc.
	bool isImported = false;
};

// MARK: - MdbParser

/// Reads `msmMMOB.mdb`, the per-folder Avid media database.
/// AAF property-based with named markers (`_ORG_BIN`, `Name`,
/// `_COLUMN_START`, etc.) — layout shifts between Avid versions.
/// See `mdbparser.cpp` for the extraction strategy.
class MdbParser
{
public:
	/// Parse the MDB at `mdbFilePath`. Returns an empty vector on
	/// any failure (missing, truncated, unreadable).
	[[nodiscard]] static QVector<MdbRecord> parse(const QString &mdbFilePath);

	/// MOB-hex-keyed lookup table, for joining against PMR entries
	/// and MediaFile rows during the scan.
	using RecordMap = QHash<QString, MdbRecord>;

	/// Build the lookup map directly. Equivalent to calling `parse`
	/// and indexing the result by `mobIdHex`.
	[[nodiscard]] static RecordMap buildMobMap(const QString &mdbFilePath);
};