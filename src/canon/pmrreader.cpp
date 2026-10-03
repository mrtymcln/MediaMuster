#include "pmrreader.h"
#include <QStringConverter>
#include <QtEndian>
#include <algorithm>
#include <cstring>

namespace Canon
{
	namespace
	{
		constexpr quint32 pmrMagic = 0x000007a9;
		constexpr qint32 unicodeVersion = 16;
		constexpr quint16 nullString = 0xffff;
		constexpr qsizetype mbcsFilenameCapacity = 2048;
		constexpr qsizetype projectCapacity = 64;
		constexpr qsizetype utf8FilenameCapacity = 1024;
		using Outcome = ParsedSource::Outcome;

		class PmrReadSession
		{
		public:
			PmrReadSession(QIODevice &device, const ReaderContext &context)
				: m_device(device), m_context(context) {}

			ParsedSource run()
			{
				m_result.outcome = Outcome::Complete;
				if (m_context.cancellation.cancelled())
					fail(Outcome::Cancelled, QStringLiteral("Cancelled before reading."));
				else if (!m_device.isOpen() || !m_device.isReadable() || m_device.isTextModeEnabled() || m_device.isSequential() ||
						 !m_device.seek(0))
					fail(Outcome::IoError, QStringLiteral("PMR reader requires an open, readable, binary, seekable source."));
				else
					parse();
				retainUnreadTail();
				// The supplied receipt remains immutable. Completion is this reader's receipt,
				// not proof that the filesystem source stayed unchanged during the read.
				SourceSnapshot receipt = m_context.snapshot ? *m_context.snapshot : SourceSnapshot{};
				receipt.source = MetadataSource::Pmr;
				receipt.readState = m_result.outcome == Outcome::Complete ? SourceReadState::Complete : (m_result.outcome == Outcome::IoError ? SourceReadState::Unreadable : SourceReadState::Incomplete);
				m_result.snapshot = SourceSnapshotRef::create(receipt);
				for (auto &object : m_result.objects)
					object.snapshot = m_result.snapshot;
				return std::move(m_result);
			}

		private:
			void fail(Outcome outcome, const QString &message)
			{
				m_result.outcome = outcome;
				m_result.diagnostics.append(message);
				m_stopped = true;
			}

			void invalidText(RawProperty &property, const QString &reason)
			{
				property.state = PropertyReadState::Unreadable;
				property.interpretation = reason;
				property.decoded.clear();
				m_result.diagnostics.append(property.locator.name + QStringLiteral(": ") + reason);
				m_result.outcome = Outcome::Malformed;
			}

			// Reads are bounded by a fixed field width or a uint16 string length, never
			// by the declared record count. Short successful reads are accumulated.
			bool appendBytes(RawProperty &property, qint64 count)
			{
				while (count > 0)
				{
					if (m_context.cancellation.cancelled())
					{
						fail(Outcome::Cancelled, QStringLiteral("Cancelled during a field read."));
						return false;
					}
					char buffer[4096];
					const qint64 read = m_device.read(buffer, std::min(count, qint64(sizeof(buffer))));
					if (read <= 0)
					{
						fail(read < 0 ? Outcome::IoError : Outcome::Incomplete,
							 QStringLiteral("Cannot complete %1 at byte %2: %3")
								 .arg(property.locator.name)
								 .arg(m_device.pos())
								 .arg(m_device.errorString()));
						return false;
					}
					property.encoding.append(buffer, read);
					count -= read;
				}
				return true;
			}

			RawProperty bytes(const QString &name, qint64 count)
			{
				RawProperty property;
				property.locator.name = name;
				const qint64 start = m_device.pos();
				property.state = appendBytes(property, count) ? PropertyReadState::Present : PropertyReadState::Unreadable;
				property.locator.ranges.append({start, property.encoding.size()});
				return property;
			}

			quint32 unsignedWord(const QByteArray &data) const
			{
				return m_bigEndian ? qFromBigEndian<quint32>(data.constData()) : qFromLittleEndian<quint32>(data.constData());
			}

			quint32 word(const QString &name)
			{
				auto property = bytes(name, 4);
				quint32 value = 0;
				if (!m_stopped)
				{
					value = unsignedWord(property.encoding);
					property.decoded = value;
				}
				m_result.unownedProperties.append(std::move(property));
				return value;
			}

			qint32 signedWord(const QString &name)
			{
				const quint32 bits = word(name);
				qint32 value = 0;
				static_assert(sizeof(value) == sizeof(bits));
				std::memcpy(&value, &bits, sizeof(value));
				if (!m_stopped)
					m_result.unownedProperties.last().decoded = value;
				return value;
			}

			RawProperty text(const QString &name, bool unicode, qsizetype capacity)
			{
				auto property = bytes(name, 2);
				property.textEncoding = unicode ? TextEncoding::Utf8 : TextEncoding::Unknown;
				if (unicode)
					property.textEncodingBasis = EvidenceBasis::Recorded;
				if (m_stopped)
					return property;
				const quint16 length = m_bigEndian ? qFromBigEndian<quint16>(property.encoding.constData()) : qFromLittleEndian<quint16>(property.encoding.constData());
				if (length == nullString)
				{
					property.decoded = QString{};
					property.interpretation = QStringLiteral("Recorded null-string marker; not an absent property.");
					return property;
				}
				const bool complete = appendBytes(property, length);
				property.locator.ranges[0].length = property.encoding.size();
				if (!complete)
				{
					property.state = PropertyReadState::Unreadable;
					return property;
				}
				QByteArray payload = property.encoding.mid(2);
				if (unicode)
				{
					if (payload.size() < 2 || payload.at(0) != '\0')
					{
						invalidText(property, QStringLiteral("Invalid Unicode length/first reserved byte; complete bytes retained."));
						return property;
					}
					payload.remove(0, 2); // Second reserved byte is retained, not required to be zero.
					if (payload.size() > 32767)
					{
						invalidText(property, QStringLiteral("Unicode payload exceeds Avid's signed 16-bit length."));
						return property;
					}
				}
				if (payload.size() >= capacity)
				{
					invalidText(property, QStringLiteral("Counted text exceeds the verified Avid input capacity; bytes retained."));
					return property;
				}
				const qsizetype terminator = payload.indexOf('\0');
				if (terminator >= 0)
					payload.truncate(terminator); // Keep the complete counted encoding above.
				if (unicode)
				{
					QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
					const QString decoded = decoder.decode(payload);
					if (decoder.hasError())
						invalidText(property, QStringLiteral("Invalid explicitly tagged UTF-8; no replacement-text selection."));
					else
					{
						property.decoded = decoded;
						property.interpretation = QStringLiteral("Explicit Unicode set: UTF-8 after two reserved bytes.");
					}
				}
				else if (std::any_of(payload.cbegin(), payload.cend(), [](char byte)
									 { return static_cast<unsigned char>(byte) >= 128; }))
					property.interpretation = QStringLiteral("Recorded MBCS bytes; codepage is not tagged. Semantic text remains undecoded.");
				else
				{
					property.textEncoding = TextEncoding::Ascii;
					property.textEncodingBasis = EvidenceBasis::Derived;
					property.decoded = QString::fromLatin1(payload);
					property.interpretation = QStringLiteral("ASCII subset of untagged MBCS; full counted bytes retained.");
				}
				return property;
			}

			QString identityEncoding(qint32 version) const
			{
				return QStringLiteral("PMR/%1/%2").arg(version <= 7 ? QStringLiteral("OMF-8") : QStringLiteral("AAF-32"), m_bigEndian ? QStringLiteral("big-endian") : QStringLiteral("little-endian"));
			}

			void record(RecordSet &set)
			{
				AvidObject object;
				object.handle = static_cast<ObjectHandle>(m_result.objects.size()) + 1;
				object.role = AvidObject::Role::FileSource;
				object.identityEncoding = identityEncoding(set.version);
				set.objects.append(object.handle);
				const qint64 width = set.version <= 7 ? 8 : 32;
				auto id = bytes(QStringLiteral("FileMobId"), width);
				if (!m_stopped)
				{
					object.recordedIdentity = id.encoding;
					id.decoded = id.encoding;
					id.interpretation = object.identityEncoding;
				}
				object.properties.append(std::move(id));
				if (!m_stopped)
					object.properties.append(text(QStringLiteral("Filename"), set.pmrFileSet == PmrFileSet::Unicode,
												  set.pmrFileSet == PmrFileSet::Unicode ? utf8FilenameCapacity : mbcsFilenameCapacity));
				if (!m_stopped && set.version != 1)
				{
					object.properties.append(text(QStringLiteral("Project"), false, projectCapacity));
					if (!m_stopped)
					{
						auto master = bytes(QStringLiteral("MasterMobId"), width);
						if (!m_stopped)
						{
							master.decoded = master.encoding;
							master.interpretation = object.identityEncoding;
							Relationship relationship;
							relationship.origin = object.handle;
							relationship.locator = master.locator;
							relationship.recordedReference = master.encoding;
							relationship.referenceEncoding = object.identityEncoding;
							relationship.explanation = QStringLiteral("Recorded master reference; target is not defined by this record.");
							m_result.relationships.append(std::move(relationship));
						}
						object.properties.append(std::move(master));
					}
				}
				else if (!m_stopped)
				{
					for (const auto &name : {QStringLiteral("Project"), QStringLiteral("MasterMobId")})
					{
						RawProperty absent;
						absent.locator.name = name;
						absent.state = PropertyReadState::Absent;
						absent.interpretation = QStringLiteral("Not stored in a version-1 PMR record layout.");
						object.properties.append(std::move(absent));
					}
				}
				if (!m_stopped)
				{
					auto timestamp = bytes(QStringLiteral("ModificationWord"), 4);
					if (!m_stopped)
					{
						timestamp.decoded = unsignedWord(timestamp.encoding);
						timestamp.interpretation = QStringLiteral("Recorded uint32; epoch/timezone selection belongs to reconciliation.");
					}
					object.properties.append(std::move(timestamp));
				}
				m_result.objects.append(std::move(object));
			}

			void recordSet(PmrFileSet fileSet, qint32 version)
			{
				RecordSet set;
				set.pmrFileSet = fileSet;
				set.name = fileSet == PmrFileSet::Legacy ? QStringLiteral("Legacy") : QStringLiteral("Unicode");
				set.version = version;
				set.declaredCount = word(set.name + QStringLiteral(".Count"));
				for (quint32 index = 0; !m_stopped && index < set.declaredCount; ++index)
					record(set);
				set.framingComplete = !m_stopped;
				m_result.recordSets.append(std::move(set));
			}

			void parse()
			{
				auto magic = bytes(QStringLiteral("Magic"), 4);
				if (m_stopped)
				{
					m_result.unownedProperties.append(std::move(magic));
					return;
				}
				m_bigEndian = qFromBigEndian<quint32>(magic.encoding.constData()) == pmrMagic;
				if (unsignedWord(magic.encoding) != pmrMagic)
				{
					m_result.unownedProperties.append(std::move(magic));
					fail(Outcome::Malformed, QStringLiteral("PMR signature does not match either supported byte order."));
					return;
				}
				magic.decoded = pmrMagic;
				m_result.unownedProperties.append(std::move(magic));
				m_result.container = ParsedSource::Container::Pmr;
				const qint32 version = signedWord(QStringLiteral("Legacy.Version"));
				if (m_stopped)
					return;
				if (version >= 9)
				{
					fail(Outcome::Unsupported, QStringLiteral("Unverified PMR base version %1; remaining bytes referenced without decoding.").arg(version));
					return;
				}
				if (version <= 0)
					m_result.diagnostics.append(QStringLiteral("Version %1 follows the observed signed branch; historical writer support is unproven.").arg(version));
				recordSet(PmrFileSet::Legacy, version);
				if (m_stopped || m_device.pos() == m_device.size())
					return;
				const qint32 extension = signedWord(QStringLiteral("Extension.Version"));
				if (m_stopped)
					return;
				if (extension != unicodeVersion)
				{
					fail(Outcome::Unsupported, QStringLiteral("Unverified PMR extension %1; base records retained.").arg(extension));
					return;
				}
				recordSet(PmrFileSet::Unicode, extension);
				if (!m_stopped && m_device.pos() != m_device.size())
					fail(Outcome::Unsupported, QStringLiteral("Bytes follow the verified Unicode set; retained as an opaque range."));
			}

			void retainUnreadTail()
			{
				if (!m_device.isOpen() || m_device.isSequential() || m_device.pos() < 0)
					return;
				const qint64 remaining = m_device.size() - m_device.pos();
				if (remaining <= 0)
					return;
				RawProperty tail;
				tail.locator.name = QStringLiteral("UnparsedTail");
				tail.locator.ranges.append({m_device.pos(), remaining});
				tail.bytesRetained = false;
				tail.interpretation = QStringLiteral("Opaque source range; rereading requires a fresh source check.");
				m_result.unownedProperties.append(std::move(tail));
			}

			QIODevice &m_device;
			const ReaderContext &m_context;
			ParsedSource m_result;
			bool m_bigEndian = false;
			bool m_stopped = false;
		};
	}

	ParsedSource PmrReader::read(QIODevice &source, const ReaderContext &context) const
	{
		return PmrReadSession(source, context).run();
	}
}
