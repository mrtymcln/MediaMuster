// MDB can store RIFF/WAVE and AIFF/AIFF-C header copies in its descriptor
// summaries. Decode their independent fields, retaining original bytes and
// uncertainty, without depending on OMF media or native audio-file parsing.

#include "mdbaudiosummary_p.h"

#include <QtEndian>
#include <QVariantMap>
#include <algorithm>
#include <cmath>

namespace MediaEngine::MdbDetail
{
    namespace
    {
        struct AudioFormatResult
        {
            QVariantMap values;
            QVector<RawProperty> fields;
            QStringList diagnostics;
        };

        quint16 word(const QByteArray &bytes, qsizetype offset, bool big)
        {
            return big ? qFromBigEndian<quint16>(bytes.constData() + offset)
                       : qFromLittleEndian<quint16>(bytes.constData() + offset);
        }

        quint32 dword(const QByteArray &bytes, qsizetype offset, bool big)
        {
            return big ? qFromBigEndian<quint32>(bytes.constData() + offset)
                       : qFromLittleEndian<quint32>(bytes.constData() + offset);
        }

        QString chunkName(const QByteArray &key)
        {
            const bool printable = std::all_of(key.cbegin(), key.cend(), [](char c)
                                               { return static_cast<unsigned char>(c) >= 32 && static_cast<unsigned char>(c) <= 126; });
            return printable ? QString::fromLatin1(key) : QStringLiteral("0x") + QString::fromLatin1(key.toHex());
        }

        RawProperty audioSlice(const RawProperty &parent, const QString &suffix, qsizetype offset, qsizetype length)
        {
            RawProperty result;
            result.locator.name = parent.locator.name + QLatin1Char('.') + suffix;
            result.locator.objectNumber = parent.locator.objectNumber;
            result.bento = parent.bento;
            const qsizetype available = std::max(qsizetype(0), parent.encoding.size() - offset);
            const qsizetype retained = std::min(length, available);
            result.encoding = parent.encoding.mid(offset, retained);
            result.state = retained == length ? PropertyReadState::Present : PropertyReadState::Unreadable;
            // Bento can split one value across several physical ranges. Map the
            // field's logical bytes through them instead of assuming contiguity.
            qint64 skip = std::min(offset, parent.encoding.size());
            qint64 remaining = retained;
            for (const auto &range : parent.locator.ranges)
            {
                if (skip > range.length || (skip == range.length && remaining > 0))
                {
                    skip -= range.length;
                    continue;
                }
                const qint64 count = std::min(remaining, range.length - skip);
                result.locator.ranges.append({range.offset + skip, count});
                remaining -= count;
                skip = 0;
                if (remaining == 0)
                    break;
            }
            return result;
        }

        class AudioFields
        {
        public:
            explicit AudioFields(const RawProperty &property) : m_property(property) {}

            void add(const QString &name, qsizetype offset, qsizetype length, const QVariant &value = {})
            {
                auto field = audioSlice(m_property, name, offset, length);
                if (field.state == PropertyReadState::Present)
                {
                    field.decoded = value;
                    if (value.isValid())
                        result.values.insert(name, value);
                }
                else
                    fail(field, QStringLiteral("Recorded %1 is truncated.").arg(name));
                result.fields.append(std::move(field));
            }

            void fail(RawProperty &field, const QString &reason)
            {
                field.state = PropertyReadState::Unreadable;
                field.interpretation = reason;
                result.diagnostics.append(reason);
            }

            AudioFormatResult result;

        private:
            const RawProperty &m_property;
        };

        AudioFormatResult decodeWaveFormat(const RawProperty &property)
        {
            AudioFields fields(property);
            const auto &bytes = property.encoding;
            if (bytes.size() < 16)
            {
                fields.add(QStringLiteral("FixedFields"), 0, 16);
                return std::move(fields.result);
            }
            const quint16 tag = word(bytes, 0, false);
            fields.add(QStringLiteral("wFormatTag"), 0, 2, tag);
            fields.add(QStringLiteral("nChannels"), 2, 2, word(bytes, 2, false));
            fields.add(QStringLiteral("nSamplesPerSec"), 4, 4, dword(bytes, 4, false));
            fields.add(QStringLiteral("nAvgBytesPerSec"), 8, 4, dword(bytes, 8, false));
            fields.add(QStringLiteral("nBlockAlign"), 12, 2, word(bytes, 12, false));
            fields.add(QStringLiteral("wBitsPerSample"), 14, 2, word(bytes, 14, false));
            fields.result.fields.last().interpretation = QStringLiteral("Recorded sample storage width; an extensible format can record a different valid precision.");
            if (tag == 1 && bytes.size() == 16)
                return std::move(fields.result);
            fields.add(QStringLiteral("cbSize"), 16, 2, bytes.size() >= 18 ? QVariant(word(bytes, 16, false)) : QVariant{});
            if (bytes.size() < 18)
                return std::move(fields.result);
            const quint16 extra = word(bytes, 16, false);
            if (extra > bytes.size() - 18)
                fields.fail(fields.result.fields.last(), QStringLiteral("WAVEFORMATEX cbSize extends beyond its fmt chunk."));
            if (tag != 0xfffe)
                return std::move(fields.result);

            fields.add(QStringLiteral("Samples"), 18, 2, bytes.size() >= 20 ? QVariant(word(bytes, 18, false)) : QVariant{});
            fields.add(QStringLiteral("dwChannelMask"), 20, 4, bytes.size() >= 24 ? QVariant(dword(bytes, 20, false)) : QVariant{});
            fields.result.fields.last().interpretation += QStringLiteral(" Channel mask describes speaker assignments, not free-text channel names.");
            fields.add(QStringLiteral("SubFormat"), 24, 16, bytes.size() >= 40 ? QVariant(bytes.mid(24, 16)) : QVariant{});
            if (extra < 22)
                fields.fail(fields.result.fields.last(), QStringLiteral("WAVE_FORMAT_EXTENSIBLE requires 22 declared extension bytes."));
            const auto &subformat = fields.result.fields.last();
            const auto pcm = QByteArray::fromHex("0100000000001000800000aa00389b71");
            const auto floatingPoint = QByteArray::fromHex("0300000000001000800000aa00389b71");
            if (subformat.state == PropertyReadState::Present && (subformat.encoding == pcm || subformat.encoding == floatingPoint))
            {
                const quint16 precision = word(bytes, 18, false);
                fields.add(QStringLiteral("wValidBitsPerSample"), 18, 2, precision);
                fields.result.fields.last().interpretation = QStringLiteral("The Samples union means valid sample precision for this recorded PCM/IEEE-float SubFormat.");
                if (precision == 0)
                    fields.result.fields.last().interpretation += QStringLiteral(" The recorded zero does not establish a usable precision.");
                if (precision > word(bytes, 14, false))
                    fields.fail(fields.result.fields.last(), QStringLiteral("Recorded valid precision exceeds the recorded sample storage width; neither number is changed."));
            }
            return std::move(fields.result);
        }

        AudioFormatResult decodeAiffCommon(const RawProperty &property, bool compressed)
        {
            AudioFields fields(property);
            const auto &bytes = property.encoding;
            if (bytes.size() < 18)
            {
                fields.add(QStringLiteral("FixedFields"), 0, 18);
                return std::move(fields.result);
            }
            fields.add(QStringLiteral("numChannels"), 0, 2, qint16(word(bytes, 0, true)));
            fields.add(QStringLiteral("numSampleFrames"), 2, 4, dword(bytes, 2, true));
            fields.add(QStringLiteral("sampleSize"), 6, 2, qint16(word(bytes, 6, true)));
            const quint16 signExponent = word(bytes, 8, true);
            const quint16 exponent = signExponent & 0x7fff;
            const quint64 mantissa = qFromBigEndian<quint64>(bytes.constData() + 10);
            fields.add(QStringLiteral("sampleRateSignExponent"), 8, 2, signExponent);
            fields.add(QStringLiteral("sampleRateSignificand"), 10, 8, QVariant::fromValue(mantissa));
            QVariant sampleRate;
            if (exponent != 0x7fff)
            {
                double approximation = std::ldexp(double(mantissa), (exponent == 0 ? 1 : exponent) - 16383 - 63);
                if (signExponent & 0x8000)
                    approximation = -approximation;
                if (std::isfinite(approximation))
                    sampleRate = approximation;
            }
            fields.add(QStringLiteral("sampleRate"), 8, 10, sampleRate);
            fields.result.fields.last().interpretation = QStringLiteral("Original 80-bit sampling-rate bytes retained; the double approximation may be rounded and does not describe the sound samples' numeric format.");
            if (!compressed)
                return std::move(fields.result);
            fields.add(QStringLiteral("compressionType"), 18, 4, bytes.size() >= 22 ? QVariant(bytes.mid(18, 4)) : QVariant{});
            fields.add(QStringLiteral("compressionNameLength"), 22, 1, bytes.size() >= 23 ? QVariant(quint8(bytes[22])) : QVariant{});
            if (bytes.size() < 23)
            {
                auto name = audioSlice(property, QStringLiteral("compressionName"), 23, 0);
                fields.fail(name, QStringLiteral("AIFF-C compression-name length is missing; independently recorded base fields and compression type remain usable."));
                fields.result.fields.append(std::move(name));
                return std::move(fields.result);
            }
            const qsizetype length = quint8(bytes[22]);
            fields.add(QStringLiteral("compressionName"), 23, length);
            auto &name = fields.result.fields.last();
            if (name.state == PropertyReadState::Present)
            {
                fields.result.values.insert(QStringLiteral("compressionNameBytes"), name.encoding);
                const bool ascii = std::all_of(name.encoding.cbegin(), name.encoding.cend(), [](char c)
                                               { return static_cast<unsigned char>(c) < 128; });
                name.textEncoding = ascii ? TextEncoding::Ascii : TextEncoding::Unknown;
                if (ascii)
                {
                    name.decoded = QString::fromLatin1(name.encoding);
                    name.textEncodingBasis = EvidenceBasis::Derived;
                    fields.result.values.insert(QStringLiteral("compressionName"), name.decoded);
                }
                name.interpretation = QStringLiteral("AIFF-C compressionName is a counted Pascal string, potentially localized. ASCII is recognized from its bytes; non-ASCII encoding is not assumed to be MacRoman or UTF-8.");
            }
            return std::move(fields.result);
        }

    }

    QVector<RawProperty> decodeAudioSummary(const RawProperty &summary)
    {
        QVector<RawProperty> result;
        const auto &bytes = summary.encoding;
        if (summary.state != PropertyReadState::Present || !summary.bytesRetained || !summary.bento ||
            (summary.bento->typeName != QLatin1String("omfi:DataValue") && summary.bento->typeName != QLatin1String("omfi:VarLenBytes")))
            return result;
        if (bytes.size() < 12)
        {
            auto header = audioSlice(summary, QStringLiteral("ContainerHeader"), 0, 12);
            header.interpretation = QStringLiteral("Audio Summary container header is truncated; no format fields inferred.");
            result.append(std::move(header));
            return result;
        }
        const bool wave = summary.locator.name == QLatin1String("OMFI:WAVD:Summary") && bytes.first(4) == "RIFF" && bytes.mid(8, 4) == "WAVE";
        const bool aiff = summary.locator.name == QLatin1String("OMFI:AIFD:Summary") && bytes.first(4) == "FORM" && (bytes.mid(8, 4) == "AIFF" || bytes.mid(8, 4) == "AIFC");
        if (!wave && !aiff)
        {
            auto header = audioSlice(summary, QStringLiteral("ContainerHeader"), 0, 12);
            header.interpretation = QStringLiteral("Audio Summary bytes retained, but this container signature has no supported interpretation.");
            result.append(std::move(header));
            return result;
        }
        // A Summary is a header copy, not a recording. Its sample chunk may
        // describe sound bytes deliberately omitted from this property.
        for (qsizetype offset = 12; offset <= bytes.size() - 8;)
        {
            const auto key = bytes.mid(offset, 4);
            const quint32 length = dword(bytes, offset + 4, aiff);
            if (key == "data" || key == "SSND")
                break;
            if (length > quint64(bytes.size() - offset - 8))
            {
                auto chunk = audioSlice(summary, chunkName(key), offset + 8, length);
                chunk.state = PropertyReadState::Unreadable;
                chunk.interpretation = QStringLiteral("Copied metadata chunk extends beyond its Summary; omitted sample data is not required here.");
                result.append(std::move(chunk));
                break;
            }
            if ((wave && key == "fmt ") || (aiff && key == "COMM"))
            {
                auto chunk = audioSlice(summary, chunkName(key), offset + 8, length);
                chunk.locator.key = key;
                auto decoded = wave ? decodeWaveFormat(chunk) : decodeAiffCommon(chunk, bytes.mid(8, 4) == "AIFC");
                chunk.decoded = std::move(decoded.values);
                chunk.interpretation = QStringLiteral("Decoded audio fields copied inside this OMF descriptor Summary; this is not the media file's native header.");
                if (!decoded.diagnostics.isEmpty())
                    chunk.interpretation += QLatin1Char(' ') + decoded.diagnostics.join(QLatin1Char(' '));
                result.append(std::move(chunk));
                result += decoded.fields;
            }
            offset += 8 + qsizetype(length) + (length & 1);
        }
        return result;
    }

}
