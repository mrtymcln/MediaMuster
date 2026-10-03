// Reads Bento's container structure independently of the old application parser.
// The table of contents describes where values live; only declared continuations
// are joined. Unknown properties keep their bytes for the MDB reader to interpret.

#include "bentoreader_p.h"

#include <QByteArrayView>
#include <QHash>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <array>

namespace Canon::Detail
{
	namespace
	{
		using Outcome = ParsedSource::Outcome;
		constexpr qint64 labelSize = 24;
		const QByteArray magic = QByteArray::fromHex("a4434da5486472d7");

		struct Failure
		{
			Outcome outcome;
			QString message;
		};

		void checkCancellation(const Cancellation &cancellation)
		{
			if (cancellation.cancelled())
				throw Failure{Outcome::Cancelled, QStringLiteral("Bento read cancelled.")};
		}

		quint32 word(const char *bytes, bool big)
		{
			return big ? qFromBigEndian<quint32>(bytes) : qFromLittleEndian<quint32>(bytes);
		}

		quint16 half(const char *bytes, bool big)
		{
			return big ? qFromBigEndian<quint16>(bytes) : qFromLittleEndian<quint16>(bytes);
		}

		class Input
		{
		public:
			Input(QIODevice &device, const Cancellation &cancellation)
				: m_device(device), m_cancellation(cancellation), m_extent(device.size())
			{
				if (m_extent < 0)
					throw Failure{Outcome::IoError, QStringLiteral("Cannot determine Bento source length.")};
			}

			qint64 extent() const { return m_extent; }

			void read(qint64 offset, qint64 length, QByteArray &bytes, QVector<ByteRange> &ranges)
			{
				checkCancellation(m_cancellation);
				if (offset < 0 || offset > m_extent || length < 0 || length > m_extent - offset)
					throw Failure{Outcome::Malformed, QStringLiteral("Bento range lies outside the captured source length.")};
				if (!m_device.seek(offset))
					throw Failure{Outcome::IoError, QStringLiteral("Cannot seek to Bento byte %1: %2").arg(offset).arg(m_device.errorString())};
				auto &range = ranges.emplaceBack(ByteRange{offset, 0});
				std::array<char, 65536> buffer; // Only the bytes filled by read() are appended.
				while (range.length < length)
				{
					checkCancellation(m_cancellation);
					const qint64 count = m_device.read(buffer.data(), std::min(length - range.length, qint64(buffer.size())));
					if (count <= 0)
						throw Failure{count == 0 && m_device.atEnd() ? Outcome::Incomplete : Outcome::IoError,
							QStringLiteral("Cannot read Bento byte %1: %2").arg(offset + range.length).arg(m_device.errorString())};
					bytes.append(buffer.data(), count);
					range.length += count;
				}
			}

			void verify() const
			{
				checkCancellation(m_cancellation);
				if (m_device.size() != m_extent)
					throw Failure{Outcome::Incomplete, QStringLiteral("Bento source length changed during reading; check this source again.")};
			}

		private:
			QIODevice &m_device;
			const Cancellation &m_cancellation;
			qint64 m_extent;
		};

		struct Segment
		{
			quint32 object = 0, property = 0, type = 0, generation = 0, references = 0;
			quint64 offset = 0, length = 0;
			bool immediate = false, continued = false;
			ByteRange framing;
		};

		class Reader
		{
		public:
			Reader(Input &input, const Cancellation &cancellation, BentoReadResult &result,
				const BentoReadOptions &options)
				: m_input(input), m_cancellation(cancellation), m_result(result), m_options(options) {}

			void read()
			{
				if (m_input.extent() < labelSize)
					throw Failure{Outcome::Incomplete, QStringLiteral("Source is too short to contain a Bento label.")};
				const qint64 labelOffset = m_options.labelOffset == -1 ? m_input.extent() - labelSize : m_options.labelOffset;
				const auto &label = structure(QStringLiteral("Bento.Label"), labelOffset, labelSize);
				if (!label.encoding.startsWith(magic))
					throw Failure{Outcome::Malformed, QStringLiteral("No Bento label at the specified source offset.")};
				const char *p = label.encoding.constData();
				// OMF toolkit omfansic.c: Bento 1 uses little-endian container words;
				// Bento 2 uses the symmetric 0x0101 little-endian label flag.
				const quint16 flags = half(p + 8, false);
				m_result.containerBigEndian = half(p + 12, false) != 1 && (flags & 0x0101) == 0;
				m_result.major = half(p + 12, m_result.containerBigEndian);
				const quint16 minor = half(p + 14, m_result.containerBigEndian);
				if ((m_result.major != 1 && m_result.major != 2) || minor != 0)
				{
					auto &uninterpreted = m_result.structure.emplaceBack();
					uninterpreted.locator.name = QStringLiteral("Bento.UninterpretedBody");
					uninterpreted.locator.ranges.append({0, labelOffset});
					uninterpreted.state = PropertyReadState::NotRead;
					uninterpreted.bytesRetained = false;
					uninterpreted.interpretation = QStringLiteral("Body retained by source range because this label version is unsupported.");
					throw Failure{Outcome::Unsupported, QStringLiteral("Bento label version %1.%2 is not supported; extended labels are not guessed.").arg(m_result.major).arg(minor)};
				}
				if (flags & ~quint16(0x0101))
					throw Failure{Outcome::Unsupported, QStringLiteral("Unrecognised Bento label flags 0x%1.").arg(flags, 4, 16, QLatin1Char('0'))};
				m_tocOffset = word(p + 16, m_result.containerBigEndian);
				const qint64 tocLength = word(p + 20, m_result.containerBigEndian);
				m_blockSize = qint64(half(p + 10, m_result.containerBigEndian)) * 1024;
				if (!m_blockSize)
					m_blockSize = (tocLength / 1024 + 1) * 1024;
				if (m_tocOffset > labelOffset || tocLength > labelOffset - m_tocOffset)
					throw Failure{Outcome::Malformed, QStringLiteral("Bento table of contents overlaps or lies beyond its label.")};
				const auto &toc = structure(QStringLiteral("Bento.TableOfContents"), m_tocOffset, tocLength);
				m_toc = QByteArrayView(toc.encoding);
				if (m_result.major == 1)
					fixedToc();
				else
					compactToc();
				if (m_continuing)
					badValue(m_result.values.back(), QStringLiteral("Continued Bento value has no final segment."));
				const qint64 gap = labelOffset - m_tocOffset - tocLength;
				if (gap)
				{
					auto &trailing = m_result.structure.emplaceBack();
					trailing.locator.name = QStringLiteral("Bento.BytesAfterTableOfContents");
					trailing.locator.ranges.append({m_tocOffset + tocLength, gap});
					trailing.state = PropertyReadState::Present;
					trailing.bytesRetained = false;
					trailing.interpretation = QStringLiteral("Unreferenced space before the label; retained as a source range.");
				}
				if (m_options.metadataOnly)
					readMetadata();
			}

		private:
			RawProperty &structure(const QString &name, qint64 offset, qint64 length)
			{
				auto &property = m_result.structure.emplaceBack();
				property.locator.name = name;
				property.state = PropertyReadState::Unreadable;
				m_input.read(offset, length, property.encoding, property.locator.ranges);
				property.state = PropertyReadState::Present;
				return property;
			}

			void badValue(BentoValue &value, const QString &problem)
			{
				value.state = PropertyReadState::Unreadable;
				value.problem = problem;
				m_result.diagnostics.append(QStringLiteral("Bento object %1 property %2: %3").arg(value.object).arg(value.property).arg(problem));
				m_result.outcome = Outcome::Malformed;
			}

			void append(const Segment &segment)
			{
				checkCancellation(m_cancellation);
				if (m_continuing)
				{
					const auto &previous = m_result.values.back();
					if (previous.object != segment.object || previous.property != segment.property || previous.type != segment.type ||
						previous.generation != segment.generation || (segment.references && previous.referenceListObject != segment.references))
					{
						badValue(m_result.values.back(), QStringLiteral("Continued Bento value changes its object, property, type, generation or reference list."));
						m_continuing = false;
					}
				}
				if (!m_continuing)
				{
					auto &value = m_result.values.emplaceBack();
					value.object = segment.object;
					value.property = segment.property;
					value.type = segment.type;
					value.generation = segment.generation;
					value.referenceListObject = segment.references;
					value.state = PropertyReadState::Present;
				}
				auto &value = m_result.values.back();
				value.tocRanges.append(segment.framing);
				m_continuing = segment.continued;
				const bool structuralExtent = segment.object == 1 && segment.type == 19 &&
					(segment.property == 4 || segment.property == 5 || segment.property == 7);
				const quint64 limit = segment.immediate || structuralExtent ? quint64(m_input.extent()) : quint64(m_tocOffset);
				if (segment.offset > limit || segment.length > limit - segment.offset)
				{
					badValue(value, QStringLiteral("Value range lies outside its container area; original offset and length remain in the TOC."));
					return;
				}
				if (structuralExtent && !segment.immediate)
				{
					value.bytesRetained = false;
					value.ranges.append({qint64(segment.offset), qint64(segment.length)});
					value.problem = QStringLiteral("Bento's internal TOC, container or free-space extent; retained as a range without copying those bytes.");
				}
				else if (segment.immediate)
				{
					value.bytes.append(m_toc.data() + qint64(segment.offset) - m_tocOffset, qsizetype(segment.length));
					value.ranges.append({qint64(segment.offset), qint64(segment.length)});
				}
				else if (m_options.metadataOnly)
				{
					value.bytesRetained = false;
					value.ranges.append({qint64(segment.offset), qint64(segment.length)});
					if (value.state == PropertyReadState::Present)
					{
						value.state = PropertyReadState::NotRead;
						value.problem = QStringLiteral("Value ranges indexed; metadata reading has not reached this value.");
					}
				}
				else
				{
					// Keep a failed/partial read attached to its value before propagating.
					const auto priorState = value.state;
					value.state = PropertyReadState::Unreadable;
					m_input.read(qint64(segment.offset), qint64(segment.length), value.bytes, value.ranges);
					value.state = priorState;
				}
				if ((segment.object == 1 && segment.property >= 8 && segment.property <= 10) || segment.property == 29 || segment.type == 30)
					throw Failure{Outcome::Unsupported, QStringLiteral("Bento update instructions require a target/update resolver; original value and TOC ranges are retained.")};
			}

			void readDeferred(BentoValue &value)
			{
				if (value.state != PropertyReadState::NotRead)
					return;
				checkCancellation(m_cancellation);
				const auto declaredRanges = std::move(value.ranges);
				value.ranges.clear();
				value.bytes.clear();
				value.bytesRetained = true;
				value.state = PropertyReadState::Unreadable;
				value.problem = QStringLiteral("Metadata read interrupted; obtained bytes and their ranges are retained. The TOC preserves the complete declared extents.");
				for (const auto &range : declaredRanges)
					m_input.read(range.offset, range.length, value.bytes, value.ranges);
				value.state = PropertyReadState::Present;
				value.problem.clear();
			}

			void readMetadata()
			{
				// Property IDs are file-local. Read their definitions before deciding
				// whether a value is media essence; a VarLenBytes/DataValue type alone
				// cannot distinguish an entire recording from a descriptor summary.
				for (auto &value : m_result.values)
				{
					checkCancellation(m_cancellation);
					if (value.property == 23 || value.property == 24)
						readDeferred(value);
				}
				// OMF toolkit omFile.c registers these seven essence property names.
				const QSet<QByteArray> essenceNames{
					"OMFI:IDAT:ImageData", "OMFI:TIFF:Data", "OMFI:TIFF:ImageData",
					"OMFI:AIFC:Data", "OMFI:AIFC:AudioData", "OMFI:WAVE:Data", "OMFI:WAVE:AudioData"};
				QHash<quint32, QSet<QByteArray>> propertyNames;
				QSet<quint32> uncertainDefinitions;
				for (const auto &value : m_result.values)
				{
					checkCancellation(m_cancellation);
					if (value.property != 24)
						continue;
					if (value.state != PropertyReadState::Present || !value.bytesRetained)
					{
						uncertainDefinitions.insert(value.object);
						continue;
					}
					const auto terminator = value.bytes.indexOf('\0');
					propertyNames[value.object].insert(terminator < 0 ? value.bytes : value.bytes.first(terminator));
				}
				for (auto &value : m_result.values)
				{
					checkCancellation(m_cancellation);
					if (value.state == PropertyReadState::Unreadable)
						continue;
					const auto names = propertyNames.constFind(value.property);
					const bool essence = names != propertyNames.cend() &&
						std::any_of(names->cbegin(), names->cend(), [&](const QByteArray &name) { return essenceNames.contains(name); });
					if (!essence)
					{
						readDeferred(value);
						continue;
					}
					value.bytes.clear();
					value.bytesRetained = false;
					value.state = PropertyReadState::Present;
					value.problem = names->size() != 1 || uncertainDefinitions.contains(value.property)
						? QStringLiteral("Conflicting property definitions include an essence property; retained by range without guessing which definition applies.")
						: QStringLiteral("Recorded media essence retained by source range; media payload bytes are not copied during metadata reading.");
				}
			}

			void fixedToc()
			{
				for (qsizetype pos = 0; pos < m_toc.size(); pos += 24)
				{
					checkCancellation(m_cancellation);
					if (m_toc.size() - pos < 24)
						throw Failure{Outcome::Malformed, QStringLiteral("Incomplete 24-byte Bento 1 TOC entry.")};
					const char *p = m_toc.data() + pos;
					const quint16 flags = half(p + 22, false);
					if (flags & ~quint16(3))
						throw Failure{Outcome::Unsupported, QStringLiteral("Unrecognised Bento 1 value flags at byte %1.").arg(m_tocOffset + pos)};
					Segment segment;
					segment.object = word(p, false);
					segment.property = word(p + 4, false);
					segment.type = word(p + 8, false);
					if ((segment.property | segment.type) & 0xff000000u)
						throw Failure{Outcome::Unsupported, QStringLiteral("Bento 1 uses high property/type bytes associated with the legacy 40-bit offset extension; original TOC retained.")};
					segment.generation = half(p + 20, false);
					segment.immediate = flags & 1;
					segment.continued = flags & 2;
					segment.length = word(p + 16, false);
					segment.offset = segment.immediate ? quint64(m_tocOffset + pos + 12) : word(p + 12, false);
					segment.framing = {m_tocOffset + pos, 24};
					if (segment.immediate && segment.length > 4)
						throw Failure{Outcome::Malformed, QStringLiteral("Bento immediate value exceeds its four-byte field.")};
					append(segment);
				}
			}

			void compactToc()
			{
				Segment context;
				bool haveObject = false, needValue = false, allowGeneration = false, allowReference = false;
				qsizetype framingStart = 0;
				for (qsizetype pos = 0; pos < m_toc.size();)
				{
					checkCancellation(m_cancellation);
					const qsizetype start = pos;
					const quint8 code = quint8(m_toc[pos++]);
					const qint64 blockEnd = std::min(qint64(m_toc.size()), (start / m_blockSize + 1) * m_blockSize);
					if (code == 255)
						continue;
					if (code == 24)
					{
						pos = blockEnd;
						continue;
					}
					qsizetype width = 0;
					switch (code)
					{
					case 1:
					case 7:
					case 8:
						width = 12;
						break;
					case 2:
					case 5:
					case 6:
						width = 8;
						break;
					case 3:
					case 4:
					case 10:
					case 11:
					case 12:
					case 13:
					case 14:
					case 15:
						width = 4;
						break;
					case 9:
						break;
					case 25:
					case 26:
						width = 16;
						break;
					default:
						throw Failure{code == 0 ? Outcome::Malformed : Outcome::Unsupported,
							QStringLiteral("Unrecognised Bento 2 opcode %1 at byte %2; original TOC retained.").arg(code).arg(m_tocOffset + start)};
					}
					if (width > blockEnd - pos)
						throw Failure{Outcome::Malformed, QStringLiteral("Bento 2 opcode crosses the TOC or buffer boundary at byte %1.").arg(m_tocOffset + start)};
					const char *p = m_toc.data() + pos;
					auto w = [&](qsizetype index) { return word(p + index * 4, m_result.containerBigEndian); };
					pos += width;
					if (code >= 1 && code <= 3)
					{
						if (needValue || (code != 1 && !haveObject))
							throw Failure{Outcome::Malformed, QStringLiteral("Bento 2 object/property/type appears without the required preceding value or object.")};
						if (code == 1)
						{
							context.object = w(0);
							context.property = w(1);
							context.type = w(2);
							haveObject = true;
						}
						else if (code == 2)
						{
							context.property = w(0);
							context.type = w(1);
						}
						else
							context.type = w(0);
						needValue = allowGeneration = allowReference = true;
						continue;
					}
					if (!haveObject)
						throw Failure{Outcome::Malformed, QStringLiteral("Bento 2 TOC must establish an object before its values.")};
					if (code == 4)
					{
						if (!allowGeneration)
							throw Failure{Outcome::Malformed, QStringLiteral("Misplaced Bento generation opcode.")};
						context.generation = w(0);
						allowGeneration = false;
						continue;
					}
					if (code == 15)
					{
						if (!allowReference)
							throw Failure{Outcome::Malformed, QStringLiteral("Misplaced Bento reference-list opcode.")};
						context.references = w(0);
						allowReference = allowGeneration = false;
						continue;
					}
					context.immediate = code >= 9 && code <= 14;
					context.continued = code == 6 || code == 8 || code == 14 || code == 26;
					context.framing = {m_tocOffset + framingStart, pos - framingStart};
					if (context.immediate)
					{
						context.offset = quint64(m_tocOffset + start + 1);
						context.length = code == 14 ? 4 : code - 9;
					}
					else if (code == 5 || code == 6)
					{
						context.offset = w(0);
						context.length = w(1);
					}
					else
					{
						context.offset = (quint64(w(0)) << 32) | w(1);
						context.length = code >= 25 ? (quint64(w(2)) << 32) | w(3) : w(2);
					}
					append(context);
					context.references = 0;
					needValue = allowGeneration = allowReference = false;
					framingStart = pos;
				}
				if (needValue)
					throw Failure{Outcome::Malformed, QStringLiteral("Bento 2 TOC ends before its declared value.")};
			}

			Input &m_input;
			const Cancellation &m_cancellation;
			BentoReadResult &m_result;
			const BentoReadOptions &m_options;
			QByteArrayView m_toc;
			qint64 m_tocOffset = 0, m_blockSize = 0;
			bool m_continuing = false;
		};
	}

	BentoReadResult readBento(QIODevice &source, const Cancellation &cancellation, const BentoReadOptions &options)
	{
		BentoReadResult result;
		try
		{
			checkCancellation(cancellation);
			if (!source.isOpen() || !source.isReadable() || source.isSequential() || source.isTextModeEnabled())
				throw Failure{Outcome::IoError, QStringLiteral("Bento reader requires an already-open, readable, seekable binary device.")};
			Input input(source, cancellation);
			result.outcome = Outcome::Complete;
			Reader(input, cancellation, result, options).read();
			input.verify();
		}
		catch (const Failure &failure)
		{
			result.outcome = failure.outcome;
			result.diagnostics.append(failure.message);
		}
		return result;
	}
}
