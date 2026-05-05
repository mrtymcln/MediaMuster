#include "pmrparser.h"
#include "mobid.h"
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QtEndian>

// PMR File Format (reverse-engineered from Media Composer 2024)
//
// All integers are LITTLE-ENDIAN.
//
// Header (12 bytes):
//   uint32 LE: related to file/data size
//   uint32 LE: version or format indicator (observed: 8)
//   uint32 LE: pair_count: number of FILE+COMP pairs
//
// Records (pair_count × 2 entries, alternating):
//
//   FILE record:
//     32 bytes: MOB ID (SMPTE UMID: source/file mob)
//     uint16 LE: filename string length
//     N bytes: filename (UTF-8, NOT null-terminated)
//     uint16 LE: project name string length
//     M bytes: project name (UTF-8, NOT null-terminated)
//
//   COMP record (composition/material mob):
//     32 bytes: MOB ID (SMPTE UMID: material mob)
//     4 bytes: flags or reference data (purpose unknown)
//
// The file may contain a second section repeating the same data
// with uint32 LE string lengths instead of uint16.

static quint16 readU16LE(const QByteArray &d, qint64 o)
{
    if (o + 2 > d.size())
        return 0;
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(d.constData() + o));
}

static quint32 readU32LE(const QByteArray &d, qint64 o)
{
    if (o + 4 > d.size())
        return 0;
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(d.constData() + o));
}

QString PmrParser::readString(const QByteArray &data, qint64 offset, int length)
{
    if (length <= 0 || offset + length > data.size())
        return {};
    QByteArray raw = data.mid(static_cast<qsizetype>(offset), length);
    // Strip any leading/trailing null bytes
    while (!raw.isEmpty() && raw.at(0) == '\0')
        raw.remove(0, 1);
    int end = static_cast<int>(raw.indexOf('\0'));
    if (end >= 0)
        raw.truncate(end);
    return QString::fromUtf8(raw);
}

QString PmrParser::formatMobId(const QByteArray &raw)
{
    return MobId::format(raw);
}

QVector<PmrEntry> PmrParser::parse(const QString &pmrFilePath)
{
    QVector<PmrEntry> entries;

    QFile file(pmrFilePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "PMR: Cannot open" << pmrFilePath;
        return entries;
    }

    QByteArray data = file.readAll();
    file.close();

    if (data.size() < 16)
    {
        qWarning() << "PMR: File too small" << data.size();
        return entries;
    }

    // 12-byte header, LE.
    quint32 word0 = readU32LE(data, 0);
    quint32 word1 = readU32LE(data, 4); // version/format (typically 8)
    quint32 pairCount = readU32LE(data, 8);

    qDebug() << "PMR:" << pmrFilePath;
    qDebug() << "  Header: word0=" << word0 << "word1=" << word1
             << "pairCount=" << pairCount << "fileSize=" << data.size();

    if (pairCount == 0 || pairCount > 5000000)
    {
        qWarning() << "PMR: Suspicious pair count" << pairCount;
        return entries;
    }

    // Pair = FILE + COMP. The COMP MOB attaches to the most recent FILE so siblings
    // (V01 + A01 + A02 of one AMA transcode) group on it downstream.
    qint64 pos = 12;
    quint32 totalRecords = pairCount * 2;
    int lastFileEntryIdx = -1;

    for (quint32 i = 0; i < totalRecords && pos + 34 < data.size(); ++i)
    {
        // Every record starts with a 32-byte MOB ID
        if (pos + 32 > data.size())
            break;
        QByteArray mob = data.mid(static_cast<qsizetype>(pos), 32);
        pos += 32;

        // Read 2-byte LE value to determine record type
        if (pos + 2 > data.size())
            break;
        quint16 firstWord = readU16LE(data, pos);

        if (firstWord > 0 && firstWord < 1024)
        {
            // FILE record: filename + project name.
            pos += 2; // filename length
            quint16 fnLen = firstWord;

            if (pos + fnLen > data.size())
                break;
            QString fileName = readString(data, pos, fnLen);
            pos += fnLen;

            // Read project name
            if (pos + 2 > data.size())
                break;
            quint16 projLen = readU16LE(data, pos);
            pos += 2;

            QString project;
            if (projLen > 0 && projLen < 1024 && pos + projLen <= data.size())
            {
                project = readString(data, pos, projLen);
                pos += projLen;
            }

            if (!fileName.isEmpty())
            {
                PmrEntry entry;
                entry.mobId = mob;
                entry.mobIdHex = formatMobId(mob);
                entry.fileName = fileName;
                entry.project = project;
                entries.append(entry);
                lastFileEntryIdx = entries.size() - 1;
            }
        }
        else
        {
            // COMP record: attach to last FILE, then clear so we don't double-attach.
            pos += 4;
            if (lastFileEntryIdx >= 0 &&
                lastFileEntryIdx < entries.size())
            {
                entries[lastFileEntryIdx].compositionMobId = mob;
                entries[lastFileEntryIdx].compositionMobIdHex = formatMobId(mob);
                lastFileEntryIdx = -1;
            }
        }
    }

    return entries;
}


PmrParser::ProjectMaps PmrParser::buildFileMapWithFallback(const QString &pmrFilePath)
{
    ProjectMaps maps;
    auto entries = parse(pmrFilePath);

    // Fallback key: strip extension, dots → '_', lowercase. Catches Avid's
    // recorded basename differing from the on-disk filename in case/punctuation.
    auto fallbackKey = [](const QString &name) -> QString
    {
        const int lastDot = name.lastIndexOf('.');
        QString base = (lastDot > 0) ? name.left(lastDot) : name;
        return base.replace('.', '_').toLower();
    };

    for (const auto &e : entries)
    {
        const QString primaryKey =
            e.fileName.normalized(QString::NormalizationForm_C).toLower();
        maps.primary[primaryKey].append(e);
        maps.fallback[fallbackKey(primaryKey)].append(e);
    }
    return maps;
}
