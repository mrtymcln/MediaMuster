#pragma once

#include <QString>
#include <QHash>
#include <QVector>
#include <QByteArray>

// MdbParser — reads Avid msmMMOB.mdb (Media MOB Object) database files
// These files live in each numbered MXF subfolder:
//   <drive>/Avid MediaFiles/MXF/<N>/msmMMOB.mdb
//
// They contain metadata records for each media file in that folder:
//   - MOB ID (links to PMR entries and MXF file UMIDs)
//   - Clip name / tape name
//   - Codec identifier
//   - Resolution, frame rate, bit depth
//   - File associations
//
// NOTE: This is NOT a Microsoft Access .mdb file. It is AVID's own
// proprietary binary format.

struct MdbRecord
{
    QByteArray mobId;      // 32-byte raw MOB ID
    QString mobIdHex;      // Human-readable hex
    QString clipName;      // Clip name as shown in the Avid bin
    QString bin;           // Original import bin name (_ORG_BIN marker).
                           // Mapped to MediaFile::originalBin downstream.
    QString startTimecode; // Start TC e.g. "09:59:44:00"

    // Rich import / AMA metadata. Extracted file-wide from the MDB and
    // copied into every record produced by that MDB. When isImported is
    // true, sourceFilePath is the path Avid recorded when the media was
    // first brought into the bin (typically the original QuickTime or
    // camera file before transcoding).
    QString sourceFilePath;  // e.g. "/Users/.../RAY 80.mov"
    QString sourceFileName;  // Basename of sourceFilePath
    QString sourceContainer; // "QTFF", "MXF", "MOV", etc.
    bool isImported = false;
};

class MdbParser
{
public:
    [[nodiscard]] static QVector<MdbRecord> parse(const QString &mdbFilePath);

    // QHash, not QMap: O(1) lookup, no need for ordered iteration.
    using RecordMap = QHash<QString, MdbRecord>;
    [[nodiscard]] static RecordMap buildMobMap(const QString &mdbFilePath);
};