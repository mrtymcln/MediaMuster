#pragma once

#include <QSet>
#include <QString>

// Pulls MOB IDs out of an Avid .avb bin file (a Bento container) by
// scanning for 32-byte MOB patterns. We never parse the full object
// graph — all we need for filtering is the set of referenced clips.

struct AvbBin
{
    QString filePath;
    QString displayName; // bin filename without .avb

    // Hex-formatted to match MobId::format: four groups of 16 hex chars,
    // dot-separated, lowercase. Holds both SMPTE (06 0E 2B 34 ...) and
    // Avid (06 0A 2B 34 ...) style MOBs.
    QSet<QString> mobIds;

    bool valid = false; // false on missing/truncated/not-an-AVB
};

class AvbParser
{
public:
    // Never throws; returns valid=false on any error.
    static AvbBin parse(const QString &avbFilePath);
};