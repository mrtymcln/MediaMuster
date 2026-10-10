// Inspects the chunk headers and native metadata in legacy audio files without
// loading their sound samples. Every chunk keeps its original location; private
// metadata stays available even when its meaning is not yet established.

#include "audioreader_p.h"

#include <QtEndian>
#include <QVariantMap>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace MediaEngine::Detail
{
    namespace
    {
        using Outcome = ParsedSource::Outcome;
        constexpr quint32 sizeSentinel = 0xffffffff;

        struct ReadFailure
        {
            Outcome outcome;
            QString reason;
        };

        class Input
        {
        public:
            Input(QIODevice &device, const Cancellation &cancellation)
                : m_device(device), m_cancellation(cancellation), m_size(device.size())
            {
                if (!device.isOpen() || !device.isReadable() || device.isSequential() || device.isTextModeEnabled())
                    throw ReadFailure{Outcome::IoError, QStringLiteral("Audio reader requires an open, readable, seekable binary device.")};
                if (m_size < 0)
                    throw ReadFailure{Outcome::IoError, QStringLiteral("Cannot determine audio file extent.")};
            }

            qint64 size() const { return m_size; }

            void check() const
            {
                if (m_cancellation.cancelled())
                    throw ReadFailure{Outcome::Cancelled, QStringLiteral("Audio read cancelled.")};
            }

            void read(RawProperty &property, qint64 offset, qint64 length)
            {
                check();
                property.state = PropertyReadState::Unreadable;
                property.locator.ranges = {{offset, 0}};
                if (offset < 0 || length < 0)
                    throw ReadFailure{Outcome::Malformed, QStringLiteral("Audio metadata has an invalid source range.")};
                if (offset > m_size)
                    throw ReadFailure{Outcome::Incomplete, QStringLiteral("Audio metadata starts beyond the physical file.")};
                if (!m_device.seek(offset))
                    throw ReadFailure{Outcome::IoError, QStringLiteral("Cannot seek to audio metadata at byte %1.").arg(offset)};
                std::array<char, 16384> buffer{};
                while (length > 0)
                {
                    check();
                    const qint64 available = m_size - offset - property.encoding.size();
                    if (available == 0)
                        throw ReadFailure{Outcome::Incomplete, QStringLiteral("Truncated %1.").arg(property.locator.name)};
                    const qint64 amount = std::min({length, available, qint64(buffer.size())});
                    const qint64 received = m_device.read(buffer.data(), amount);
                    if (received <= 0)
                        throw ReadFailure{received == 0 && m_device.atEnd() ? Outcome::Incomplete : Outcome::IoError,
                                          QStringLiteral("Cannot read %1 at byte %2: %3")
                                              .arg(property.locator.name)
                                              .arg(offset + property.encoding.size())
                                              .arg(m_device.errorString())};
                    property.encoding.append(buffer.data(), received);
                    property.locator.ranges.front().length = property.encoding.size();
                    length -= received;
                }
                property.state = PropertyReadState::Present;
            }

            void finish() const
            {
                check();
                if (m_device.size() != m_size)
                    throw ReadFailure{Outcome::Incomplete, QStringLiteral("Audio file length changed while reading; source must be checked again.")};
            }

        private:
            QIODevice &m_device;
            const Cancellation &m_cancellation;
            const qint64 m_size;
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

        quint64 qword(const QByteArray &bytes, qsizetype offset)
        {
            return qFromLittleEndian<quint64>(bytes.constData() + offset);
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

        struct SizeEntry
        {
            QByteArray key;
            quint64 size = 0;
            bool used = false;
        };

        class AudioParser
        {
        public:
            AudioParser(Input &input, AudioReadResult &result) : m_input(input), m_result(result) {}

            void parse()
            {
                const auto headerIndex = append(QStringLiteral("Audio.ContainerHeader"));
                m_input.read(properties()[headerIndex], 0, 12);
                const QByteArray header = properties()[headerIndex].encoding;
                const QByteArray signature = header.first(4);
                m_big = signature == "FORM";
                m_rf64 = signature == "RF64";
                m_aifc = header.mid(8, 4) == "AIFC";
                const bool wave = (signature == "RIFF" || m_rf64) && header.mid(8, 4) == "WAVE";
                const bool aiff = m_big && (m_aifc || header.mid(8, 4) == "AIFF");
                if (!wave && !aiff)
                    throw ReadFailure{Outcome::Unsupported, QStringLiteral("Source is not supported RIFF/RF64 WAVE or FORM AIFF/AIFF-C audio.")};
                m_result.source.container = wave ? ParsedSource::Container::Wave : ParsedSource::Container::Aiff;
                const quint32 declaredSize = dword(header, 4, m_big);
                properties()[headerIndex].decoded = QVariantMap{{QStringLiteral("signature"), signature},
                                                                {QStringLiteral("size"), declaredSize},
                                                                {QStringLiteral("formType"), header.mid(8, 4)}};
                m_extendedContainerSize = m_rf64 && declaredSize == sizeSentinel;
                m_end = m_extendedContainerSize ? m_input.size() : qint64(declaredSize) + 8;
                if (m_end < 12)
                    throw ReadFailure{Outcome::Malformed, QStringLiteral("Audio container size is smaller than its header.")};

                qint64 position = 12;
                bool first = true;
                while (position < m_end)
                {
                    m_input.check();
                    if (m_end - position < 8)
                    {
                        range(QStringLiteral("Audio.IncompleteChunkHeader"), {}, position, std::max(qint64(0), std::min(m_end, m_input.size()) - position));
                        throw ReadFailure{m_end > m_input.size() ? Outcome::Incomplete : Outcome::Malformed,
                                          QStringLiteral("Audio container ends inside a chunk header.")};
                    }
                    const auto chunkHeader = append(QStringLiteral("Audio.ChunkHeader"));
                    m_input.read(properties()[chunkHeader], position, 8);
                    const QByteArray framing = properties()[chunkHeader].encoding;
                    const QByteArray key = framing.first(4);
                    const quint32 size32 = dword(framing, 4, m_big);
                    properties()[chunkHeader].decoded = QVariantMap{{QStringLiteral("chunkId"), key}, {QStringLiteral("chunkSize"), size32}};
                    if (m_rf64 && first && key != "ds64")
                        throw ReadFailure{Outcome::Malformed, QStringLiteral("RF64 requires ds64 as its first chunk.")};
                    first = false;
                    const quint64 size64 = resolveSize(key, size32);
                    const qint64 payload = position + 8;
                    if (size64 > quint64(std::numeric_limits<qint64>::max()) || size64 > quint64(m_end - payload) ||
                        payload > m_input.size() || size64 > quint64(m_input.size() - payload))
                    {
                        const auto index = range(QStringLiteral("Audio.") + chunkName(key), key, payload,
                                                 std::max(qint64(0), std::min(m_end, m_input.size()) - payload));
                        properties()[index].state = PropertyReadState::Unreadable;
                        throw ReadFailure{m_end > m_input.size() || payload > m_input.size() || size64 > quint64(std::max(qint64(0), m_input.size() - payload))
                                              ? Outcome::Incomplete
                                              : Outcome::Malformed,
                                          QStringLiteral("Chunk %1 extends beyond its container or file.").arg(chunkName(key))};
                    }
                    const qint64 length = qint64(size64);
                    readChunk(key, payload, length);
                    const qint64 end = payload + length;
                    if ((length & 1) != 0)
                    {
                        if (end == m_end)
                            throw ReadFailure{Outcome::Malformed, QStringLiteral("Odd-sized audio chunk has no alignment byte inside its container.")};
                        const auto padding = append(QStringLiteral("Audio.ChunkPadding"));
                        m_input.read(properties()[padding], end, 1);
                    }
                    position = end + (length & 1);
                    if (position > m_end)
                        throw ReadFailure{Outcome::Malformed, QStringLiteral("RF64 declared extent ends inside ds64 or another chunk.")};
                }
                if (m_rf64 && !m_haveDs64)
                    throw ReadFailure{Outcome::Malformed, QStringLiteral("RF64 is missing its ds64 chunk.")};
                if (m_end > m_input.size())
                    throw ReadFailure{Outcome::Incomplete, QStringLiteral("Audio container extends beyond the physical file.")};
                if (m_end < m_input.size())
                {
                    m_result.trailingBytes = {m_end, m_input.size() - m_end};
                    const auto trailing = range(QStringLiteral("Audio.TrailingBytes"), {}, m_end, m_input.size() - m_end);
                    properties()[trailing].interpretation = QStringLiteral("Bytes outside the declared audio container; no container or metadata meaning assumed.");
                }
                m_input.finish();
                m_result.source.outcome = m_badMetadata ? Outcome::Malformed : Outcome::Complete;
            }

        private:
            QVector<RawProperty> &properties() { return m_result.source.unownedProperties; }

            qsizetype append(const QString &name, const QByteArray &key = {})
            {
                RawProperty property;
                property.locator.name = name;
                property.locator.key = key;
                properties().push_back(std::move(property));
                return properties().size() - 1;
            }

            qsizetype range(const QString &name, const QByteArray &key, qint64 offset, qint64 length)
            {
                const auto index = append(name, key);
                auto &property = properties()[index];
                property.locator.ranges = {{offset, length}};
                property.state = PropertyReadState::Present;
                property.bytesRetained = false;
                property.interpretation = QStringLiteral("Original payload range retained without loading its bytes into RAM.");
                return index;
            }

            void invalid(qsizetype index, const QString &reason)
            {
                auto &property = properties()[index];
                property.state = PropertyReadState::Unreadable;
                property.interpretation = reason;
                m_result.source.diagnostics.push_back(QStringLiteral("%1 at byte %2: %3")
                                                          .arg(property.locator.name)
                                                          .arg(property.locator.ranges.front().offset)
                                                          .arg(reason));
                m_badMetadata = true;
            }

            quint64 resolveSize(const QByteArray &key, quint32 size)
            {
                const bool firstData = key == "data" && !m_seenData;
                if (key == "data")
                    m_seenData = true;
                if (!m_rf64 || size != sizeSentinel)
                    return size;
                if (!m_haveDs64)
                    throw ReadFailure{Outcome::Malformed, QStringLiteral("RF64 chunk uses a 64-bit size before a usable ds64.")};
                if (firstData)
                {
                    return m_dataSize;
                }
                // Equal chunk IDs are intentionally separate, ordered table entries.
                for (auto &entry : m_sizeTable)
                {
                    if (!entry.used && entry.key == key)
                    {
                        entry.used = true;
                        return entry.size;
                    }
                }
                throw ReadFailure{Outcome::Malformed, QStringLiteral("RF64 has no remaining ds64 size entry for %1.").arg(chunkName(key))};
            }

            void readChunk(const QByteArray &key, qint64 offset, qint64 length)
            {
                const QString name = QStringLiteral("Audio.") + chunkName(key);
                if (key == "omfi")
                {
                    range(name, key, offset, length);
                    m_result.omfiChunks.push_back({offset, length});
                    return;
                }
                if (key == "SSND" && m_big)
                {
                    const auto chunk = range(name, key, offset, length);
                    if (length < 8)
                    {
                        invalid(chunk, QStringLiteral("SSND requires its offset and blockSize fields."));
                        return;
                    }
                    const auto soundHeader = append(QStringLiteral("Audio.SSND.SoundHeader"));
                    m_input.read(properties()[soundHeader], offset, 8);
                    const QByteArray bytes = properties()[soundHeader].encoding;
                    const quint32 soundOffset = dword(bytes, 0, true);
                    const QVariantMap fields{{QStringLiteral("offset"), soundOffset}, {QStringLiteral("blockSize"), dword(bytes, 4, true)}};
                    properties()[soundHeader].decoded = fields;
                    properties()[chunk].decoded = fields;
                    properties()[chunk].interpretation = QStringLiteral("Sound header decoded separately; alignment bytes and sound sample data remain on disk.");
                    if (quint64(soundOffset) > quint64(length - 8))
                        invalid(chunk, QStringLiteral("SSND offset exceeds the bytes following its sound header."));
                    return;
                }
                if ((!m_big && key == "data") || key == "fill" || key == "JUNK" || key == "PAD ")
                {
                    range(name, key, offset, length);
                    return;
                }
                const bool knownMetadata = key == "fmt " || key == "COMM" || key == "ds64" || key == "bext" ||
                                           key == "iXML" || key == "axml" || key == "umid" || key == "minf" ||
                                           key == "FVER" || key == "fact" || key == "chna" || key == "CHAN";
                if (!knownMetadata)
                {
                    const auto index = range(name, key, offset, length);
                    properties()[index].interpretation = QStringLiteral("Uninterpreted chunk retained as an exact source range; its private content is not assumed to be small metadata or sound data.");
                    return;
                }
                const auto index = append(name, key);
                m_input.read(properties()[index], offset, length);
                if (key == "fmt " && !m_big)
                    waveFormat(index);
                else if (key == "COMM" && m_big)
                    common(index);
                else if (key == "ds64" && m_rf64)
                    sizes(index);
                else
                    properties()[index].interpretation = QStringLiteral("Original metadata bytes retained; no private interpretation or text-encoding guess applied.");
            }

            void waveFormat(qsizetype index)
            {
                retainFormat(index, decodeWaveFormat(properties()[index]));
            }

            void common(qsizetype index)
            {
                retainFormat(index, decodeAiffCommon(properties()[index], m_aifc));
            }

            void retainFormat(qsizetype index, AudioFormatResult decoded)
            {
                properties()[index].decoded = std::move(decoded.values);
                if (!decoded.diagnostics.isEmpty())
                    invalid(index, decoded.diagnostics.join(QLatin1Char(' ')));
                properties() += decoded.fields;
            }

            void sizes(qsizetype index)
            {
                const auto &bytes = properties()[index].encoding;
                if (m_haveDs64)
                    throw ReadFailure{Outcome::Malformed, QStringLiteral("RF64 contains more than one ds64; size selection would be ambiguous.")};
                if (bytes.size() < 28)
                {
                    invalid(index, QStringLiteral("RF64 ds64 is shorter than its fixed fields."));
                    throw ReadFailure{Outcome::Malformed, QStringLiteral("Cannot follow RF64 without a usable ds64.")};
                }
                const quint64 riffSize = qword(bytes, 0);
                const quint64 dataSize = qword(bytes, 8);
                const quint32 tableLength = dword(bytes, 24, false);
                QVariantMap fields{{QStringLiteral("riffSize"), QVariant::fromValue(riffSize)},
                                   {QStringLiteral("dataSize"), QVariant::fromValue(dataSize)},
                                   {QStringLiteral("sampleCount"), QVariant::fromValue(qword(bytes, 16))},
                                   {QStringLiteral("tableLength"), tableLength}};
                properties()[index].decoded = fields;
                if (quint64(tableLength) > quint64(bytes.size() - 28) / 12)
                {
                    invalid(index, QStringLiteral("RF64 ds64 table length exceeds its payload."));
                    throw ReadFailure{Outcome::Malformed, QStringLiteral("Cannot follow RF64 with a truncated size table.")};
                }
                QVariantList table;
                for (quint32 row = 0; row < tableLength; ++row)
                {
                    m_input.check();
                    const qsizetype offset = 28 + qsizetype(row) * 12;
                    const QByteArray key = bytes.mid(offset, 4);
                    const quint64 size = qword(bytes, offset + 4);
                    m_sizeTable.push_back({key, size, false});
                    table.push_back(QVariantMap{{QStringLiteral("chunkId"), key}, {QStringLiteral("chunkSize"), QVariant::fromValue(size)}});
                }
                fields.insert(QStringLiteral("table"), table);
                properties()[index].decoded = fields;
                if (m_extendedContainerSize)
                {
                    if (riffSize > quint64(std::numeric_limits<qint64>::max() - 8) || riffSize < 4)
                        throw ReadFailure{Outcome::Malformed, QStringLiteral("RF64 declared extent cannot be represented as a valid file range.")};
                    m_end = qint64(riffSize) + 8;
                }
                m_dataSize = dataSize;
                m_haveDs64 = true;
            }

            Input &m_input;
            AudioReadResult &m_result;
            bool m_big = false;
            bool m_rf64 = false;
            bool m_aifc = false;
            bool m_haveDs64 = false;
            bool m_seenData = false;
            bool m_extendedContainerSize = false;
            bool m_badMetadata = false;
            qint64 m_end = 0;
            quint64 m_dataSize = 0;
            QVector<SizeEntry> m_sizeTable;
        };
    }

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

    AudioReadResult readChunkedAudio(QIODevice &device, const ReaderContext &context)
    {
        AudioReadResult result;
        try
        {
            Input input(device, context.cancellation);
            AudioParser parser(input, result);
            parser.parse();
        }
        catch (const ReadFailure &failure)
        {
            result.source.outcome = failure.outcome;
            result.source.diagnostics.push_back(failure.reason);
        }
        auto receipt = QSharedPointer<SourceSnapshot>::create();
        if (context.snapshot)
            *receipt = *context.snapshot;
        receipt->source = MetadataSource::Omf;
        receipt->readState = result.source.outcome == Outcome::Complete  ? SourceReadState::Complete
                             : result.source.outcome == Outcome::IoError ? SourceReadState::Unreadable
                                                                         : SourceReadState::Incomplete;
        result.source.snapshot = receipt;
        return result;
    }
}
