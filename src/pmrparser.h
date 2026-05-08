#pragma once

#include <QString>
#include <QHash>
#include <QVector>

struct PmrEntry
{
    QString mobId;            // canonical hex form of the file MOB
    QString compositionMobId; // canonical hex form of the master clip MOB
                              // from the paired COMP; shared by V01/A01/A02 siblings
    QString fileName;
    QString project;
};

class PmrParser
{
public:
    static QVector<PmrEntry> parse(const QString &pmrFilePath);

    // Dual filename-keyed lookup, built in one pass:
    //   primary  — NFC-normalised, lowercased filename
    //   fallback — primary minus extension, dots → '_'
    using ProjectMap = QHash<QString, QVector<PmrEntry>>;
    struct ProjectMaps
    {
        ProjectMap primary;
        ProjectMap fallback;
    };
    static ProjectMaps buildFileMapWithFallback(const QString &pmrFilePath);
};