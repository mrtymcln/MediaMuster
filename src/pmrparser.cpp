#include "pmrparser.h"
#include "mobid.h"
#include "pmrkey.h"

#include <QFile>
#include <QDebug>
#include <QtEndian>

// msmFMID.pmr — is the Persistent Media Record written into every
// Avid MediaFiles/MXF folder. It is a flat Filename to MobId index, and the
// fastest way to map an essence file to its source/master Mobs without
// walking the MXF headers.
//
// Layout is reverse-engineered from Media Composer 2025, all integers LE:
//   Header (12 bytes):  uint32 sizeHint, uint32 version (=8), uint32 pairCount
//   Body (pairCount × 2 records, alternating FILE then COMP):
//     FILE: 32-byte MOB | uint16 nameLen | name (UTF-8) | uint16 projLen | project (UTF-8)
//     COMP: 32-byte MOB | 4 bytes (flags/refs — purpose unknown)
// The COMP MOB is the master clip. Sibling tracks (V01/A01/A02 etc)
// all reference the same COMP, which is how we group relatives.

namespace
{
    constexpr qint64 kHeaderSize = 12;
    constexpr qint64 kMobIdSize = MobId::kRawSize;
    constexpr qint64 kCompTrailerSize = 4;
    constexpr quint16 kMaxStringLen = 1024;
    constexpr quint32 kMaxPairCount = 5'000'000;

    template <typename T>
    T readLE(const QByteArray &data, qint64 offset)
    {
        if (offset < 0 || offset + qint64(sizeof(T)) > data.size())
            return T{};
        return qFromLittleEndian<T>(data.constData() + offset);
    }

    QString readString(const QByteArray &data, qint64 offset, qint64 length)
    {
        if (length <= 0 || offset < 0 || offset + length > data.size())
            return {};
        const char *begin = data.constData() + offset;
        const char *end = begin + length;
        while (begin < end && *begin == '\0')
            ++begin;
        const char *terminator = std::find(begin, end, '\0');
        return QString::fromUtf8(begin, terminator - begin);
    }
}

QVector<PmrEntry> PmrParser::parse(const QString &pmrFilePath)
{
    QFile file(pmrFilePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "PMR: cannot open" << pmrFilePath;
        return {};
    }
    const QByteArray data = file.readAll();

    if (data.size() < kHeaderSize + kMobIdSize)
    {
        qWarning() << "PMR: file too small" << data.size() << pmrFilePath;
        return {};
    }

    const quint32 pairCount = readLE<quint32>(data, 8);
    if (pairCount == 0 || pairCount > kMaxPairCount)
    {
        qWarning() << "PMR: implausible pair count" << pairCount << pmrFilePath;
        return {};
    }

    QVector<PmrEntry> entries;
    entries.reserve(pairCount);

    qint64 pos = kHeaderSize;
    int lastFileIdx = -1;
    const quint32 totalRecords = pairCount * 2;

    for (quint32 i = 0; i < totalRecords; ++i)
    {
        if (pos + kMobIdSize + 2 > data.size())
            break;

        const QString mobHex =
            MobId::format(reinterpret_cast<const unsigned char *>(data.constData() + pos));
        pos += kMobIdSize;

        const quint16 firstWord = readLE<quint16>(data, pos);

        // FILE records lead with a 1..1023-byte filename length; COMP records
        // lead with flag bytes that, in observed files, sit outside that range.
        const bool isFile = firstWord > 0 && firstWord < kMaxStringLen;

        if (isFile)
        {
            pos += 2;
            const quint16 nameLen = firstWord;
            if (pos + nameLen + 2 > data.size())
                break;

            QString fileName = readString(data, pos, nameLen);
            pos += nameLen;

            const quint16 projLen = readLE<quint16>(data, pos);
            pos += 2;

            QString project;
            if (projLen > 0 && projLen < kMaxStringLen && pos + projLen <= data.size())
            {
                project = readString(data, pos, projLen);
                pos += projLen;
            }

            if (fileName.isEmpty())
                continue;

            PmrEntry entry;
            entry.mobId = mobHex;
            entry.fileName = std::move(fileName);
            entry.project = std::move(project);
            entries.append(std::move(entry));
            lastFileIdx = entries.size() - 1;
        }
        else
        {
            // COMP record — attach Master Clip MOB to the previous FILE,
            // then drop the index so a stray COMP can't double-attach.
            pos += kCompTrailerSize;
            if (lastFileIdx >= 0)
            {
                entries[lastFileIdx].compositionMobId = mobHex;
                lastFileIdx = -1;
            }
        }
    }

    return entries;
}

PmrParser::ProjectMaps PmrParser::buildFileMapWithFallback(const QString &pmrFilePath)
{
    ProjectMaps maps;
    for (const auto &entry : parse(pmrFilePath))
    {
        const QString key = PmrKey::primary(entry.fileName);
        maps.primary[key].append(entry);
        maps.fallback[PmrKey::fallback(key)].append(entry);
    }
    return maps;
}