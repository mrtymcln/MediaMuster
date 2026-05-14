#include "avbparser.h"
#include "mobid.h"

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <cstring>
#include <utility>

// Pulls MOB IDs out of an Avid bin (.avb) by scanning for 32-byte MOB
// patterns. We never decode the full Bento object graph — for filtering
// all that matters is the set of clips the bin references.
//
// An Avid bin is a Bento container with this header layout:
//   06 00 "DomainDJBO"  07 00 "AObjDoc"  04 13 00  <YYYY/MM/DD HH:MM:SS>
//
// MOB IDs appear in two forms:
//   1. Binary — 32-byte runs in the byte stream:
//         SMPTE UMID   06 0E 2B 34 04 01 ...   (32 bytes)
//         Avid MOB     06 0A 2B 34 01 01 0F ... (32 bytes)
//
//   2. ASCII hex string — same underlying 32 bytes, serialised with dashes
//      at field boundaries, e.g.:
//         "060a2b340101010001010f0013-000000-6b0fb74dc6fa687b-..."
//      64 hex chars after stripping dashes.
//
// Endian mismatch:
//   Bytes 16..23 hold a (uint32, uint16, uint16) triple. AVB stores them
//   little-endian; PMR stores them big-endian. The same logical MOB therefore
//   has two distinct 32-byte representations, e.g.:
//     Bin : 060a2b3401010105 01010f1013000000 78563412 bbaa ddcc 0123456789abcdef
//     PMR : 060a2b3401010105 01010f1013000000 12345678 aabb ccdd 0123456789abcdef
//   Both variants land in mobIds so lookups against MediaFile::mobId and
//   MediaFile::compositionMobId hit regardless of source.
//
// Bento sentinels share the 06 0? 2B 34 prefix (e.g. 06 0E 2B 34 7F 7F 2A 80)
// and are filtered out by the byte-4/12/20 checks in the validators below.

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
        // fromHex silently skips characters not in [0-9a-fA-F] and returns a
        // shorter buffer; validate the decoded size before the memcpy so the
        // function stays safe if the caller's regex ever loosens.
        const QByteArray decoded = QByteArray::fromHex(clean);
        if (decoded.size() != kMobIdLen)
            return false;
        std::memcpy(out, decoded.constData(), kMobIdLen);
        return true;
    }
}

AvbBin AvbParser::parse(const QString &avbFilePath)
{
    AvbBin result;
    result.filePath = avbFilePath;

    // completeBaseName() preserves dots ("director.cuts.avb" → "director.cuts").
    QFileInfo fi(avbFilePath);
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
        qWarning() << "AVB: larger than 64 MB." << avbFilePath
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

    const auto *data = reinterpret_cast<const uchar *>(buf.constData());
    const qint64 size = buf.size();

    // Pass 1: binary 32-byte MOB IDs.
    const qint64 scanEnd = size - kMobIdLen;
    for (qint64 i = 0; i <= scanEnd;)
    {
        if (data[i] != 0x06 || data[i + 2] != 0x2B || data[i + 3] != 0x34)
        {
            ++i;
            continue;
        }
        const uchar *p = data + i;
        if ((data[i + 1] == 0x0E && isValidSmpteUmid(p)) ||
            (data[i + 1] == 0x0A && isValidAvidMob(p)))
        {
            insertBothForms(result.mobIds, p);
            i += kMobIdLen;
        }
        else
        {
            ++i;
        }
    }

    // Pass 2: ASCII hex string MOB IDs (the format used by the PMR). Manual
    // byte scan instead of a regex on a QString — `QString::fromLatin1(buf)`
    // on a 64 MB cap doubled the buffer to a 128 MB QString just to run the
    // pattern. Match semantics are identical to the old regex
    // `06(?:0a|0e)2b34[0-9a-f-]{56,68}`: 8-char prefix, then 56-68 chars
    // from [0-9a-f-]. decodeHexStringMob still validates the decoded length
    // downstream.
    const char *bytes = buf.constData();
    auto isHexDash = [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '-';
    };
    for (qint64 i = 0; i + 8 <= size; ++i)
    {
        if (bytes[i] != '0' || bytes[i + 1] != '6') continue;
        if (bytes[i + 2] != '0') continue;
        if (bytes[i + 3] != 'a' && bytes[i + 3] != 'e') continue;
        if (bytes[i + 4] != '2' || bytes[i + 5] != 'b') continue;
        if (bytes[i + 6] != '3' || bytes[i + 7] != '4') continue;

        qint64 j = i + 8;
        const qint64 maxEnd = qMin(i + 8 + 68, size);
        while (j < maxEnd && isHexDash(bytes[j]))
            ++j;

        if (j - i >= 8 + 56)
        {
            uchar raw[kMobIdLen];
            if (decodeHexStringMob(QByteArray::fromRawData(bytes + i, int(j - i)), raw))
                insertBothForms(result.mobIds, raw);
            i = j - 1; // ++i next iteration will land at j
        }
    }

    result.valid = true;
    return result;
}