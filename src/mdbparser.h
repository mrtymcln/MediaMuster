#pragma once

#include <QString>
#include <QHash>
#include <QVector>
#include <QByteArray>

struct MdbRecord
{
    QByteArray mobId;      // 32-byte raw MOB ID
    QString mobIdHex;      // Human-readable hex
    QString clipName;      // Clip name as shown in the Avid bin
    QString bin;           // Original import bin name (_ORG_BIN marker).
                           // Mapped to MediaFile::originalBin downstream.
    QString startTimecode; // Start TC e.g. "09:59:50:00"

    // Rich import / Avid Media Access metadata. Extracted file-wide from the MDB and
    // copied into every record produced by that MDB. When isImported is
    // true, sourceFilePath is the path Avid recorded when the media was
    // first brought into the bin.
    QString sourceFilePath;  // e.g. "/Users/.../Desktop/my amazing clip 88.mov"
    QString sourceFileName;  // Basename of sourceFilePath
    QString sourceContainer; // "QTFF", "MXF", "MOV", etc.
    bool isImported = false;
};

class MdbParser
{
public:
    [[nodiscard]] static QVector<MdbRecord> parse(const QString &mdbFilePath);

    using RecordMap = QHash<QString, MdbRecord>;
    [[nodiscard]] static RecordMap buildMobMap(const QString &mdbFilePath);
};