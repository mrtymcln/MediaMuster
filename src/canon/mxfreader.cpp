// Walks MXF framing, seeking over recording payloads and padding. Header metadata
// is scoped by each partition and its own Primer Pack. No first/last header wins:
// every encountered set and property remains available for later reconciliation.

#include "mxfreader.h"
#include "mxfobjects_p.h"

#include <QHash>
#include <QSet>
#include <QVariantMap>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <limits>

namespace Canon
{
	namespace
	{
		using Outcome = ParsedSource::Outcome;
		constexpr qint64 keySize = 16;
		constexpr qint64 maximumRunIn = 65535; // ST 377-1, not an application memory limit.
		const QByteArray partitionPrefix = QByteArray::fromHex("060e2b34020501010d01020101");
		const QByteArray primerKey = QByteArray::fromHex("060e2b34020501010d01020101050100");
		const QByteArray fillKey = QByteArray::fromHex("060e2b34010101020301021001000000");
		const QByteArray ripKey = QByteArray::fromHex("060e2b34020501010d01020101110100");
		const QByteArray indexKey = QByteArray::fromHex("060e2b34025301010d01020101100100");
		const QByteArray avidRootKey = QByteArray::fromHex("8053080036210804b3b398a51c9011d4");
		const QByteArray avidDirectoryKey = QByteArray::fromHex("9613b38a87348746f10296f056e04d2a");

		struct Failure
		{
			Outcome outcome;
			QString reason;
		};

		void check(const Cancellation &cancellation)
		{
			if (cancellation.cancelled())
				throw Failure{Outcome::Cancelled, QStringLiteral("MXF metadata read cancelled.")};
		}

		bool sameUl(const QByteArray &a, const QByteArray &b)
		{
			return a.size() == b.size() && a.size() >= 8 && a.first(7) == b.first(7) && a.sliced(8) == b.sliced(8);
		}

		bool partitionKey(const QByteArray &key)
		{
			return key.size() == keySize && sameUl(key.first(partitionPrefix.size()), partitionPrefix) && quint8(key[13]) >= 2 && quint8(key[13]) <= 4 && ((quint8(key[14]) >= 1 && quint8(key[14]) <= 4) || (key[13] == 3 && key[14] == 0x11)) && key[15] == 0;
		}

		bool recordingKey(const QByteArray &key)
		{
			// GC/Avid essence and GC system items are never Header Metadata,
			// even when a damaged HeaderByteCount claims to contain them.
			if (key.size() == keySize && key.first(5) == QByteArray::fromHex("060e2b3402") && key.mid(8, 4) == QByteArray::fromHex("0d010301"))
				return true;
			static const std::array<QByteArray, 2> prefixes{
				QByteArray::fromHex("060e2b34010201010d010301"),
				QByteArray::fromHex("060e2b34010201010e040301")};
			return std::any_of(prefixes.begin(), prefixes.end(), [&](const QByteArray &prefix)
							   { return sameUl(key.first(prefix.size()), prefix); });
		}

		bool localSetKey(const QByteArray &key)
		{
			return key.size() == keySize && key.first(5) == QByteArray::fromHex("060e2b3402") && (quint8(key[5]) & 7) == 3 && key[6] == 1;
		}

		quint16 u16(const QByteArray &bytes, qsizetype offset)
		{
			return qFromBigEndian<quint16>(bytes.constData() + offset);
		}

		quint32 u32(const QByteArray &bytes, qsizetype offset)
		{
			return qFromBigEndian<quint32>(bytes.constData() + offset);
		}

		quint64 u64(const QByteArray &bytes, qsizetype offset)
		{
			return qFromBigEndian<quint64>(bytes.constData() + offset);
		}

		void qualify(ParsedSource &result, Outcome outcome, const QString &reason)
		{
			// A later incomplete pointer must not hide an established framing error.
			if (result.outcome == Outcome::Complete || result.outcome == Outcome::NotRead || outcome == Outcome::Malformed)
				result.outcome = outcome;
			result.diagnostics.append(reason);
		}

		class Input
		{
		public:
			Input(QIODevice &device, const Cancellation &cancellation)
				: m_device(device), m_cancellation(cancellation), m_extent(device.size()) {}

			qint64 size() const { return m_extent; }

			void read(RawProperty &property, qint64 offset, qint64 length)
			{
				check(m_cancellation);
				property.state = PropertyReadState::Unreadable;
				property.locator.ranges.append({offset, 0});
				if (offset < 0 || offset > m_extent || length < 0)
					throw Failure{Outcome::Incomplete, QStringLiteral("MXF read lies outside the physical source.")};
				if (!m_device.seek(offset))
					throw Failure{Outcome::IoError, QStringLiteral("Cannot seek to MXF byte %1: %2").arg(offset).arg(m_device.errorString())};
				std::array<char, 16384> buffer{};
				while (length)
				{
					check(m_cancellation);
					const qint64 available = m_extent - offset - property.locator.ranges.back().length;
					if (available == 0)
						throw Failure{Outcome::Incomplete, QStringLiteral("MXF ends while reading %1.").arg(property.locator.name)};
					const qint64 count = std::min({length, available, qint64(buffer.size())});
					const qint64 got = m_device.read(buffer.data(), count);
					if (got <= 0)
						throw Failure{got == 0 && m_device.atEnd() ? Outcome::Incomplete : Outcome::IoError,
									  QStringLiteral("Cannot read MXF %1: %2").arg(property.locator.name, m_device.errorString())};
					property.encoding.append(buffer.data(), got);
					property.locator.ranges.back().length += got;
					length -= got;
				}
				property.state = PropertyReadState::Present;
			}

			quint64 ber(RawProperty &property, qint64 &position, qint64 limit)
			{
				const auto firstIndex = property.encoding.size();
				if (position >= limit)
					throw Failure{Outcome::Malformed, QStringLiteral("MXF BER length is missing inside its enclosing value.")};
				read(property, position++, 1);
				const quint8 first = quint8(property.encoding[firstIndex]);
				if (first < 0x80)
					return first;
				const quint8 width = first & 0x7f;
				if (width == 0 || width > 8)
					throw Failure{Outcome::Malformed, QStringLiteral("MXF requires a definite BER length of at most eight value bytes.")};
				if (width > limit - position)
					throw Failure{Outcome::Malformed, QStringLiteral("MXF BER length crosses its enclosing value.")};
				read(property, position, width);
				position += width;
				quint64 length = 0;
				for (qsizetype i = firstIndex + 1; i < property.encoding.size(); ++i)
					length = (length << 8) | quint8(property.encoding[i]);
				return length;
			}

			void finish() const
			{
				check(m_cancellation);
				if (m_device.size() != m_extent)
					throw Failure{Outcome::Incomplete, QStringLiteral("MXF source length changed during reading; check this source again.")};
			}

		private:
			QIODevice &m_device;
			const Cancellation &m_cancellation;
			qint64 m_extent;
		};

		struct Packet
		{
			QByteArray key;
			qint64 offset = 0;
			qint64 valueOffset = 0;
			qint64 length = 0;
			bool fits = false;
		};

		struct Partition
		{
			qint64 offset = -1;
			quint64 previous = 0;
			quint64 footer = 0;
			quint64 headerBytes = 0;
			qint64 headerStart = -1;
			qint64 headerEnd = -1;
			qint64 primerOffset = -1;
			int leadingFills = 0;
			QHash<quint16, QByteArray> mappings;
			QSet<quint16> ambiguousTags;
		};

		class Reader
		{
		public:
			Reader(Input &input, const Cancellation &cancellation, ParsedSource &result)
				: m_input(input), m_cancellation(cancellation), m_result(result) {}

			void read()
			{
				m_runIn = findHeader();
				m_result.container = ParsedSource::Container::Mxf;
				qint64 position = m_runIn;
				while (position < m_input.size())
				{
					check(m_cancellation);
					const Packet packet = klv(position);
					if (partitionKey(packet.key))
					{
						finishPartition(packet.offset);
						partition(packet);
					}
					else
					{
						const bool header = inHeader(packet);
						QByteArray candidate = packet.key;
						if (localSetKey(candidate))
							candidate[5] = 0x53;
						const bool index = sameUl(candidate, indexKey);
						if (sameUl(packet.key, fillKey))
							range(packet, QStringLiteral("MXF.Fill"), QStringLiteral("Padding retained by range; no metadata value is inferred."));
						else if (recordingKey(packet.key) || index)
						{
							range(packet, index ? QStringLiteral("MXF.IndexTable") : QStringLiteral("MXF.RecordingData"),
								  QStringLiteral("Recording/index value retained by range; no Header Metadata Primer is applied."));
							if (header)
								qualify(m_result, Outcome::Malformed, QStringLiteral("HeaderByteCount includes a recording or index KLV; payload bytes were not loaded."));
						}
						else if (header && sameUl(packet.key, primerKey))
							primer(packet);
						else if (header && (localSetKey(packet.key) || packet.key == avidRootKey))
							localSet(packet);
						else if (sameUl(packet.key, ripKey))
							randomIndex(packet);
						else if (header && packet.key == avidDirectoryKey)
							value(packet, QStringLiteral("MXF.AvidObjectDirectory"));
						else if (header)
							range(packet, QStringLiteral("MXF.UninterpretedHeaderValue"),
								  QStringLiteral("Unknown header KLV retained by exact range. Its private payload is not assumed to be small metadata or recording data."));
						else
						{
							range(packet, QStringLiteral("MXF.BodyValue"),
								  QStringLiteral("Value outside Header Metadata retained by range. No header Primer or private payload interpretation is applied."));
							if (sameUl(packet.key.first(partitionPrefix.size()), partitionPrefix))
								qualify(m_result, Outcome::Unsupported, QStringLiteral("Unrecognised Partition Pack key retained without interpreting a new partition variant."));
						}
					}
					if (!packet.fits)
						throw Failure{Outcome::Incomplete, QStringLiteral("MXF KLV at byte %1 extends beyond the physical file; available metadata is retained.").arg(packet.offset)};
					position = packet.valueOffset + packet.length;
				}
				finishPartition(position);
				checkPointers();
			}

		private:
			RawProperty &append(const QString &name, const QByteArray &key = {})
			{
				auto &property = m_result.unownedProperties.emplaceBack();
				property.locator.name = name;
				property.locator.key = key;
				return property;
			}

			qint64 findHeader()
			{
				auto &probe = append(QStringLiteral("MXF.RunIn"));
				m_input.read(probe, 0, std::min(keySize, m_input.size()));
				qint64 offset = 0;
				while (probe.encoding.size() >= keySize)
				{
					const QByteArray key = probe.encoding.last(keySize);
					if (partitionKey(key) && key[13] == 2)
					{
						probe.encoding.chop(keySize);
						probe.locator.ranges = {{0, offset}};
						probe.interpretation = QStringLiteral("Bytes preceding the first Header Partition; partition offsets are relative to the following key.");
						return offset;
					}
					if (offset == maximumRunIn || offset + keySize >= m_input.size())
						break;
					m_input.read(probe, offset + keySize, 1);
					++offset;
				}
				throw Failure{m_input.size() < keySize ? Outcome::Incomplete : Outcome::Unsupported,
							  QStringLiteral("No MXF Header Partition was established within the permitted run-in.")};
			}

			Packet klv(qint64 offset)
			{
				auto &framing = append(QStringLiteral("MXF.KlvHeader"));
				m_input.read(framing, offset, keySize);
				Packet packet;
				packet.key = framing.encoding;
				packet.offset = offset;
				framing.locator.key = packet.key;
				qint64 position = offset + keySize;
				// A KLV may declare more than the physical bytes available. Its BER is
				// read independently so a truncated payload can still retain evidence.
				if (position >= m_input.size())
					throw Failure{Outcome::Incomplete, QStringLiteral("MXF ends before its KLV length.")};
				const quint64 length = m_input.ber(framing, position, std::numeric_limits<qint64>::max());
				framing.decoded = QVariantMap{{QStringLiteral("length"), QVariant::fromValue(length)}};
				if (length > quint64(std::numeric_limits<qint64>::max() - position))
					throw Failure{Outcome::Malformed, QStringLiteral("MXF KLV extent exceeds the supported file-offset range.")};
				packet.valueOffset = position;
				packet.length = qint64(length);
				packet.fits = packet.length <= m_input.size() - position;
				return packet;
			}

			RawProperty &value(const Packet &packet, const QString &name)
			{
				auto &property = append(name, packet.key);
				m_input.read(property, packet.valueOffset, std::min(packet.length, m_input.size() - packet.valueOffset));
				if (!packet.fits)
					property.state = PropertyReadState::Unreadable;
				return property;
			}

			void range(const Packet &packet, const QString &name, const QString &reason)
			{
				auto &property = append(name, packet.key);
				property.locator.ranges = {{packet.valueOffset, std::min(packet.length, m_input.size() - packet.valueOffset)}};
				property.state = packet.fits ? PropertyReadState::Present : PropertyReadState::Unreadable;
				property.bytesRetained = false;
				property.interpretation = reason;
			}

			void partition(const Packet &packet)
			{
				auto &property = value(packet, QStringLiteral("MXF.PartitionPack"));
				const auto &bytes = property.encoding;
				if (bytes.size() >= 2 && u16(bytes, 0) != 1)
				{
					property.interpretation = QStringLiteral("Unrecognised major version; original bytes retained without applying the version-1 partition layout.");
					throw Failure{Outcome::Unsupported, property.interpretation};
				}
				if (bytes.size() < 88)
					throw Failure{packet.fits ? Outcome::Malformed : Outcome::Incomplete, QStringLiteral("MXF Partition Pack lacks its fixed fields.")};
				const quint64 thisPartition = u64(bytes, 8);
				Partition current;
				current.offset = packet.offset;
				current.previous = u64(bytes, 16);
				current.footer = u64(bytes, 24);
				current.headerBytes = u64(bytes, 32);
				property.decoded = QVariantMap{
					{QStringLiteral("majorVersion"), u16(bytes, 0)}, {QStringLiteral("minorVersion"), u16(bytes, 2)}, {QStringLiteral("kagSize"), u32(bytes, 4)}, {QStringLiteral("thisPartition"), QVariant::fromValue(thisPartition)}, {QStringLiteral("previousPartition"), QVariant::fromValue(current.previous)}, {QStringLiteral("footerPartition"), QVariant::fromValue(current.footer)}, {QStringLiteral("headerByteCount"), QVariant::fromValue(current.headerBytes)}, {QStringLiteral("indexByteCount"), QVariant::fromValue(u64(bytes, 40))}, {QStringLiteral("indexSID"), u32(bytes, 48)}, {QStringLiteral("bodyOffset"), QVariant::fromValue(u64(bytes, 52))}, {QStringLiteral("bodySID"), u32(bytes, 60)}, {QStringLiteral("operationalPattern"), bytes.mid(64, 16)}, {QStringLiteral("kind"), quint8(packet.key[13])}, {QStringLiteral("status"), quint8(packet.key[14])}};
				m_partition = std::move(current);
				m_partitions.insert(packet.offset, packet.key);
				m_pointerRecords.append({packet.offset, m_partition.previous, m_partition.footer});
				if (thisPartition != quint64(packet.offset - m_runIn))
					qualify(m_result, Outcome::Malformed, QStringLiteral("Partition ThisPartition disagrees with its physical key location at byte %1.").arg(packet.offset));
				if (packet.key[13] == 2 && !m_partition.headerBytes)
					qualify(m_result, Outcome::Malformed, QStringLiteral("Header Partition declares no Header Metadata bytes; no property mappings are inferred from body data."));
				const quint32 count = u32(bytes, 80), stride = u32(bytes, 84);
				if ((count && stride != 16) || quint64(count) * stride > quint64(bytes.size() - 88))
				{
					property.state = PropertyReadState::Unreadable;
					qualify(m_result, Outcome::Malformed, QStringLiteral("Partition essence-container batch has an invalid stride or extent."));
					return;
				}
				QVariantList labels;
				for (quint32 i = 0; i < count; ++i)
				{
					check(m_cancellation);
					labels.append(bytes.mid(88 + qsizetype(i) * 16, 16));
				}
				auto fields = property.decoded.toMap();
				fields.insert(QStringLiteral("essenceContainerCount"), count);
				fields.insert(QStringLiteral("essenceContainerStride"), stride);
				fields.insert(QStringLiteral("essenceContainers"), labels);
				property.decoded = fields;
				if (quint64(bytes.size() - 88) != quint64(count) * stride)
					property.interpretation = QStringLiteral("Trailing Partition Pack bytes retained without assigning private semantics.");
			}

			bool inHeader(const Packet &packet)
			{
				if (m_partition.offset < 0 || !m_partition.headerBytes)
					return false;
				if (m_partition.headerStart < 0)
				{
					if (sameUl(packet.key, fillKey))
					{
						if (++m_partition.leadingFills > 1)
							qualify(m_result, Outcome::Malformed, QStringLiteral("More than one Fill precedes this partition's Header Metadata."));
						return false;
					}
					m_partition.headerStart = packet.offset;
					if (m_partition.headerBytes > quint64(std::numeric_limits<qint64>::max() - packet.offset))
						throw Failure{Outcome::Malformed, QStringLiteral("HeaderByteCount exceeds the supported offset range.")};
					m_partition.headerEnd = packet.offset + qint64(m_partition.headerBytes);
					if (!sameUl(packet.key, primerKey))
						qualify(m_result, Outcome::Incomplete, QStringLiteral("Header Metadata does not begin with a Primer Pack; local tags have no established mapping."));
				}
				if (packet.offset >= m_partition.headerEnd)
					return false;
				if (packet.valueOffset > m_partition.headerEnd || packet.length > m_partition.headerEnd - packet.valueOffset)
					throw Failure{Outcome::Malformed, QStringLiteral("KLV at byte %1 crosses the declared HeaderByteCount boundary.").arg(packet.offset)};
				return true;
			}

			void primer(const Packet &packet)
			{
				auto &property = value(packet, QStringLiteral("MXF.PrimerPack"));
				const auto &bytes = property.encoding;
				if (m_partition.primerOffset >= 0)
				{
					qualify(m_result, Outcome::Malformed, QStringLiteral("More than one Primer occurs in a Header Metadata section; subsequent mappings are not selected."));
					m_partition.ambiguousTags.unite(QSet<quint16>(m_partition.mappings.keyBegin(), m_partition.mappings.keyEnd()));
					m_partition.mappings.clear();
					return;
				}
				m_partition.primerOffset = packet.offset;
				if (bytes.size() < 8 || u32(bytes, 4) != 18 || quint64(u32(bytes, 0)) * 18 != quint64(bytes.size() - 8))
				{
					property.state = PropertyReadState::Unreadable;
					qualify(m_result, packet.fits ? Outcome::Malformed : Outcome::Incomplete, QStringLiteral("Primer batch count/stride does not match its original payload."));
					return;
				}
				QVariantList entries;
				for (quint32 i = 0; i < u32(bytes, 0); ++i)
				{
					check(m_cancellation);
					const qsizetype offset = 8 + qsizetype(i) * 18;
					const quint16 tag = u16(bytes, offset);
					const QByteArray ul = bytes.mid(offset + 2, 16);
					entries.append(QVariantMap{{QStringLiteral("localTag"), tag}, {QStringLiteral("ul"), ul}});
					if (m_partition.mappings.contains(tag))
					{
						if (m_partition.mappings.value(tag) != ul)
							m_partition.ambiguousTags.insert(tag);
						qualify(m_result, Outcome::Malformed, QStringLiteral("Primer repeats local tag %1; all entries are retained.").arg(tag, 4, 16, QLatin1Char('0')));
					}
					else
						m_partition.mappings.insert(tag, ul);
				}
				property.decoded = entries;
			}

			void localSet(const Packet &packet)
			{
				auto &object = m_result.objects.emplaceBack();
				object.handle = quint64(m_result.objects.size());
				auto context = QSharedPointer<MxfSetContext>::create();
				context->key = packet.key;
				context->partitionOffset = m_partition.offset;
				context->framing = {packet.offset, packet.valueOffset - packet.offset};
				context->value = {packet.valueOffset, packet.length};
				object.mxf = context;
				// libMXF's Avid root writer uses 2-byte local tags and lengths for
				// this exact private UUID key. Other UUID keys remain opaque.
				const quint8 coding = packet.key == avidRootKey ? 0x53 : quint8(packet.key[5]);
				if (coding != 0x53 && coding != 0x13)
				{
					auto &property = object.properties.emplaceBack();
					property.locator.name = QStringLiteral("MXF.UnsupportedLocalSet");
					property.locator.key = packet.key;
					property.locator.objectNumber = object.handle;
					property.locator.ranges = {{packet.valueOffset, std::min(packet.length, m_input.size() - packet.valueOffset)}};
					property.state = packet.fits ? PropertyReadState::Present : PropertyReadState::Unreadable;
					property.bytesRetained = false;
					property.interpretation = QStringLiteral("Original value range retained; this local-tag/length coding is not interpreted.");
					qualify(m_result, Outcome::Unsupported, property.interpretation);
					return;
				}
				qint64 position = packet.valueOffset;
				const qint64 end = packet.valueOffset + packet.length;
				while (position < end && position < m_input.size())
				{
					check(m_cancellation);
					auto &property = object.properties.emplaceBack();
					property.locator.objectNumber = object.handle;
					auto native = QSharedPointer<MxfPropertyContext>::create();
					native->primerOffset = m_partition.primerOffset;
					property.mxf = native;
					RawProperty framing;
					framing.locator.name = QStringLiteral("MXF.LocalTagAndLength");
					try
					{
						if (end - position < 2)
							throw Failure{Outcome::Malformed, QStringLiteral("MXF set ends inside a local tag.")};
						m_input.read(framing, position, 2);
						position += 2;
						native->localTag = u16(framing.encoding, 0);
						quint64 length = 0;
						if (coding == 0x13)
							length = m_input.ber(framing, position, end);
						else
						{
							if (end - position < 2)
								throw Failure{Outcome::Malformed, QStringLiteral("MXF set ends inside a two-byte local length.")};
							m_input.read(framing, position, 2);
							position += 2;
							length = u16(framing.encoding, 2);
						}
						native->framingBytes = framing.encoding;
						native->framingRanges = framing.locator.ranges;
						if (!m_partition.ambiguousTags.contains(quint16(native->localTag)))
							native->mappedAuid = m_partition.mappings.value(quint16(native->localTag));
						property.locator.key = native->mappedAuid.isEmpty() ? framing.encoding.first(2) : native->mappedAuid;
						property.locator.name = QStringLiteral("MXF.LocalTag.%1").arg(native->localTag, 4, 16, QLatin1Char('0'));
						if (native->mappedAuid.isEmpty())
						{
							property.interpretation = QStringLiteral("No unambiguous Primer mapping; the local tag does not establish a property type or meaning.");
							qualify(m_result, Outcome::Incomplete, QStringLiteral("Local tag %1 in partition at byte %2 has no unambiguous Primer mapping.").arg(native->localTag, 4, 16, QLatin1Char('0')).arg(m_partition.offset));
						}
						if (length > quint64(end - position))
							throw Failure{Outcome::Malformed, QStringLiteral("MXF property length crosses its set boundary.")};
						m_input.read(property, position, qint64(length));
						position += qint64(length);
					}
					catch (const Failure &)
					{
						native->framingBytes = framing.encoding;
						native->framingRanges = framing.locator.ranges;
						property.state = PropertyReadState::Unreadable;
						throw;
					}
				}
			}

			void randomIndex(const Packet &packet)
			{
				auto &property = value(packet, QStringLiteral("MXF.RandomIndexPack"));
				const auto &bytes = property.encoding;
				if (bytes.size() < 4 || (bytes.size() - 4) % 12 != 0)
				{
					property.state = PropertyReadState::Unreadable;
					qualify(m_result, Outcome::Malformed, QStringLiteral("Random Index Pack has an invalid entry extent."));
					return;
				}
				QVariantList entries;
				for (qsizetype offset = 0; offset < bytes.size() - 4; offset += 12)
				{
					check(m_cancellation);
					entries.append(QVariantMap{{QStringLiteral("bodySID"), u32(bytes, offset)},
											   {QStringLiteral("byteOffset"), QVariant::fromValue(u64(bytes, offset + 4))}});
				}
				const quint32 total = u32(bytes, bytes.size() - 4);
				property.decoded = QVariantMap{{QStringLiteral("entries"), entries}, {QStringLiteral("totalLength"), total}};
				if (total != quint64(packet.valueOffset - packet.offset + packet.length))
					qualify(m_result, Outcome::Malformed, QStringLiteral("Random Index Pack total length disagrees with its KLV extent."));
				property.interpretation = QStringLiteral("Recorded index retained; partition discovery used physical KLV framing rather than trusting these offsets.");
			}

			void finishPartition(qint64 boundary)
			{
				if (m_partition.offset < 0 || !m_partition.headerBytes)
					return;
				if (m_partition.headerStart < 0 || m_partition.headerEnd > boundary)
					qualify(m_result, boundary >= m_input.size() ? Outcome::Incomplete : Outcome::Malformed,
							QStringLiteral("Partition HeaderByteCount does not fit the encountered metadata section."));
			}

			void checkPointers()
			{
				for (const auto &record : m_pointerRecords)
				{
					check(m_cancellation);
					for (const auto pointer : {record.previous, record.footer})
					{
						if (!pointer)
							continue;
						if (pointer >= quint64(m_input.size() - m_runIn))
							qualify(m_result, Outcome::Incomplete, QStringLiteral("Partition pointer at byte %1 refers beyond the available file.").arg(record.offset));
						else if (!m_partitions.contains(m_runIn + qint64(pointer)))
							qualify(m_result, Outcome::Malformed, QStringLiteral("Partition pointer at byte %1 does not identify an encountered Partition Pack.").arg(record.offset));
					}
				}
			}

			struct Pointers
			{
				qint64 offset;
				quint64 previous;
				quint64 footer;
			};
			Input &m_input;
			const Cancellation &m_cancellation;
			ParsedSource &m_result;
			qint64 m_runIn = 0;
			Partition m_partition;
			QHash<qint64, QByteArray> m_partitions;
			QVector<Pointers> m_pointerRecords;
		};
	}

	ParsedSource MxfReader::read(QIODevice &source, const ReaderContext &context) const
	{
		ParsedSource result;
		try
		{
			check(context.cancellation);
			if (!source.isOpen() || !source.isReadable() || source.isSequential() || source.isTextModeEnabled() || source.size() < 0)
				throw Failure{Outcome::IoError, QStringLiteral("MXF reader requires an already-open readable seekable binary device with a known length.")};
			Input input(source, context.cancellation);
			result.outcome = Outcome::Complete;
			Reader(input, context.cancellation, result).read();
			input.finish();
		}
		catch (const Failure &failure)
		{
			qualify(result, failure.outcome, failure.reason);
			if (failure.outcome == Outcome::Cancelled || failure.outcome == Outcome::IoError)
				result.outcome = failure.outcome;
		}
		Detail::interpretMxfObjects(result, context.cancellation);
		auto snapshot = QSharedPointer<SourceSnapshot>::create(context.snapshot ? *context.snapshot : SourceSnapshot{});
		snapshot->source = MetadataSource::Mxf;
		snapshot->readState = result.outcome == Outcome::Complete  ? SourceReadState::Complete
							  : result.outcome == Outcome::IoError ? SourceReadState::Unreadable
																   : SourceReadState::Incomplete;
		result.snapshot = snapshot;
		for (auto &object : result.objects)
		{
			object.snapshot = snapshot;
			object.properties.squeeze();
		}
		// Parsing is finished, so unused growth capacity need not stay in RAM
		// for every media file. All properties, references and locators remain.
		result.objects.squeeze();
		result.unownedProperties.squeeze();
		result.relationships.squeeze();
		return result;
	}
}
