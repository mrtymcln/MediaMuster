#include "mdbparser.h"
#include "mobid.h"
#include <QFile>
#include <QSet>
#include <QStringList>
#include <QDebug>
#include <QRegularExpression>
#include <algorithm>

// msmMMOB.mdb is an AAF property-based structure (named properties:
// _ORG_BIN, _IMPORTSETTING, _COLUMN_START, _SRCFILE, UNC Path …); layout
// shifts between Avid versions so fixed-offset parsing is unreliable.
//
// Per-cluster parser: each distinct MOB ID gets a record built from strings
// within 1 KB of any occurrence. Exception: bin name uses a file-wide
// _ORG_BIN scan and picks the marker closest to the MOB's first occurrence
// (markers can sit more than 1 KB from their clip's first reference).
//
// Field sources:
//   clipName        — value after an AAF "Name" column label. Fallback:
//                     source basename with its media extension stripped
//                     ("my amazing clip 88.mov" > "my amazing clip 88").
//   bin             — bin name from the _ORG_BIN marker closest by byte
//                     offset to this MOB's first occurrence.
//   startTimecode   — value after _COLUMN_START, or any bare HH:MM:SS:FF
//                     match in the cluster.
//   sourceFilePath  — first path-shaped string in the cluster (offset order).
//   sourceFileName  — basename of sourceFilePath.
//   sourceContainer — first QTFF/MXF/MOV/… token in the cluster.
//   isImported      — true if any _IMPORTSETTING marker is in the cluster.

namespace
{

    constexpr qint64 kClusterWindow = 1024;
    // Bin name sits 7–20 bytes before the _ORG_BIN marker; 64 caps off AAF column noise.
    constexpr qint64 kBinAdjacency = 64;

    const QStringList kMediaExtensions = {
        ".mxf", ".wav", ".aif", ".aiff", ".bwf", ".omf", ".aaf",
        ".mov", ".mp4", ".avi", ".mkv", ".jpeg", ".jpg", ".png", ".tif",
        ".tiff", ".dpx", ".exr", ".bmp", ".gif", ".heic", ".heif",
        ".r3d", ".braw", ".arri", ".cin"};

    const QString kMarkerImportSetting = QStringLiteral("_IMPORTSETTING");
    const QString kMarkerOrgBin = QStringLiteral("_ORG_BIN");
    const QString kMarkerNameCol = QStringLiteral("Name");
    const QString kMarkerStartCol = QStringLiteral("_COLUMN_START");

    const QSet<QString> kContainerHints = {
        QStringLiteral("QTFF"), QStringLiteral("MXF"), QStringLiteral("MOV"),
        QStringLiteral("AVI"), QStringLiteral("MP4"), QStringLiteral("WAV"),
        QStringLiteral("AIFF")};

    const QRegularExpression kTimecodeRe(
        QStringLiteral("^\\d{2}:\\d{2}:\\d{2}[:;]\\d{2}$"));

    // Strip the leading 'z' (0x7A) AAF sometimes prefixes to string properties as a type tag.
    QString stripZPrefix(const QString &s)
    {
        if (s.length() > 3 && s[0] == QLatin1Char('z'))
        {
            const QChar next = s[1];
            if (next.isUpper() || next.isDigit() || next == QLatin1Char('_'))
                return s.mid(1);
        }
        return s;
    }

    bool looksLikeSourcePath(const QString &s)
    {
        if (s.startsWith(QStringLiteral("/Users/")) ||
            s.startsWith(QStringLiteral("/Volumes/")))
            return true;
        if (s.startsWith(QStringLiteral("\\\\")))
            return true;
        if (s.length() >= 3 && s[1] == QLatin1Char(':') &&
            (s[2] == QLatin1Char('\\') || s[2] == QLatin1Char('/')))
            return true;
        return false;
    }

    bool looksLikeHumanLabel(const QString &s)
    {
        if (s.contains(QLatin1Char('/')) || s.contains(QLatin1Char('\\')))
            return false;
        if (s.startsWith(QLatin1Char('_')) || s.startsWith(QLatin1Char('&')))
            return false;
        if (s.startsWith(QStringLiteral("omfi:")) ||
            s.startsWith(QStringLiteral("OMFI:")))
            return false;
        if (s.startsWith(QStringLiteral("ATN_")) ||
            s.startsWith(QStringLiteral("0x")))
            return false;
        if (s.length() < 2 || s.length() > 128)
            return false;
        for (QChar c : s)
        {
            if (c.isLetter())
                return true;
        }
        return false;
    }

    QString stripMediaExtension(const QString &name)
    {
        const QString lower = name.toLower();
        for (const QString &ext : kMediaExtensions)
        {
            if (lower.endsWith(ext))
                return name.left(name.length() - ext.length());
        }
        return name;
    }

    QString pathBasename(const QString &path)
    {
        const int slash = qMax(path.lastIndexOf(QLatin1Char('/')),
                               path.lastIndexOf(QLatin1Char('\\')));
        return (slash >= 0) ? path.mid(slash + 1) : path;
    }

    struct ExtractedString
    {
        qint64 offset;
        QString value;
    };

    [[nodiscard]] QVector<ExtractedString> extractAllStrings(const QByteArray &data, int minLen = 3)
    {
        const auto isPrintable = [](uchar c) { return c >= 0x20 && c < 0x7F; };

        QVector<ExtractedString> out;
        out.reserve(2048);
        const qint64 n = data.size();
        const char *ptr = data.constData();
        qint64 i = 0;
        while (i < n)
        {
            if (!isPrintable(static_cast<uchar>(ptr[i])))
            {
                ++i;
                continue;
            }
            const qint64 start = i;
            while (i < n && isPrintable(static_cast<uchar>(ptr[i])))
                ++i;
            if (const int len = int(i - start); len >= minLen)
                out.append({start, QString::fromLatin1(ptr + start, len)});
        }
        return out;
    }

    struct OrgBinMarker
    {
        qint64 offset;   // byte offset of the _ORG_BIN string
        QString binName; // associated bin name
    };

    [[nodiscard]] QVector<OrgBinMarker> findOrgBinMarkers(const QVector<ExtractedString> &strings)
    {
        QVector<OrgBinMarker> out;
        for (int i = 0; i < strings.size(); ++i)
        {
            if (strings[i].value != kMarkerOrgBin)
                continue;
            const qint64 markerOff = strings[i].offset;
            QString binName;
            // Gate by byte distance, not list index.
            for (int j = i - 1; j >= 0 && j >= i - 20; --j)
            {
                const qint64 prevEnd = strings[j].offset + strings[j].value.length();
                if (markerOff - prevEnd > kBinAdjacency)
                    break;
                const QString cand = stripZPrefix(strings[j].value);
                if (!looksLikeHumanLabel(cand))
                    continue;
                // Duplicates are fine — key and value carry the same text.
                binName = cand;
            }
            if (!binName.isEmpty())
                out.append({markerOff, binName});
        }
        return out;
    }

    [[nodiscard]] QVector<qint64> findMobOffsets(const QByteArray &data)
    {
        QVector<qint64> out;
        const qint64 n = data.size();
        const auto *ptr = reinterpret_cast<const uchar *>(data.constData());
        for (qint64 i = 0; i + 32 <= n;)
        {
            if (ptr[i] == 0x06 && ptr[i + 1] == 0x0a &&
                ptr[i + 2] == 0x2b && ptr[i + 3] == 0x34)
            {
                out.append(i);
                i += 32;
            }
            else
            {
                ++i;
            }
        }
        return out;
    }

    // ±kClusterWindow window around every MOB occurrence, dedup'd by offset.
    [[nodiscard]] QVector<ExtractedString> buildCluster(
        const QVector<qint64> &mobOffsets,
        const QVector<ExtractedString> &strings)
    {
        QSet<qint64> seen;
        QVector<ExtractedString> cluster;
        cluster.reserve(64);
        for (qint64 off : mobOffsets)
        {
            const qint64 lo = off - kClusterWindow;
            const qint64 hi = off + kClusterWindow;
            // Binary search to jump to the window start — strings is offset-sorted.
            const auto start = std::lower_bound(strings.cbegin(), strings.cend(), lo,
                                                [](const ExtractedString &es, qint64 val)
                                                { return es.offset < val; });
            for (auto it = start; it != strings.cend() && it->offset <= hi; ++it)
            {
                if (!seen.contains(it->offset))
                {
                    seen.insert(it->offset);
                    cluster.append(*it);
                }
            }
        }
        std::sort(cluster.begin(), cluster.end(),
                  [](const ExtractedString &a, const ExtractedString &b)
                  { return a.offset < b.offset; });
        return cluster;
    }

    [[nodiscard]] MdbRecord extractMobRecord(
        const QByteArray &rawMobId,
        const QString &mobHex,
        const QVector<qint64> &mobOccurrences,
        const QVector<ExtractedString> &allStrings,
        const QVector<OrgBinMarker> &orgBinMarkers)
    {
        MdbRecord rec;
        rec.mobId = rawMobId;
        rec.mobIdHex = mobHex;

        const QVector<ExtractedString> cluster = buildCluster(mobOccurrences, allStrings);

        // One pass: mark import, harvest Name and _COLUMN_START label > value pairs.
        for (int i = 0; i < cluster.size(); ++i)
        {
            const QString &raw = cluster[i].value;

            if (raw == kMarkerImportSetting)
            {
                rec.isImported = true;
            }
            else if (raw == kMarkerNameCol && rec.clipName.isEmpty() && i + 1 < cluster.size())
            {
                const QString nxt = stripZPrefix(cluster[i + 1].value);
                if (nxt != kMarkerNameCol && !nxt.startsWith(QLatin1Char('_')) && looksLikeHumanLabel(nxt))
                    rec.clipName = nxt;
            }
            else if (raw == kMarkerStartCol && rec.startTimecode.isEmpty() && i + 1 < cluster.size())
            {
                const QString &nxt = cluster[i + 1].value;
                if (kTimecodeRe.match(nxt).hasMatch())
                    rec.startTimecode = nxt;
            }
        }

        // Start timecode fallback: bare HH:MM:SS:FF regex match anywhere.
        if (rec.startTimecode.isEmpty())
        {
            for (const ExtractedString &es : cluster)
            {
                if (kTimecodeRe.match(es.value).hasMatch())
                {
                    rec.startTimecode = es.value;
                    break;
                }
            }
        }

        // Source file path: first path-looking string in the cluster by offset.
        for (const ExtractedString &es : cluster)
        {
            const QString s = stripZPrefix(es.value);
            if (looksLikeSourcePath(s))
            {
                rec.sourceFilePath = s;
                rec.sourceFileName = pathBasename(s);
                break;
            }
        }

        // Clip name fallback: stripped source file basename.
        if (rec.clipName.isEmpty() && !rec.sourceFileName.isEmpty())
            rec.clipName = stripMediaExtension(rec.sourceFileName);

        // Bin name: closest _ORG_BIN marker to the MOB's first occurrence.
        if (!orgBinMarkers.isEmpty() && !mobOccurrences.isEmpty())
        {
            const qint64 firstOcc = *std::min_element(mobOccurrences.cbegin(), mobOccurrences.cend());
            const OrgBinMarker *best = nullptr;
            qint64 bestDist = 0;
            for (const OrgBinMarker &m : orgBinMarkers)
            {
                const qint64 d = qAbs(m.offset - firstOcc);
                if (!best || d < bestDist)
                {
                    best = &m;
                    bestDist = d;
                }
            }
            if (best)
                rec.bin = best->binName;
        }

        // Source container: first known token in cluster.
        for (const ExtractedString &es : cluster)
        {
            if (kContainerHints.contains(es.value))
            {
                rec.sourceContainer = es.value;
                break;
            }
        }

        return rec;
    }

}

QVector<MdbRecord> MdbParser::parse(const QString &mdbFilePath)
{
    QVector<MdbRecord> records;

    QFile file(mdbFilePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "MDB: Cannot open" << mdbFilePath;
        return records;
    }
    const QByteArray data = file.readAll();
    file.close();

    if (data.size() < 64)
        return records;

    const QVector<qint64> mobOffsets = findMobOffsets(data);
    const QVector<ExtractedString> strings = extractAllStrings(data, 3);
    const QVector<OrgBinMarker> orgBins = findOrgBinMarkers(strings);

    qDebug() << "MDB:" << mdbFilePath
             << "size:" << data.size()
             << "mobs:" << mobOffsets.size()
             << "strings:" << strings.size()
             << "_ORG_BIN markers:" << orgBins.size();
    for (const OrgBinMarker &m : orgBins)
        qDebug() << "  _ORG_BIN @" << m.offset << ":" << m.binName;

    QHash<QString, QVector<qint64>> mobOccurrences;
    QHash<QString, QByteArray> mobRawBytes;
    for (qint64 off : mobOffsets)
    {
        const QByteArray mob = data.mid(qsizetype(off), 32);
        const QString hex = MobId::format(mob);
        mobOccurrences[hex].append(off);
        if (!mobRawBytes.contains(hex))
            mobRawBytes.insert(hex, mob);
    }

    records.reserve(mobOccurrences.size());
    for (auto it = mobOccurrences.cbegin(); it != mobOccurrences.cend(); ++it)
    {
        records.append(extractMobRecord(
            mobRawBytes.value(it.key()), it.key(), it.value(), strings, orgBins));
    }

    qDebug() << "MDB: built" << records.size() << "per-MOB records";
    return records;
}

MdbParser::RecordMap MdbParser::buildMobMap(const QString &mdbFilePath)
{
    RecordMap map;
    const auto records = parse(mdbFilePath);
    map.reserve(records.size());
    for (const auto &r : records)
    {
        if (!r.mobIdHex.isEmpty())
            map.insert(r.mobIdHex, r);
    }
    return map;
}