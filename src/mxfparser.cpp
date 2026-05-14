#include "mxfparser.h"
#include <QFile>
#include <QHash>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cstring>

// Extracts technical metadata from MXF file headers via direct KLV parsing.
// Only the header partition is read — essence data is never touched.
//
// The two SMPTE Universal Labels below are the KLV keys we anchor against:
// the Header Partition Pack marks where parsing starts, and the Set prefix
// flags every metadata Set we care about (descriptors, packages, components).

static constexpr char UL_HEADER_PARTITION[] =
    "\x06\x0e\x2b\x34\x02\x05\x01\x01\x0d\x01\x02\x01\x01\x02";

static constexpr char UL_SET_PREFIX[] =
    "\x06\x0e\x2b\x34\x02\x53\x01\x01\x0d\x01\x01\x01\x01\x01";

static constexpr quint8 SET_CDCI = 0x28;
static constexpr quint8 SET_RGBA = 0x29;
static constexpr quint8 SET_WAVE = 0x48;
static constexpr quint8 SET_AES3 = 0x47;
static constexpr quint8 SET_MAT_PKG = 0x36;
static constexpr quint8 SET_SRC_PKG = 0x37;
static constexpr quint8 SET_SEQUENCE = 0x0F;
static constexpr quint8 SET_SOURCE_CLIP = 0x11;
static constexpr quint8 SET_TIMECODE = 0x14;

// Duration fields come in 4- or 8-byte big endian form depending on the MXF flavour.
// Caller must have verified that pos + len is within data bounds.
static qint64 readDuration(const QByteArray &data, qint64 pos, quint16 len)
{
    if (len < 4)
        return -1;
    const auto *p = reinterpret_cast<const uchar *>(data.constData() + pos);
    if (len >= 8)
    {
        const quint32 hi = qFromBigEndian<quint32>(p);
        const quint32 lo = qFromBigEndian<quint32>(p + 4);
        return (static_cast<qint64>(hi) << 32) | lo;
    }
    return qFromBigEndian<quint32>(p);
}

qint64 MxfParser::readBerLength(const QByteArray &data, qint64 offset, int &bytesUsed)
{
    if (offset >= data.size())
    {
        bytesUsed = 0;
        return -1;
    }
    const quint8 firstByte = static_cast<quint8>(data[offset]);
    if (firstByte < 0x80)
    {
        bytesUsed = 1;
        return firstByte;
    }
    const int lenBytes = firstByte & 0x7f;
    if (lenBytes > 8 || offset + 1 + lenBytes > data.size())
    {
        bytesUsed = 0;
        return -1;
    }
    bytesUsed = 1 + lenBytes;
    qint64 length = 0;
    for (int i = 0; i < lenBytes; ++i)
        length = (length << 8) | static_cast<quint8>(data[offset + 1 + i]);
    return length;
}

quint16 MxfParser::readUint16BE(const QByteArray &data, qint64 offset)
{
    if (offset + 2 > data.size())
        return 0;
    return qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(data.constData() + offset));
}

quint32 MxfParser::readUint32BE(const QByteArray &data, qint64 offset)
{
    if (offset + 4 > data.size())
        return 0;
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + offset));
}

// Avid allocates 256 KB or 512 KB for the header, depending on Media Composer version.
// Fall back to 512 KB read if the first pass yields no valid metadata results.
MxfMetadata MxfParser::parseHeader(const QString &filePath, qint64 *bytesRead)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (bytesRead)
            *bytesRead = 0;
        return {};
    }

    constexpr qint64 kFastRead = 256 * 1024;
    constexpr qint64 kFallbackRead = 512 * 1024;

    QByteArray data = file.read(kFastRead);
    MxfMetadata meta = parseFromBuffer(data);

    if (!meta.valid && data.size() < kFallbackRead)
    {
        data.append(file.read(kFallbackRead - data.size()));
        meta = parseFromBuffer(data);
    }
    file.close();

    if (bytesRead)
        *bytesRead = data.size();
    return meta;
}

MxfMetadata MxfParser::parseFromBuffer(const QByteArray &data)
{
    MxfMetadata meta;

    qint64 pos = 0;
    const int headerIdx = data.indexOf(QByteArray(UL_HEADER_PARTITION, 14));
    if (headerIdx >= 0)
        pos = headerIdx;

    // Hot loop: compare against the source buffer directly. data.mid(pos, 16)
    // here was allocating one QByteArray per KLV record, ~500 records per
    // MXF file × 50k+ files per scan, when all we needed was the underlying
    // bytes for one memcmp and one byte read.
    const char *base = data.constData();
    const qint64 dataSize = data.size();
    while (pos + 16 < dataSize)
    {
        int bytesUsed = 0;
        const qint64 length = readBerLength(data, pos + 16, bytesUsed);
        if (length < 0 || bytesUsed == 0)
            break;
        const qint64 valuePos = pos + 16 + bytesUsed;
        if (valuePos + length > dataSize)
            break;

        if (std::memcmp(base + pos, UL_SET_PREFIX, 13) == 0)
        {
            const auto setType = static_cast<quint8>(base[pos + 14]);
            switch (setType)
            {
            case SET_CDCI:
            case SET_RGBA:
                parseDescriptorSet(data, valuePos, length, meta);
                break;
            case SET_WAVE:
            case SET_AES3:
                meta.isAudio = true;
                parseDescriptorSet(data, valuePos, length, meta);
                break;
            case SET_MAT_PKG:
            case SET_SRC_PKG:
                parseMaterialPackage(data, valuePos, length, meta);
                break;
            case SET_SEQUENCE:
            case SET_SOURCE_CLIP:
            case SET_TIMECODE:
                parseStructuralComponent(data, valuePos, length, meta);
                break;
            }
        }
        pos = valuePos + length;
    }

    // Interlaced sources store one field height in the descriptor (e.g. 540 for 1080i).
    // FrameLayout 1 = Separate Fields and 3 = Mixed Fields both need doubling.
    if (meta.frameLayout == 1 || meta.frameLayout == 3)
        meta.height *= 2;

    // Some Avid MXF files write garbage into the stored width tag (0x3203).
    if (meta.width > 16384)
        meta.width = 0;

    if (meta.width > 0 && meta.height > 0)
    {
        meta.resolution = QString("%1x%2").arg(meta.width).arg(meta.height);
        meta.valid = true;
    }
    else if (meta.height > 0)
    {
        // Width is missing or corrupt — infer it from standard frame sizes.
        static constexpr std::array<std::pair<int, int>, 6> kHeightToWidth = {{{2160, 3840}, {1080, 1920}, {720, 1280}, {576, 720}, {486, 720}, {480, 720}}};
        const auto it = std::find_if(kHeightToWidth.begin(), kHeightToWidth.end(),
                                     [h = meta.height](const auto &p)
                                     { return p.first == h; });
        if (it != kHeightToWidth.end())
        {
            meta.width = it->second;
            meta.resolution = QString("%1x%2").arg(meta.width).arg(meta.height);
            meta.valid = true;
        }
    }
    else if (meta.isAudio && meta.sampleRate > 0)
    {
        meta.valid = true;
    }

    // Codec translation is deferred until here so the framerate is finalised first.
    if (meta.valid && !meta.essenceContainerLabel.isEmpty())
        meta.codec = codecFromEssenceLabel(meta.essenceContainerLabel, meta.fps);
    if (meta.valid && meta.codec.isEmpty())
        meta.codec = meta.isAudio ? "PCM Audio" : "Avid Uncompressed";

    // Avid displays DV as "DV 25 420 i(PAL)" etc. Scan type and broadcast
    // standard come from MXF metadata rather than the codec UL itself.
    if (meta.codec.startsWith("DV "))
    {
        const QString scan = (meta.frameLayout == 1 || meta.frameLayout == 3) ? "i" : "p";
        QString standard;
        if (meta.fps == "25" || meta.height == 576 || meta.height == 288)
            standard = "PAL";
        else if (meta.fps == "29.97" || meta.height == 480 || meta.height == 486)
            standard = "NTSC";
        if (!standard.isEmpty())
            meta.codec += QString(" %1(%2)").arg(scan, standard);
    }

    return meta;
}

void MxfParser::parseDescriptorSet(const QByteArray &data, qint64 startPos, qint64 length, MxfMetadata &out)
{
    qint64 pos = startPos;
    const qint64 endPos = startPos + length;

    while (pos + 4 <= endPos)
    {
        const quint16 tag = readUint16BE(data, pos);
        const quint16 len = readUint16BE(data, pos + 2);
        pos += 4;
        if (pos + len > endPos)
            break;

        switch (tag)
        {
        case 0x3201: // picture essence coding UL — identifies the codec
            if (len >= 8 && out.essenceContainerLabel.isEmpty())
                out.essenceContainerLabel = data.mid(pos, len);
            break;
        case 0x3203: // stored width
            if (len >= 4)
                out.width = static_cast<int>(readUint32BE(data, pos));
            break;
        case 0x3202: // stored height — one field only for interlaced content
            if (len >= 4)
                out.height = static_cast<int>(readUint32BE(data, pos));
            break;
        case 0x320C: // frame layout: 0=full frame, 1=separate fields, 2=single field, 3=mixed
            if (len >= 1)
                out.frameLayout = static_cast<quint8>(data[pos]);
            break;
        case 0x3001: // sample rate — fps for video, Hz for audio
            if (len >= 8)
            {
                const quint32 num = readUint32BE(data, pos);
                const quint32 den = readUint32BE(data, pos + 4);
                if (den > 0)
                {
                    if (out.isAudio)
                    {
                        out.sampleRate = static_cast<int>(num / den);
                    }
                    else
                    {
                        if (num == 30000 && den == 1001)
                            out.fps = "29.97";
                        else if (num == 24000 && den == 1001)
                            out.fps = "23.976";
                        else if (num == 60000 && den == 1001)
                            out.fps = "59.94";
                        else
                            out.fps = QString::number(qRound(static_cast<float>(num) / den));
                    }
                }
            }
            break;
        case 0x3002: // container duration (4 or 8 bytes)
            if (const qint64 d = readDuration(data, pos, len); d > 0)
                if (out.durationFrames == 0 || d < out.durationFrames)
                    out.durationFrames = d;
            break;
        case 0x3D03: // audio sampling rate (alternate to 0x3001 for Wave/AES3 descriptors)
            if (out.isAudio && len >= 8)
            {
                const quint32 num = readUint32BE(data, pos);
                const quint32 den = readUint32BE(data, pos + 4);
                if (den > 0)
                    out.sampleRate = static_cast<int>(num / den);
            }
            break;
        case 0x3301: // video quantisation bits
            if (len >= 4)
                out.bitDepth = QString("%1-bit").arg(readUint32BE(data, pos));
            break;
        case 0x3D01: // audio quantisation bits
            if (out.isAudio && len >= 4)
                out.bitDepth = QString("%1-bit").arg(readUint32BE(data, pos));
            break;
        case 0x3D07: // audio channel count
            if (out.isAudio && len >= 4)
                out.channels = static_cast<int>(readUint32BE(data, pos));
            break;
        }
        pos += len;
    }
}

void MxfParser::parseMaterialPackage(const QByteArray &data, qint64 startPos, qint64 length, MxfMetadata &out)
{
    qint64 pos = startPos;
    const qint64 endPos = startPos + length;

    while (pos + 4 <= endPos)
    {
        const quint16 tag = readUint16BE(data, pos);
        const quint16 len = readUint16BE(data, pos + 2);
        pos += 4;
        if (pos + len > endPos)
            break;

        if (tag == 0x4401 && len >= 32 && out.umid.isEmpty())
        {
            out.umid = formatUmid(data.mid(pos, 32));
        }
        else if (tag == 0x4402 && out.clipName.isEmpty())
        {
            // UTF-16BE string
            QString name;
            name.reserve(len / 2);
            for (int i = 0; i + 1 < len; i += 2)
            {
                const quint16 ch = readUint16BE(data, pos + i);
                if (ch == 0)
                    break;
                name.append(QChar(ch));
            }
            out.clipName = std::move(name);
        }
        pos += len;
    }
}

void MxfParser::parseStructuralComponent(const QByteArray &data, qint64 startPos, qint64 length, MxfMetadata &out)
{
    qint64 pos = startPos;
    const qint64 endPos = startPos + length;

    while (pos + 4 <= endPos)
    {
        const quint16 tag = readUint16BE(data, pos);
        const quint16 len = readUint16BE(data, pos + 2);
        pos += 4;
        if (pos + len > endPos)
            break;

        if (tag == 0x0202) // component duration (4 or 8 bytes)
        {
            if (const qint64 d = readDuration(data, pos, len); d > 0)
                if (out.durationFrames == 0 || d < out.durationFrames)
                    out.durationFrames = d;
        }
        pos += len;
    }
}

// ULs verified against files from Media Composer 2023 and 2025.
QString MxfParser::codecFromEssenceLabel(const QByteArray &label, const QString &fps)
{
    if (label.isEmpty())
        return {};

    // Hex strings here are decoded once at first call; lookup uses raw bytes
    // against `label` directly — no per-call toHex/toUpper allocation.
    struct Entry { const char *hex; const char *name; };
    static constexpr Entry kEntries[] = {
        // Avid legacy (JFIF / Meridien / MJPEG)
        {"060E2B34040101010D01030102010101", "Avid 15:1s"},
        {"060E2B34040101010D01030102010201", "Avid 2:1"},
        {"060E2B34040101010D01030102010401", "Avid 3:1"},
        {"060E2B34040101010E04020102040100", "Avid 20:1"},

        // Avid uncompressed
        {"060E2B34040101010D01030102050101", "Avid 1:1 8-bit"},
        {"060E2B34040101010D01030102050201", "Avid 1:1 10-bit"},

        // DNxHD — Avid private (bitrate names resolved from fps below)
        {"060E2B34040101010D01030102060301", "DNxHD LB"},
        {"060E2B34040101010D01030102060101", "DNxHD SQ"},
        {"060E2B34040101010D01030102060201", "DNxHD HQ"},
        {"060E2B34040101010D01030102060202", "DNxHD HQX"},

        // DNxHD — SMPTE VC-3 registered (namespace 04.01.02.02.71.XX.00.00)
        {"060E2B340401010A0401020271130000", "DNxHD LB"},  // CID 0x13
        {"060E2B340401010A0401020271030000", "DNxHD SQ"},  // CID 0x03
        {"060E2B340401010A0401020271040000", "DNxHD HQ"},  // CID 0x04
        {"060E2B340401010A0401020271010000", "DNxHD HQX"}, // CID 0x01
        {"060E2B340401010A0401020271070000", "DNxHD HQX"}, // CID 0x07 — 10-bit 4:2:2
        {"060E2B340401010A0401020271090000", "DNxHD HQ"},  // CID 0x09 — 8-bit 4:2:2

        // DNxHR — Avid private (namespace 0D.01.03.01.02.11.XX.01)
        {"060E2B34040101010D01030102110101", "DNxHR LB"},
        {"060E2B34040101010D01030102110201", "DNxHR SQ"},
        {"060E2B34040101010D01030102110301", "DNxHR HQ"},
        {"060E2B34040101010D01030102110401", "DNxHR HQX"},
        {"060E2B34040101010D01030102110501", "DNxHR 444"},

        // DNxHR — Avid private (namespace 0E.04.02.01.02.11.XX.00)
        {"060E2B34040101010E04020102110300", "DNxHR HQ"},
        {"060E2B34040101010E04020102110400", "DNxHR HQX"},

        // DNxHR — SMPTE registered
        {"060E2B340401010D0401020271250000", "DNxHR SQ"}, // CID 0x25
        {"060E2B340401010D0401020271260000", "DNxHR HQ"}, // CID 0x26

        // DNxUncompressed (SMPTE RDD 44) — bit depth is in descriptor tag 0x3301, not the UL
        {"060E2B340401010D0401020203070100", "DNxUncompressed"},
        {"060E2B340401010D0401020203070200", "DNxUncompressed"},

        // Sony IMX
        {"060E2B34040101010401020201020101", "Sony IMX 30"},
        {"060E2B34040101010401020201020102", "Sony IMX 40"},
        {"060E2B34040101010401020201020103", "Sony IMX 50"},

        // Sony XDCAM
        {"060E2B34040101030401020201030300", "XDCAM EX 35"},
        {"060E2B34040101030401020201040200", "XDCAM EX 35"},
        {"060E2B34040101030401020201040300", "XDCAM HD 50"},

        // Sony XAVC (SMPTE RDD 32)
        {"060E2B340401010A0401020201323102", "XAVC HD Intra CBG Class 100"},
        {"060E2B340401010D0401020201323203", "XAVC HD Intra CBG Class 200"},
        {"060E2B340401010D0401020201323204", "XAVC HD Intra CBG Class 200"},

        // Panasonic AVC-Intra
        {"060E2B340401010A0401020201313001", "AVC-Intra 50"},
        {"060E2B340401010A0401020201323001", "AVC-Intra 100"},
        {"060E2B340401010A0401020201323104", "AVC-Intra 100 (4:2:2)"},

        // AVC Long GOP — covers variants
        {"060E2B340401010D0401020201312001", "AVC Long GOP"},
        {"060E2B340401010D0401020201314001", "AVC Long GOP"},
        {"060E2B340401010D0401020201316001", "AVC Long GOP"},

        // H.264 Proxy — covers 800 Kbps and 1500 Kbps variants
        {"060E2B340401010D0401020201311101", "H.264 Proxy"},

        // DV
        {"060E2B34040101010401020202010200", "DV 25 420"},
        {"060E2B34040101010401020202020200", "DV 25 411"},
        {"060E2B34040101010401020202020400", "DV 50"},

        // JPEG 2000
        {"060E2B34040101070401020203010100", "JPEG 2000"},
        {"060E2B340401010D040102020301020A", "JPEG 2000 IMF"},

        // Apple ProRes — Avid private
        {"060E2B34040101010D010301020C0101", "Apple ProRes Proxy"},
        {"060E2B34040101010D010301020C0201", "Apple ProRes LT"},
        {"060E2B34040101010D010301020C0301", "Apple ProRes 422"},
        {"060E2B34040101010D010301020C0401", "Apple ProRes HQ"},
        {"060E2B34040101010D010301020C0501", "Apple ProRes 4444"},

        // Apple ProRes — SMPTE RDD 44 (namespace 04.01.02.02.03.06.XX.00)
        {"060E2B340401010D0401020203060100", "Apple ProRes Proxy"},
        {"060E2B340401010D0401020203060200", "Apple ProRes LT"},
        {"060E2B340401010D0401020203060300", "Apple ProRes 422"},
        {"060E2B340401010D0401020203060400", "Apple ProRes HQ"},
        {"060E2B340401010D0401020203060500", "Apple ProRes 4444"},
        {"060E2B340401010D0401020203060600", "Apple ProRes 4444 XQ"},

        // miscellaneous
        {"060E2B34040101010E04030101030200", "Avid Title/Matte"},
        {"4B464141000D4D4F", "Avid Title (Uncompressed)"},
        {"060E2B34040101010D01030102060100", "PCM Audio"},
    };

    static const QHash<QByteArray, QString> kCodecs = []
    {
        QHash<QByteArray, QString> m;
        m.reserve(std::size(kEntries));
        for (const auto &e : kEntries)
            m.insert(QByteArray::fromHex(e.hex), QString::fromLatin1(e.name));
        return m;
    }();

    const auto it = kCodecs.find(label);
    if (it == kCodecs.end())
    {
        // Unknown UL — build the hex string only for the error path.
        const QString hexStr = QString::fromLatin1(label.toHex().toUpper());
        // Try to return the family name from the byte structure so editors see
        // something more useful than the raw hex.
        QString family;
        if (label.size() >= 16)
        {
            const quint8 b8 = static_cast<quint8>(label[8]);
            const quint8 b9 = static_cast<quint8>(label[9]);
            const quint8 b12 = static_cast<quint8>(label[12]);

            if (b8 == 0x04 && b9 == 0x01) // SMPTE picture
            {
                switch (b12)
                {
                case 0x01:
                    family = "MPEG";
                    break;
                case 0x02:
                    family = "DV";
                    break;
                case 0x03:
                    family = "Picture Coding";
                    break; // J2K, ProRes, FFV1
                case 0x71:
                    family = "VC-3";
                    break; // DNxHD / DNxHR
                }
            }
            else if (b8 == 0x04 && b9 == 0x02)
            {
                family = "Audio";
            }
            else if ((b8 == 0x0E && b9 == 0x04) || b8 == 0x0D)
            {
                family = "Avid";
            }
        }
        if (!family.isEmpty())
            return family + " (unknown variant: " + hexStr + ")";
        return "Unknown (" + hexStr + ")";
    }

    const QString &baseName = it.value();

    // DNxHD bitrates depend on framerate — the UL only identifies the quality
    // tier. Four parallel if-chains used to live here; the table separates the
    // data (16 bitrate strings) from the control flow (one fps mapping).
    struct DnxEntry { const char *tier, *r30, *r25, *r60, *rDefault; };
    static constexpr DnxEntry kDnxTiers[] = {
        // tier         29.97/30   25         50/59.94   default
        {"DNxHD LB",   "45",      "36",      "90",      "36"},
        {"DNxHD SQ",   "145",     "120",     "115",     "115"},
        {"DNxHD HQ",   "220",     "185",     "175",     "175"},
        {"DNxHD HQX",  "220X",    "185X",    "175X",    "175X"},
    };

    for (const auto &e : kDnxTiers)
    {
        if (baseName != QLatin1String(e.tier))
            continue;
        const char *bitrate;
        if (fps == "29.97" || fps == "30")
            bitrate = e.r30;
        else if (fps == "25")
            bitrate = e.r25;
        else if (fps == "50" || fps == "59.94")
            bitrate = e.r60;
        else
            bitrate = e.rDefault;
        return QLatin1String(e.tier) + QLatin1String(" (DNxHD ") +
               QLatin1String(bitrate) + QLatin1Char(')');
    }

    return baseName;
}

QString MxfParser::formatUmid(const QByteArray &raw)
{
    if (raw.size() < 32)
        return {};

    if (std::all_of(raw.cbegin(), raw.cbegin() + 32,
                    [](char c) { return c == '\0'; }))
        return QStringLiteral("0000000000000000.0000000000000000."
                              "0000000000000000.0000000000000000");

    static constexpr char kHex[] = "0123456789ABCDEF";
    QString result(67, Qt::Uninitialized);
    int out = 0;
    for (int i = 0; i < 32; ++i)
    {
        const quint8 b = static_cast<quint8>(raw[i]);
        result[out++] = QLatin1Char(kHex[b >> 4]);
        result[out++] = QLatin1Char(kHex[b & 0xF]);
        if ((i + 1) % 8 == 0 && i < 31)
            result[out++] = QLatin1Char('.');
    }
    return result;
}