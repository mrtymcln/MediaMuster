#include "avbparser.h"
#include "mobid.h"

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <cstring>
#include <utility>

// AVB header layout (first ~20 bytes):
// 06 00 "DomainDJBO"  07 00 "AObjDoc"  04 13 00  <YYYY/MM/DD HH:MM:SS>
//
// After the prelude the file is a Bento container stream. The full object
// graph is not parsed here — we only need the MOB ID.
//
// MOB IDs appear in two forms:
//
//   1. Binary — 32-byte runs in the byte stream:
//         SMPTE UMID   06 0E 2B 34 04 01 ...   (32 bytes)
//         Avid MOB     06 0A 2B 34 01 01 0F ... (32 bytes)
//
//   2. ASCII hex-string — same underlying 32 bytes, serialised with dashes
//      at field boundaries, e.g.:
//         "060a2b340101010001010f0013-000000-6b0fb74dc6fa687b-..."
//      64 hex chars after stripping dashes.
//
// Endian mismatch:
//   Bytes 16..23 hold a (uint32, uint16, uint16) triple.
//   AVB files store these little-endian; PMR files store them big-endian.
//   The same logical MOB therefore has two distinct 32-byte representations.
//
//   Example of the AVB ↔ PMR endian flip ("RAY 80 ama" composition MOB):
//     Bin : 060a2b3401010105 01010f1013000000 a4bb7f13 1139 9006 6d01ce4ff0f5d57a
//     PMR : 060a2b3401010105 01010f1013000000 137fbba4 3911 0690 6d01ce4ff0f5d57a
//                                             [u32]    [u16][u16]
//
//   Both variants are stored in mobIds so lookups against MediaFile::mobId
//   and MediaFile::compositionMobId work regardless of source.
//
// Bento sentinels share the 06 0? 2B 34 prefix (e.g. 06 0E 2B 34 7F 7F 2A 80)
// and are excluded by the byte-4/12/20 checks in the validators below.

namespace
{
    constexpr int kMobIdLen = 32;

    bool isValidSmpteUmid(const uchar *p)
    {
        // Prefix already matched: p[0..3] == 06 0E 2B 34
        return p[4] == 0x04 && p[5] == 0x01;
    }

    bool isValidAvidMob(const uchar *p)
    {
        // Prefix already matched: p[0..3] == 06 0A 2B 34
        return p[4] == 0x01 && p[12] == 0x44 && p[20] == 0x48;
    }

    // Reverses the u32 at bytes 16..19 and the two u16s at 20..21 and 22..23
    // to convert between AVB (little-endian) and PMR (big-endian) mob form.
    void swapMobMiddleFields(const uchar *in, uchar *out)
    {
        std::memcpy(out, in, kMobIdLen);
        std::swap(out[16], out[19]);
        std::swap(out[17], out[18]);
        std::swap(out[20], out[21]);
        std::swap(out[22], out[23]);
    }

    void insertBothForms(QSet<QString> &out, const uchar *raw32)
    {
        out.insert(MobId::format(raw32));
        uchar swapped[kMobIdLen];
        swapMobMiddleFields(raw32, swapped);
        out.insert(MobId::format(swapped));
    }

    // Strips dashes and decodes a hex-string MOB like
    //   "060a2b340101010001010f0013-000000-6b0fb74dc6fa687b-a68db556e126-6e62"
    // into 32 raw bytes. Returns false if the result isn't exactly 64 hex chars.
    bool decodeHexStringMob(const QByteArray &hexWithDashes, uchar (&out)[kMobIdLen])
    {
        QByteArray clean;
        clean.reserve(hexWithDashes.size());
        for (char c : hexWithDashes)
        {
            if (c != '-')
                clean.append(c);
        }
        if (clean.size() != 64)
            return false;
        const QByteArray raw = QByteArray::fromHex(clean);
        std::memcpy(out, raw.constData(), kMobIdLen);
        return true;
    }
} // namespace

AvbBin AvbParser::parse(const QString &avbFilePath)
{
    AvbBin result;
    result.filePath = avbFilePath;

    QFileInfo fi(avbFilePath);
    // preserves fullstops in the filename (e.g. "director.cuts.avb" = "director.cuts")
    result.displayName = fi.completeBaseName();

    QFile file(avbFilePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "AVB: cannot open" << avbFilePath << file.errorString();
        return result;
    }

    // Bins are small so 64 MB is a sanity cap.
    static constexpr qint64 kMaxBinBytes = 64LL * 1024 * 1024;
    if (file.size() > kMaxBinBytes)
    {
        qWarning() << "AVB: file too large, refusing to parse" << avbFilePath
                   << file.size() << "bytes";
        return result;
    }

    const QByteArray buf = file.readAll();
    file.close();

    // Header check; otherwise we'd hunt MOB patterns in unrelated files.
    static const QByteArray kAvbHeader =
        QByteArray::fromRawData("\x06\x00"
                                "DomainDJBO",
                                12);
    if (!buf.startsWith(kAvbHeader))
    {
        qWarning() << "AVB: not an Avid bin (header mismatch)" << avbFilePath;
        return result;
    }

    const uchar *data = reinterpret_cast<const uchar *>(buf.constData());
    const qint64 size = buf.size();

    // pass 1: binary 32-byte MOB IDs
    const qint64 scanEnd = size - kMobIdLen;
    qint64 i = 0;
    while (i <= scanEnd)
    {
        if (data[i] != 0x06 || data[i + 2] != 0x2B || data[i + 3] != 0x34)
        {
            ++i;
            continue;
        }

        const uchar b1 = data[i + 1];
        const uchar *p = data + i;
        if (b1 == 0x0E && isValidSmpteUmid(p))
        {
            insertBothForms(result.mobIds, p);
            i += kMobIdLen;
            continue;
        }
        if (b1 == 0x0A && isValidAvidMob(p))
        {
            insertBothForms(result.mobIds, p);
            i += kMobIdLen;
            continue;
        }
        ++i;
    }

    // pass 2: ASCII hex-string MOB IDs (same form PMR records use). Match the
    // prefix then up to 68 chars of [0-9a-f-]; decoder validates after dashes are stripped.
    static const QRegularExpression kHexStringMobRe(
        QStringLiteral("06(?:0a|0e)2b34[0-9a-f-]{56,68}"));
    const QString asLatin1 = QString::fromLatin1(buf);
    auto it = kHexStringMobRe.globalMatch(asLatin1);
    while (it.hasNext())
    {
        const QRegularExpressionMatch m = it.next();
        uchar raw[kMobIdLen];
        if (decodeHexStringMob(m.captured(0).toLatin1(), raw))
            insertBothForms(result.mobIds, raw);
    }

    result.valid = true;
    return result;
}