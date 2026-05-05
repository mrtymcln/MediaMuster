#pragma once

#include <QString>
#include <QHash>
#include <QByteArray>
#include <QVector>

// Reads msmFMID.pmr (file → MOB-ID database). Reverse-engineered from
// Media Composer 2024: little-endian, alternating FILE/COMP record pairs.

struct PmrEntry
{
    QByteArray mobId;            // raw 32-byte file MOB
    QString mobIdHex;
    QByteArray compositionMobId; // master-clip MOB from the paired COMP
                                 // record; shared by V01/A01/A02 siblings
    QString compositionMobIdHex;
    QString fileName;
    QString project;
    QString bin;
};

class PmrParser
{
public:
    static QVector<PmrEntry> parse(const QString &pmrFilePath);

    // Dual filename-keyed lookup, built in one pass:
    //   primary  — NFC-normalised, lowercased filename
    //   fallback — primary minus extension, dots → '_'
    // QHash because lookup is hot (was ~30% of scan time as a QMap on
    // 10k+ file scans) and we don't need ordered iteration.
    using ProjectMap = QHash<QString, QVector<PmrEntry>>;
    struct ProjectMaps
    {
        ProjectMap primary;
        ProjectMap fallback;
    };
    static ProjectMaps buildFileMapWithFallback(const QString &pmrFilePath);

private:
    static QString readString(const QByteArray &data, qint64 offset, int length);
    static QString formatMobId(const QByteArray &raw);
};