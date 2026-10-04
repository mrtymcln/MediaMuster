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

namespace Canon::Detail
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
                auto &property = properties()[index];
                const auto &bytes = property.encoding;
                if (bytes.size() < 16)
                {
                    invalid(index, QStringLiteral("WAVE fmt chunk is shorter than WAVEFORMAT's fixed fields."));
                    return;
                }
                const quint16 tag = word(bytes, 0, false);
                QVariantMap fields{{QStringLiteral("wFormatTag"), tag}, {QStringLiteral("nChannels"), word(bytes, 2, false)}, {QStringLiteral("nSamplesPerSec"), dword(bytes, 4, false)}, {QStringLiteral("nAvgBytesPerSec"), dword(bytes, 8, false)}, {QStringLiteral("nBlockAlign"), word(bytes, 12, false)}, {QStringLiteral("wBitsPerSample"), word(bytes, 14, false)}};
                property.decoded = fields;
                if (bytes.size() == 17 || (tag != 1 && bytes.size() < 18))
                {
                    invalid(index, QStringLiteral("WAVEFORMATEX extension length is missing or truncated."));
                    return;
                }
                if (bytes.size() >= 18)
                {
                    const quint16 extra = word(bytes, 16, false);
                    fields.insert(QStringLiteral("cbSize"), extra);
                    property.decoded = fields;
                    if (extra > bytes.size() - 18)
                    {
                        invalid(index, QStringLiteral("WAVEFORMATEX cbSize extends beyond its fmt chunk."));
                        return;
                    }
                    if (tag == 0xfffe)
                    {
                        if (extra < 22)
                        {
                            invalid(index, QStringLiteral("WAVE_FORMAT_EXTENSIBLE requires 22 extension bytes."));
                            return;
                        }
                        const auto subformat = bytes.mid(24, 16);
                        const quint16 samples = word(bytes, 18, false);
                        fields.insert(QStringLiteral("Samples"), samples);
                        fields.insert(QStringLiteral("dwChannelMask"), dword(bytes, 20, false));
                        fields.insert(QStringLiteral("SubFormat"), subformat);
                        const auto pcm = QByteArray::fromHex("0100000000001000800000aa00389b71");
                        const auto floatingPoint = QByteArray::fromHex("0300000000001000800000aa00389b71");
                        if (subformat == pcm || subformat == floatingPoint)
                            fields.insert(QStringLiteral("wValidBitsPerSample"), samples);
                        property.interpretation = QStringLiteral("wBitsPerSample describes sample-container width. Samples is a union; valid-bit interpretation is supplied only for the recorded PCM or IEEE-float subformat. Channel mask is a speaker assignment, not a free-text channel name.");
                    }
                }
                property.decoded = fields;
            }

            void common(qsizetype index)
            {
                auto &property = properties()[index];
                const auto &bytes = property.encoding;
                if (bytes.size() < 18)
                {
                    invalid(index, QStringLiteral("AIFF COMM chunk is shorter than its fixed fields."));
                    return;
                }
                const quint16 signExponent = word(bytes, 8, true);
                const quint16 exponent = signExponent & 0x7fff;
                const quint64 mantissa = qFromBigEndian<quint64>(bytes.constData() + 10);
                QVariantMap fields{{QStringLiteral("numChannels"), qint16(word(bytes, 0, true))},
                                   {QStringLiteral("numSampleFrames"), dword(bytes, 2, true)},
                                   {QStringLiteral("sampleSize"), qint16(word(bytes, 6, true))},
                                   {QStringLiteral("sampleRateSignExponent"), signExponent},
                                   {QStringLiteral("sampleRateSignificand"), QVariant::fromValue(mantissa)}};
                // Keep the exact 80-bit representation as well as a useful approximation.
                // This is the sample rate's representation, not the sound samples' format.
                if (exponent != 0x7fff)
                {
                    double sampleRate = std::ldexp(double(mantissa), (exponent == 0 ? 1 : exponent) - 16383 - 63);
                    if (signExponent & 0x8000)
                        sampleRate = -sampleRate;
                    if (std::isfinite(sampleRate))
                        fields.insert(QStringLiteral("sampleRate"), sampleRate);
                }
                property.decoded = fields;
                property.interpretation = QStringLiteral("sampleRate preserves the original extended-precision fields; its double value may be rounded. No audio sample format is inferred from the sample-rate encoding.");
                if (!m_aifc)
                    return;
                if (bytes.size() < 23)
                {
                    invalid(index, QStringLiteral("AIFF-C COMM lacks its compression type or Pascal-string length."));
                    return;
                }
                fields.insert(QStringLiteral("compressionType"), bytes.mid(18, 4));
                const qsizetype nameLength = quint8(bytes[22]);
                fields.insert(QStringLiteral("compressionNameLength"), nameLength);
                property.decoded = fields;
                if (bytes.size() - 23 < nameLength)
                {
                    invalid(index, QStringLiteral("AIFF-C compression name extends beyond COMM."));
                    return;
                }
                const QByteArray name = bytes.mid(23, nameLength);
                fields.insert(QStringLiteral("compressionNameBytes"), name);
                const bool ascii = std::all_of(name.cbegin(), name.cend(), [](char c)
                                               { return static_cast<unsigned char>(c) < 128; });
                if (ascii)
                    fields.insert(QStringLiteral("compressionName"), QString::fromLatin1(name));
                property.decoded = fields;
                const qint64 nameOffset = property.locator.ranges.front().offset + 23;
                const auto nameIndex = append(QStringLiteral("Audio.COMM.compressionName"));
                auto &nameProperty = properties()[nameIndex];
                nameProperty.encoding = name;
                nameProperty.locator.ranges = {{nameOffset, nameLength}};
                nameProperty.state = PropertyReadState::Present;
                nameProperty.textEncoding = ascii ? TextEncoding::Ascii : TextEncoding::Unknown;
                if (ascii)
                {
                    nameProperty.decoded = QString::fromLatin1(name);
                    nameProperty.textEncodingBasis = EvidenceBasis::Derived;
                }
                nameProperty.interpretation = QStringLiteral("AIFF-C compressionName is a counted Pascal string, potentially localized. ASCII is recognized from its bytes; non-ASCII encoding is not assumed to be MacRoman or UTF-8.");
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
