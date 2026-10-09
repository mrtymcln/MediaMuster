// Decodes PMR records whilst keeping the Legacy and Unicode sets separate.
// Reading bytes, interpreting text, and following the record layout are kept apart
// so format rules and damaged-file handling can be checked without the UI.

#include "pmrreader.h"
#include "sourcestorage_p.h"

#include <QByteArrayView>
#include <QStringDecoder>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <type_traits>
#include <utility>

namespace Canon
{
	namespace
	{
		using Outcome = ParsedSource::Outcome;
		constexpr quint32 signature = 0x000007a9;
		constexpr qint32 unicodeSetVersion = 16;
		constexpr quint16 nullTextLength = 0xffff;

		// Only expected input failures cross the grammar this way. Allocation failures
		// and programming errors are not converted into apparently usable PMR results.
		struct ParseFailure
		{
			Outcome outcome;
			QString message;
		};

		void checkCancellation(const Cancellation &cancellation)
		{
			if (cancellation.cancelled())
				throw ParseFailure{Outcome::Cancelled, QStringLiteral("PMR read cancelled.")};
		}

		/// Owns a cursor, not the device. Its captured extent bounds every read/range.
		class Input
		{
		public:
			Input(QIODevice &device, const Cancellation &cancellation)
				: m_device(device), m_cancellation(cancellation), m_extent(device.size())
			{
				if (m_extent < 0 || !device.seek(0))
					throw ParseFailure{Outcome::IoError, QStringLiteral("Cannot determine PMR extent or seek to its start.")};
			}

			qint64 position() const { return m_position; }
			qint64 remaining() const { return m_extent - m_position; }

			void append(RawProperty &property, qsizetype count)
			{
				// Field widths are fixed or uint16-counted. A corrupt record count never
				// reserves a vector, and an unknown trailing payload is never loaded.
				std::array<char, 4096> buffer{};
				while (count > 0)
				{
					checkCancellation(m_cancellation);
					if (remaining() == 0)
						throw ParseFailure{Outcome::Incomplete, QStringLiteral("Truncated %1 at byte %2.")
																	.arg(property.locator.name)
																	.arg(m_position)};
					const qint64 requested = std::min({qint64(count), remaining(), qint64(buffer.size())});
					const qint64 received = m_device.read(buffer.data(), requested);
					if (received <= 0)
					{
						const auto outcome = received == 0 && m_device.atEnd() ? Outcome::Incomplete : Outcome::IoError;
						throw ParseFailure{outcome, QStringLiteral("Cannot read %1 at byte %2: %3")
														.arg(property.locator.name)
														.arg(m_position)
														.arg(m_device.errorString())};
					}
					property.encoding.append(buffer.data(), received);
					m_position += received;
					property.locator.ranges.front().length = property.encoding.size();
					count -= received;
				}
			}

			void verifyCompletion() const
			{
				checkCancellation(m_cancellation); // Includes cancellation during the final read.
				if (m_device.size() != m_extent)
					throw ParseFailure{Outcome::Incomplete, QStringLiteral("PMR length changed during reading; source must be checked again.")};
			}

		private:
			QIODevice &m_device;
			const Cancellation &m_cancellation;
			const qint64 m_extent;
			qint64 m_position = 0;
		};

		enum class TextField
		{
			LegacyFilename,
			UnicodeFilename,
			Project
		};

		qsizetype textCapacity(TextField field)
		{
			// MC 26.8 ReadPmrRec/AStream capacities include room for a terminator.
			// They are compatibility constraints, not a cap on evidence retention.
			switch (field)
			{
			case TextField::LegacyFilename:
				return 2048;
			case TextField::UnicodeFilename:
				return 1024;
			case TextField::Project:
				return 64;
			}
			Q_UNREACHABLE();
		}

		/// Interprets a fully captured counted payload. No device access or selection.
		/// An empty error means either decoded text or explicitly unknown legacy text.
		QString decodeText(RawProperty &property, TextField field)
		{
			QByteArrayView text(property.encoding);
			text = text.sliced(sizeof(quint16));
			if (field == TextField::UnicodeFilename)
			{
				if (text.size() < 2 || text[0] != '\0')
					return QStringLiteral("Invalid Unicode framing: requires two reserved bytes, with the first zero.");
				text = text.sliced(2); // Keep both original reserved bytes in property.encoding.
			}
			if (text.size() >= textCapacity(field))
				return QStringLiteral("Counted text exceeds the verified Avid input capacity; full bytes retained.");

			// Avid consumes a C string, but bytes after its terminator remain evidence.
			const auto terminator = std::find(text.cbegin(), text.cend(), '\0');
			text = text.first(terminator - text.cbegin());
			if (field == TextField::UnicodeFilename)
			{
				QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
				const QString decoded = decoder.decode(text);
				if (decoder.hasError())
					return QStringLiteral("Invalid UTF-8 in the explicitly Unicode filename.");
				property.decoded = decoded;
				property.interpretation = QStringLiteral("UTF-8 required by the Unicode filename layout.");
			}
			else if (std::all_of(text.cbegin(), text.cend(), [](char c)
								 { return static_cast<unsigned char>(c) < 128; }))
			{
				property.textEncoding = TextEncoding::Ascii;
				property.textEncodingBasis = EvidenceBasis::Derived;
				property.decoded = QString::fromLatin1(text);
				property.interpretation = QStringLiteral("ASCII subset only; the legacy codepage is not declared.");
			}
			else
				property.interpretation = QStringLiteral("Untagged legacy text; bytes retained without choosing a codepage.");
			return {};
		}

		class Grammar
		{
		public:
			Grammar(Input &input, ParsedSource &result) : m_input(input), m_result(result) {}

			void parse()
			{
				auto &magic = fixed(m_result.unownedProperties, QStringLiteral("Magic"), sizeof(quint32));
				if (qFromLittleEndian<quint32>(magic.encoding.constData()) == signature)
					m_bigEndian = false;
				else if (qFromBigEndian<quint32>(magic.encoding.constData()) == signature)
					m_bigEndian = true;
				else
					throw ParseFailure{Outcome::Malformed, QStringLiteral("Invalid PMR signature.")};
				magic.decoded = signature;
				m_result.container = ParsedSource::Container::Pmr;

				const auto version = integer<qint32>(m_result.unownedProperties, QStringLiteral("Legacy.Version"));
				if (version >= 9)
					throw ParseFailure{Outcome::Unsupported, QStringLiteral("Unverified PMR legacy version %1.").arg(version)};
				if (version <= 0)
					m_result.diagnostics.append(QStringLiteral("Version %1 follows the inspected signed branch; a historical writer is not established.").arg(version));
				readSet(PmrFileSet::Legacy, version);
				if (m_input.remaining() == 0)
					return;

				const auto extension = integer<qint32>(m_result.unownedProperties, QStringLiteral("Extension.Version"));
				if (extension != unicodeSetVersion)
					throw ParseFailure{Outcome::Unsupported, QStringLiteral("Unverified PMR extension %1.").arg(extension)};
				readSet(PmrFileSet::Unicode, extension);
				if (m_input.remaining() != 0)
					throw ParseFailure{Outcome::Unsupported, QStringLiteral("Uninterpreted bytes follow the Unicode set.")};
			}

		private:
			RawProperty &begin(QVector<RawProperty> &properties, const QString &name)
			{
				auto &property = properties.emplaceBack();
				property.locator.name = name;
				property.locator.ranges.append({m_input.position(), 0});
				property.state = PropertyReadState::Unreadable;
				return property;
			}

			RawProperty &fixed(QVector<RawProperty> &properties, const QString &name, qsizetype width)
			{
				auto &property = begin(properties, name);
				m_input.append(property, width);
				property.state = PropertyReadState::Present;
				return property;
			}

			template <typename T>
			T decodeInteger(const RawProperty &property) const
			{
				static_assert(std::is_integral_v<T> && (sizeof(T) == 2 || sizeof(T) == 4));
				return m_bigEndian ? qFromBigEndian<T>(property.encoding.constData()) : qFromLittleEndian<T>(property.encoding.constData());
			}

			template <typename T>
			T integer(QVector<RawProperty> &properties, const QString &name)
			{
				auto &property = fixed(properties, name, sizeof(T));
				const T value = decodeInteger<T>(property);
				property.decoded = QVariant::fromValue(value);
				return value;
			}

			void text(QVector<RawProperty> &properties, const QString &name, TextField field)
			{
				auto &property = begin(properties, name);
				property.textEncoding = field == TextField::UnicodeFilename ? TextEncoding::Utf8 : TextEncoding::Unknown;
				if (field == TextField::UnicodeFilename)
					property.textEncodingBasis = EvidenceBasis::Recorded;
				m_input.append(property, sizeof(quint16));
				const auto length = decodeInteger<quint16>(property);
				if (length == nullTextLength)
				{
					property.state = PropertyReadState::Present;
					property.decoded = QString{};
					property.interpretation = QStringLiteral("Recorded null-string marker, distinct from an absent field.");
					return;
				}
				m_input.append(property, length);
				const QString error = decodeText(property, field);
				if (error.isEmpty())
					property.state = PropertyReadState::Present;
				else
				{
					property.interpretation = error;
					m_result.diagnostics.append(QStringLiteral("%1 at byte %2: %3").arg(name).arg(property.locator.ranges.front().offset).arg(error));
					m_result.outcome = Outcome::Malformed;
				}
			}

			void readSet(PmrFileSet kind, qint32 version)
			{
				auto &set = m_result.recordSets.emplaceBack();
				set.pmrFileSet = kind;
				set.name = kind == PmrFileSet::Legacy ? QStringLiteral("Legacy") : QStringLiteral("Unicode");
				set.version = version;
				set.declaredCount = integer<quint32>(m_result.unownedProperties, set.name + QStringLiteral(".Count"));
				for (quint32 record = 0; record < set.declaredCount; ++record)
					readRecord(set);
				set.framingComplete = true;
			}

			void readRecord(RecordSet &set)
			{
				auto &object = m_result.objects.emplaceBack();
				object.handle = static_cast<ObjectHandle>(m_result.objects.size());
				object.role = AvidObject::Role::FileSource;
				const qsizetype identityWidth = set.version <= 7 ? 8 : 32;
				object.identityEncoding = QStringLiteral("PMR/%1/%2")
											  .arg(set.version <= 7 ? QStringLiteral("OMF-8") : QStringLiteral("AAF-32"),
												   m_bigEndian ? QStringLiteral("big-endian") : QStringLiteral("little-endian"));
				set.objects.append(object.handle);

				auto &file = fixed(object.properties, QStringLiteral("FileMobId"), identityWidth);
				file.decoded = file.encoding;
				file.interpretation = object.identityEncoding;
				object.recordedIdentity = file.encoding;
				text(object.properties, QStringLiteral("Filename"),
					 set.pmrFileSet == PmrFileSet::Unicode ? TextField::UnicodeFilename : TextField::LegacyFilename);
				if (set.version == 1)
				{
					for (const auto &name : {QStringLiteral("Project"), QStringLiteral("MasterMobId")})
					{
						auto &absent = object.properties.emplaceBack();
						absent.locator.name = name;
						absent.state = PropertyReadState::Absent;
						absent.interpretation = QStringLiteral("Version 1 does not store this field.");
					}
				}
				else
				{
					text(object.properties, QStringLiteral("Project"), TextField::Project);
					auto &master = fixed(object.properties, QStringLiteral("MasterMobId"), identityWidth);
					master.decoded = master.encoding;
					master.interpretation = object.identityEncoding;
					auto &reference = m_result.relationships.emplaceBack();
					reference.origin = object.handle;
					reference.locator = master.locator;
					reference.recordedReference = master.encoding;
					reference.referenceEncoding = object.identityEncoding;
					reference.explanation = QStringLiteral("Recorded master identity; no target object is defined here.");
				}
				integer<quint32>(object.properties, QStringLiteral("ModificationWord"));
				object.properties.last().interpretation = QStringLiteral("Recorded uint32; no epoch or timezone selected.");
			}

			Input &m_input;
			ParsedSource &m_result;
			bool m_bigEndian = false;
		};

		void retainTail(ParsedSource &result, const Input &input)
		{
			if (input.remaining() == 0)
				return;
			auto &tail = result.unownedProperties.emplaceBack();
			tail.locator.name = QStringLiteral("UnparsedTail");
			tail.locator.ranges.append({input.position(), input.remaining()});
			tail.bytesRetained = false;
			tail.interpretation = QStringLiteral("Source range only; revalidate the source before rereading.");
		}
	}

	ParsedSource PmrReader::read(QIODevice &source, const ReaderContext &context) const
	{
		ParsedSource result;
		result.outcome = Outcome::Complete;
		try
		{
			checkCancellation(context.cancellation);
			if (!source.isOpen() || !source.isReadable() || source.isSequential() || source.isTextModeEnabled())
				throw ParseFailure{Outcome::IoError, QStringLiteral("PMR input must be open, readable, seekable and binary.")};
			Input input(source, context.cancellation);
			try
			{
				Grammar(input, result).parse();
				input.verifyCompletion();
			}
			catch (const ParseFailure &failure)
			{
				result.outcome = failure.outcome;
				result.diagnostics.append(failure.message);
			}
			retainTail(result, input);
		}
		catch (const ParseFailure &failure)
		{
			result.outcome = failure.outcome;
			result.diagnostics.append(failure.message);
		}
		SourceSnapshot receipt = context.snapshot ? *context.snapshot : SourceSnapshot{};
		receipt.source = MetadataSource::Pmr;
		receipt.readState = result.outcome == Outcome::Complete ? SourceReadState::Complete : (result.outcome == Outcome::IoError ? SourceReadState::Unreadable : SourceReadState::Incomplete);
		result.snapshot = SourceSnapshotRef::create(receipt);
		for (auto &object : result.objects)
			object.snapshot = result.snapshot;
		Detail::squeezeSourceStorage(result, context.cancellation);
		return result;
	}
}
