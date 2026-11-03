#include "audioreader_p.h"

#include <QVector>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>

namespace Bella::Detail
{
	namespace
	{
		constexpr quint32 extendedSize = 0xffffffffU;
		constexpr qint64 labelSize = 24;

		bool tag(QByteArrayView bytes, qsizetype offset, const char *name)
		{
			return offset >= 0 && offset <= bytes.size() && bytes.size() - offset >= 4 &&
				   std::memcmp(bytes.data() + offset, name, 4) == 0;
		}

		quint32 word32(const char *bytes, bool big)
		{
			return big ? qFromBigEndian<quint32>(bytes) : qFromLittleEndian<quint32>(bytes);
		}

		bool sameRate(const OmfNativeRate &a, const OmfNativeRate &b)
		{
			return qint64(a.numerator) * b.denominator == qint64(b.numerator) * a.denominator;
		}

		// Preserve the native reader's exact signed-32 range and accepted encodings.
		// The stricter MDB/OMF AIFF decoder has different admission rules.
		std::optional<OmfNativeRate> nativeAiffRate(const char *bytes)
		{
			const quint16 signExponent = qFromBigEndian<quint16>(bytes);
			quint64 significand = qFromBigEndian<quint64>(bytes + 2);
			if ((signExponent & 0x8000U) || (signExponent & 0x7fffU) == 0x7fffU || !significand)
				return std::nullopt;
			const int exponent = int(signExponent & 0x7fffU);
			int power = (exponent == 0 ? 1 : exponent) - 16383 - 63;
			while ((significand & 1U) == 0)
			{
				significand >>= 1;
				++power;
			}
			constexpr auto maximum = quint64(std::numeric_limits<qint32>::max());
			if (power >= 0)
			{
				if (power > 30 || significand > (maximum >> power))
					return std::nullopt;
				return OmfNativeRate{qint32(significand << power), 1};
			}
			if (power < -30 || significand > maximum)
				return std::nullopt;
			return OmfNativeRate{qint32(significand), qint32(quint32{1} << -power)};
		}

		template <typename Value>
		struct Choice
		{
			std::optional<Value> value;
			bool conflict = false;
		};

		template <typename Value, typename Equal>
		bool offer(Choice<Value> &choice, const std::optional<Value> &next, Equal equal)
		{
			if (!next || choice.conflict)
				return false;
			if (!choice.value)
				choice.value = next;
			else if (!equal(*choice.value, *next))
			{
				choice.value.reset();
				choice.conflict = true;
				return true;
			}
			return false;
		}

		template <typename Value>
		bool offer(Choice<Value> &choice, const std::optional<Value> &next)
		{
			return offer(choice, next, [](const Value &a, const Value &b)
						 { return a == b; });
		}

		struct SizeEntry
		{
			std::array<char, 4> key{};
			quint64 size = 0;
			bool used = false;
		};

		struct WaveCountFormat
		{
			quint16 blockAlign = 0;
			std::optional<OmfNativeRate> rate;
		};

		class NativeReader
		{
		public:
			NativeReader(QFile &file, const std::atomic_bool &stop) : m_file(file), m_stop(stop) {}

			NativeAudioRead run(QByteArrayView header)
			{
				if (cancelled())
					return std::move(m_result);
				if (!m_file.isOpen() || !m_file.isReadable() || m_file.isSequential() || m_file.isTextModeEnabled())
				{
					fail(OmfReadResult::IoError, QStringLiteral("Native audio requires an open readable binary file."));
					return std::move(m_result);
				}
				m_extent = m_file.size();
				if (m_extent < 0)
					fail(OmfReadResult::IoError, QStringLiteral("Native audio file length is unavailable."));
				else if (header.size() < 12)
					fail(OmfReadResult::Incomplete, QStringLiteral("Native audio container header is incomplete."));
				else
					walk(header.first(12));
				finishValues();
				return std::move(m_result);
			}

		private:
			bool fail(OmfReadResult reading, const QString &reason)
			{
				m_result.reading = reading;
				m_result.problemNotes.append(reason);
				return false;
			}

			bool cancelled()
			{
				if (!m_stop.load(std::memory_order_relaxed))
					return false;
				if (m_result.reading != OmfReadResult::Cancelled)
					fail(OmfReadResult::Cancelled, QStringLiteral("Native audio metadata read cancelled."));
				return true;
			}

			void malformed(const QString &reason)
			{
				m_badMetadata = true;
				m_result.problemNotes.append(reason);
			}

			bool read(qint64 offset, char *destination, qint64 length)
			{
				if (cancelled())
					return false;
				if (offset < 0 || offset > m_extent || length < 0 || length > m_extent - offset)
					return fail(OmfReadResult::Incomplete, QStringLiteral("Native audio metadata range extends beyond the physical file."));
				if (!m_file.seek(offset))
					return fail(OmfReadResult::IoError, QStringLiteral("Cannot seek to native audio byte %1: %2").arg(offset).arg(m_file.errorString()));
				qint64 received = 0;
				while (received < length)
				{
					if (cancelled())
						return false;
					const auto count = m_file.read(destination + received, length - received);
					if (cancelled())
						return false;
					if (count <= 0)
						return fail(count < 0 ? OmfReadResult::IoError : OmfReadResult::Incomplete,
									QStringLiteral("Cannot read native audio metadata at byte %1: %2").arg(offset + received).arg(m_file.errorString()));
					received += count;
				}
				return true;
			}

			void walk(QByteArrayView header)
			{
				m_big = tag(header, 0, "FORM");
				m_rf64 = tag(header, 0, "RF64");
				m_aifc = m_big && tag(header, 8, "AIFC");
				const bool wave = (tag(header, 0, "RIFF") || m_rf64) && tag(header, 8, "WAVE");
				if (!wave && !(m_big && (m_aifc || tag(header, 8, "AIFF"))))
				{
					fail(OmfReadResult::Unsupported, QStringLiteral("Native audio is not supported RIFF/RF64 WAVE or FORM AIFF/AIFF-C."));
					return;
				}
				const quint32 declared = word32(header.data() + 4, m_big);
				m_extendedContainer = m_rf64 && declared == extendedSize;
				m_end = m_extendedContainer ? m_extent : qint64(declared) + 8;
				m_result.declaredContainerEnd = m_end;
				m_result.reading = OmfReadResult::Completed;
				if (m_end < 12)
				{
					fail(OmfReadResult::Malformed, QStringLiteral("Native audio container is smaller than its form header."));
					return;
				}
				qint64 position = 12;
				while (position < m_end)
				{
					if (m_end - position < 8)
					{
						fail(m_end > m_extent ? OmfReadResult::Incomplete : OmfReadResult::Malformed,
							 QStringLiteral("Native audio container ends inside a chunk header."));
						return;
					}
					std::array<char, 8> bytes{};
					if (!read(position, bytes.data(), qint64(bytes.size())))
						return;
					const QByteArrayView chunk(bytes.data(), bytes.size());
					if (m_rf64 && position == 12 && !tag(chunk, 0, "ds64"))
					{
						fail(OmfReadResult::Malformed, QStringLiteral("RF64 requires ds64 as its first chunk."));
						return;
					}
					const auto size = chunkSize(chunk.first(4), word32(bytes.data() + 4, m_big));
					if (!size)
						return;
					const qint64 payload = position + 8;
					if (payload > m_extent || *size > quint64(m_extent - payload))
					{
						fail(OmfReadResult::Incomplete, QStringLiteral("Native audio chunk at byte %1 extends beyond the physical file.").arg(position));
						return;
					}
					if (*size > quint64(m_end - payload))
					{
						fail(OmfReadResult::Malformed, QStringLiteral("Native audio chunk at byte %1 extends beyond its declared container.").arg(position));
						return;
					}
					const qint64 length = qint64(*size);
					if (!readChunk(chunk.first(4), payload, length))
						return;
					const qint64 end = payload + length;
					if (end > m_end || ((length & 1) && end == m_end))
					{
						fail(OmfReadResult::Malformed, QStringLiteral("Native audio chunk has no complete aligned extent inside its container."));
						return;
					}
					if ((length & 1) && end == m_extent)
					{
						fail(OmfReadResult::Incomplete, QStringLiteral("Native audio chunk alignment byte is outside the physical file."));
						return;
					}
					position = end + (length & 1);
				}
				if (m_rf64 && !m_haveDs64)
				{
					fail(OmfReadResult::Malformed, QStringLiteral("RF64 has no usable ds64 chunk."));
					return;
				}
				if (cancelled())
					return;
				m_result.locationDiscoveryComplete = true;
				if (m_badMetadata)
					m_result.reading = OmfReadResult::Malformed;
			}

			std::optional<quint64> chunkSize(QByteArrayView key, quint32 ordinary)
			{
				const bool firstData = tag(key, 0, "data") && !m_seenData;
				if (tag(key, 0, "data"))
					m_seenData = true;
				if (!m_rf64 || ordinary != extendedSize)
					return ordinary;
				if (!m_haveDs64)
				{
					fail(OmfReadResult::Malformed, QStringLiteral("RF64 uses an extended chunk size before a usable ds64."));
					return std::nullopt;
				}
				if (firstData)
					return m_primaryDataSize;
				for (auto &entry : m_sizes)
					if (!entry.used && std::memcmp(entry.key.data(), key.data(), 4) == 0)
					{
						entry.used = true;
						return entry.size;
					}
				fail(OmfReadResult::Malformed, QStringLiteral("RF64 has no remaining ds64 entry for an extended chunk size."));
				return std::nullopt;
			}

			bool sizes(qint64 offset, qint64 length)
			{
				if (m_haveDs64 || length < 28)
					return fail(OmfReadResult::Malformed, m_haveDs64 ? QStringLiteral("RF64 contains conflicting repeated ds64 chunks.")
																	 : QStringLiteral("RF64 ds64 has an incomplete fixed prefix."));
				std::array<char, 28> bytes{};
				if (!read(offset, bytes.data(), qint64(bytes.size())))
					return false;
				const quint64 riffSize = qFromLittleEndian<quint64>(bytes.data());
				const quint32 count = qFromLittleEndian<quint32>(bytes.data() + 24);
				if (quint64(count) > quint64(length - 28) / 12)
					return fail(OmfReadResult::Malformed, QStringLiteral("RF64 ds64 size table exceeds its bounded chunk."));
				if (m_extendedContainer)
				{
					if (riffSize < 4 || riffSize > quint64(std::numeric_limits<qint64>::max() - 8))
						return fail(OmfReadResult::Malformed, QStringLiteral("RF64 declared extent is not a supported file range."));
					m_end = qint64(riffSize) + 8;
					m_result.declaredContainerEnd = m_end;
					if (offset + length > m_end || ((length & 1) && offset + length == m_end))
						return fail(OmfReadResult::Malformed, QStringLiteral("RF64 declared extent ends inside its ds64 chunk."));
				}
				m_primaryDataSize = qFromLittleEndian<quint64>(bytes.data() + 8);
				for (quint32 index = 0; index < count; ++index)
				{
					std::array<char, 12> row{};
					if (!read(offset + 28 + qint64(index) * 12, row.data(), qint64(row.size())))
						return false;
					SizeEntry entry;
					std::copy_n(row.begin(), entry.key.size(), entry.key.begin());
					entry.size = qFromLittleEndian<quint64>(row.data() + 4);
					m_sizes.append(entry);
				}
				m_haveDs64 = true;
				return true;
			}

			bool readChunk(QByteArrayView key, qint64 offset, qint64 length)
			{
				if (tag(key, 0, "omfi"))
				{
					if (m_result.omfLocation)
						m_result.multipleOmfLocations = true;
					else
						m_result.omfLocation = EmbeddedOmfLocation{offset, length, length >= labelSize ? std::optional<qint64>{offset + length - labelSize} : std::nullopt};
				}
				else if (!m_big && tag(key, 0, "data"))
				{
					++m_dataChunks;
					m_dataLength = length;
				}
				else if (m_rf64 && tag(key, 0, "ds64"))
					return sizes(offset, length);
				else if (!m_big && tag(key, 0, "fmt "))
					return waveFormat(offset, length);
				else if (m_big && tag(key, 0, "COMM"))
					return aiffCommon(offset, length);
				else if (m_big && tag(key, 0, "SSND"))
				{
					if (length < 8)
						malformed(QStringLiteral("Native AIFF SSND has no complete offset/block-size prefix."));
					else
					{
						std::array<char, 8> bytes{};
						if (!read(offset, bytes.data(), qint64(bytes.size())))
							return false;
						if (quint64(qFromBigEndian<quint32>(bytes.data())) > quint64(length - 8))
							malformed(QStringLiteral("Native AIFF SSND alignment offset exceeds its bounded sound chunk."));
					}
				}
				return true;
			}

			void formatValues(const std::optional<OmfNativeRate> &rate, const std::optional<quint16> &bits,
							  const std::optional<OmfAudioEncoding> &encoding)
			{
				m_result.information.kind = OmfKind::Audio;
				if (offer(m_rate, rate, sameRate))
					m_result.problemNotes.append(QStringLiteral("Native audio format chunks disagree on sample frequency."));
				if (offer(m_bits, bits))
					m_result.problemNotes.append(QStringLiteral("Native audio format chunks disagree on sample precision."));
				if (offer(m_encoding, encoding))
					m_result.problemNotes.append(QStringLiteral("Native audio format chunks disagree on audio encoding."));
			}

			bool waveFormat(qint64 offset, qint64 length)
			{
				if (length < 16)
				{
					malformed(QStringLiteral("Native WAVE fmt has an incomplete required prefix."));
					return true;
				}
				std::array<char, 40> bytes{};
				const qint64 available = std::min(length, qint64(bytes.size()));
				if (!read(offset, bytes.data(), available))
					return false;
				const quint16 format = qFromLittleEndian<quint16>(bytes.data());
				const quint32 frequency = qFromLittleEndian<quint32>(bytes.data() + 4);
				const quint16 align = qFromLittleEndian<quint16>(bytes.data() + 12);
				const quint16 storage = qFromLittleEndian<quint16>(bytes.data() + 14);
				const std::optional<OmfNativeRate> rate = frequency > 0 && frequency <= quint32(std::numeric_limits<qint32>::max())
															  ? std::optional<OmfNativeRate>{OmfNativeRate{qint32(frequency), 1}}
															  : std::nullopt;
				std::optional<quint16> precision = storage;
				std::optional<OmfAudioEncoding> encoding;
				quint16 subtype = format;
				bool extensionDamaged = false;
				if (!(format == 1 && length == 16))
				{
					if (length < 18)
					{
						malformed(QStringLiteral("Native WAVE format has no complete extension-size field."));
						extensionDamaged = true;
					}
					else if (qFromLittleEndian<quint16>(bytes.data() + 16) > quint64(length - 18))
					{
						malformed(QStringLiteral("Native WAVE extension size exceeds its bounded fmt chunk."));
						extensionDamaged = true;
					}
				}
				if (format == 0xfffe)
				{
					precision.reset();
					if (length < 40)
					{
						if (!extensionDamaged)
							malformed(QStringLiteral("Native WAVE extensible format has incomplete required fields."));
					}
					else if (length >= 40 && qFromLittleEndian<quint16>(bytes.data() + 16) < 22)
						malformed(QStringLiteral("Native WAVE extensible format declares fewer than 22 extension bytes."));
					else if (length >= 40)
					{
						constexpr std::array<quint8, 16> pcm{1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
						constexpr std::array<quint8, 16> floating{3, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
						if (std::memcmp(bytes.data() + 24, pcm.data(), pcm.size()) == 0)
							subtype = 1;
						else if (std::memcmp(bytes.data() + 24, floating.data(), floating.size()) == 0)
							subtype = 3;
						if (subtype == 1 || subtype == 3)
						{
							precision = qFromLittleEndian<quint16>(bytes.data() + 18);
							if (*precision > storage)
							{
								malformed(QStringLiteral("Native WAVE valid precision exceeds its recorded storage width."));
								precision.reset();
							}
						}
					}
				}
				if (subtype == 1)
					encoding = OmfAudioEncoding::Pcm;
				else if (subtype == 3)
					encoding = OmfAudioEncoding::IeeeFloat;
				if (precision && (*precision == 0 || *precision > 64))
					precision.reset();
				formatValues(rate, precision, encoding);
				if (encoding)
					m_waveCounts.append({align, rate});
				return true;
			}

			bool aiffCommon(qint64 offset, qint64 length)
			{
				if (length < 18)
				{
					malformed(QStringLiteral("Native AIFF COMM has an incomplete required prefix."));
					return true;
				}
				std::array<char, 23> bytes{};
				if (!read(offset, bytes.data(), std::min(length, m_aifc ? qint64(bytes.size()) : qint64(18))))
					return false;
				const qint16 recorded = qFromBigEndian<qint16>(bytes.data() + 6);
				const std::optional<quint16> bits = recorded > 0 && recorded <= 64 ? std::optional<quint16>{quint16(recorded)} : std::nullopt;
				std::optional<OmfAudioEncoding> encoding;
				if (!m_aifc)
					encoding = OmfAudioEncoding::Pcm;
				else if (length < 22)
					malformed(QStringLiteral("Native AIFF-C COMM encoding and compression-name framing are incomplete; base fields remain usable."));
				else
				{
					const QByteArrayView body(bytes.data(), qsizetype(std::min(length, qint64(bytes.size()))));
					if (tag(body, 18, "NONE") || tag(body, 18, "twos") || tag(body, 18, "sowt") || tag(body, 18, "in24") || tag(body, 18, "in32"))
						encoding = OmfAudioEncoding::Pcm;
					else if (tag(body, 18, "fl32") || tag(body, 18, "FL32") || tag(body, 18, "fl64") || tag(body, 18, "FL64"))
						encoding = OmfAudioEncoding::IeeeFloat;
				}
				if (m_aifc && length >= 22 && (length < 23 || quint64(static_cast<unsigned char>(bytes[22])) > quint64(length - 23)))
					malformed(QStringLiteral("Native AIFF-C compression name is incomplete; independent format fields remain usable."));
				formatValues(nativeAiffRate(bytes.data() + 8), bits, encoding);
				if (offer(m_aiffCount, std::optional<qint64>{qFromBigEndian<quint32>(bytes.data() + 2)}))
					m_result.problemNotes.append(QStringLiteral("Native AIFF format chunks disagree on sample-frame count."));
				return true;
			}

			void finishValues()
			{
				m_result.information.samplingRate = m_rate.value;
				m_result.information.bitDepth = m_bits.value;
				m_result.information.encoding = m_encoding.value.value_or(OmfAudioEncoding::Unknown);
				if (m_big)
				{
					m_result.information.sampleFrameCount = m_aiffCount.value;
					return;
				}
				if (m_dataChunks != 1)
					return;
				Choice<qint64> frames;
				for (const auto &format : m_waveCounts)
				{
					if (!format.blockAlign || m_dataLength % format.blockAlign != 0)
						continue;
					// Do not manufacture timing from one fmt's alignment and another's clock.
					if (m_rate.value && (!format.rate || !sameRate(*m_rate.value, *format.rate)))
						continue;
					if (offer(frames, std::optional<qint64>{m_dataLength / format.blockAlign}))
						m_result.problemNotes.append(QStringLiteral("Native WAVE format chunks disagree on sample-frame count."));
				}
				m_result.information.sampleFrameCount = frames.value;
			}

			QFile &m_file;
			const std::atomic_bool &m_stop;
			NativeAudioRead m_result;
			qint64 m_extent = 0;
			qint64 m_end = 0;
			bool m_big = false;
			bool m_rf64 = false;
			bool m_aifc = false;
			bool m_extendedContainer = false;
			bool m_haveDs64 = false;
			bool m_seenData = false;
			bool m_badMetadata = false;
			quint64 m_primaryDataSize = 0;
			quint64 m_dataChunks = 0;
			qint64 m_dataLength = 0;
			QVector<SizeEntry> m_sizes;
			QVector<WaveCountFormat> m_waveCounts;
			Choice<OmfNativeRate> m_rate;
			Choice<quint16> m_bits;
			Choice<OmfAudioEncoding> m_encoding;
			Choice<qint64> m_aiffCount;
		};
	}

	NativeAudioRead readNativeAudio(QFile &file, QByteArrayView first12Bytes, const std::atomic_bool &stopRequested)
	{
		return NativeReader(file, stopRequested).run(first12Bytes);
	}
}
