// MDB databases and OMF media share this object vocabulary. Interpret their
// recorded dictionaries, values and local references once, keeping every
// observation. File matching and display selection belong to later stages.

#include "omfobjects_p.h"
#include "audioreader_p.h"

#include <QHash>
#include <QSet>
#include <QStringDecoder>
#include <QtEndian>

namespace Canon
{
	namespace
	{
		using Outcome = ParsedSource::Outcome;
		using Value = Detail::BentoValue;

		struct Name
		{
			QString text;
			bool ambiguous = false;
		};

		using Dictionary = QHash<quint32, Name>;

		QString nameOf(const Dictionary &dictionary, quint32 id)
		{
			const auto found = dictionary.constFind(id);
			return found == dictionary.cend() || found->ambiguous ? QString() : found->text;
		}

		bool ascii(QByteArrayView bytes)
		{
			for (char byte : bytes)
				if (static_cast<unsigned char>(byte) > 127)
					return false;
			return true;
		}

		QByteArrayView stringContent(const QByteArray &bytes)
		{
			QByteArrayView view(bytes);
			const qsizetype terminator = view.indexOf('\0');
			return terminator < 0 ? view : view.first(terminator);
		}

		void define(Dictionary &dictionary, const Value &value)
		{
			auto &entry = dictionary[value.object];
			const auto content = stringContent(value.bytes);
			if (value.state != PropertyReadState::Present || !value.bytesRetained ||
				content.empty() || !ascii(content) || content.indexOf('\0') >= 0)
			{
				entry.ambiguous = true;
				return;
			}
			const QString text = QString::fromLatin1(content.data(), content.size());
			if (!entry.text.isEmpty() && entry.text != text)
				entry.ambiguous = true;
			else
				entry.text = text;
		}

		template <typename T>
		T number(const char *bytes, bool bigEndian)
		{
			return bigEndian ? qFromBigEndian<T>(bytes) : qFromLittleEndian<T>(bytes);
		}

		QByteArray numericKey(quint32 key)
		{
			QByteArray bytes(4, Qt::Uninitialized);
			qToBigEndian(key, bytes.data());
			return bytes;
		}

		struct ByteOrder
		{
			bool seen = false;
			bool uncertain = false;
			std::optional<bool> bigEndian;

			void observe(const Value &value)
			{
				seen = true;
				if (value.state != PropertyReadState::Present ||
					(value.bytes != "II" && value.bytes != "MM"))
					uncertain = true;
				else if (bigEndian && *bigEndian != (value.bytes == "MM"))
					uncertain = true;
				else
					bigEndian = value.bytes == "MM";
			}

			std::optional<bool> established() const
			{
				return uncertain ? std::nullopt : bigEndian;
			}
		};

		struct ReferenceTable
		{
			bool readable = false;
			QHash<quint32, quint32> targets;
			QSet<quint32> ambiguousKeys;
		};

		struct RangeCursor
		{
			qsizetype index = 0;
			qint64 logicalStart = 0;
		};

		bool classType(const QString &type)
		{
			return type == QLatin1String("omfi:ObjectTag") || type == QLatin1String("omfi:ClassID");
		}

		bool explicitUtf8(const QString &name)
		{
			// These are Avid's named UTF-8 counterparts in the inspected dictionaries.
			// A private property's suggestive suffix alone does not prove its encoding.
			return name == QLatin1String("OMFI:DL:PathNameUTF8") ||
				   name == QLatin1String("OMFI:FL:PathNameUTF8") ||
				   name == QLatin1String("OMFI:MCBR:MC:binNameUTF8") ||
				   name == QLatin1String("OMFI:MSML:LastKnownVolumeUTF8");
		}

		class ObjectReader
		{
		public:
			ObjectReader(Detail::BentoReadResult &bento, ParsedSource &result, const Cancellation &cancellation)
				: m_bento(bento), m_result(result), m_cancellation(cancellation) {}

			void read()
			{
				for (const auto &value : m_bento.values)
				{
					if (cancelled())
						break;
					if (value.property == 23)
						define(m_types, value);
					if (value.property == 24)
						define(m_properties, value);
				}
				if (!cancelled())
					header();
				// Cancellation stops I/O and interpretation, but cannot discard values
				// that the container reader has already obtained, including partial ones.
				for (const auto &value : m_bento.values)
				{
					if (value.object == 0)
					{
						m_result.unownedProperties.append(property(value));
						m_result.diagnostics.append(QStringLiteral("Bento object zero cannot be a source-local object handle."));
						incomplete();
						continue;
					}
					if (!m_objects.contains(value.object))
					{
						m_objects.insert(value.object, m_result.objects.size());
						AvidObject object;
						object.handle = value.object;
						m_result.objects.append(std::move(object));
					}
					m_result.objects[m_objects.value(value.object)].properties.append(property(value));
				}
				if (cancelled())
					return;
				buildReferenceTables();
				for (auto &object : m_result.objects)
				{
					if (cancelled())
						return;
					objectFacts(object);
					QVector<RawProperty> audioFields;
					for (auto &property : object.properties)
					{
						if (cancelled())
							return;
						interpret(property);
						if (property.locator.name == QLatin1String("OMFI:WAVD:Summary") ||
							property.locator.name == QLatin1String("OMFI:AIFD:Summary"))
						{
							auto decoded = Detail::decodeAudioSummary(property);
							for (const auto &field : decoded)
								if (field.state == PropertyReadState::Unreadable)
									m_result.diagnostics.append(QStringLiteral("%1: %2").arg(field.locator.name, field.interpretation));
							// These are fields inside an already-read value, not more
							// Bento entries. Keep its original framing outcome intact.
							audioFields += decoded;
						}
					}
					object.properties += audioFields;
				}
			}

			bool hasOmfHeader() const { return m_revision != 0; }
			std::optional<OmfRevision> revision() const
			{
				if (m_revision == 1)
					return OmfRevision::V1;
				if (m_revision == 2)
					return OmfRevision::V2;
				return {};
			}

		private:
			bool cancelled()
			{
				if (!m_cancellation.cancelled())
					return false;
				m_result.outcome = Outcome::Cancelled;
				return true;
			}

			void incomplete()
			{
				if (m_result.outcome == Outcome::Complete)
					m_result.outcome = Outcome::Incomplete;
			}

			void unreadable(RawProperty &property, const QString &why)
			{
				property.state = PropertyReadState::Unreadable;
				property.interpretation = why;
				incomplete();
			}

			void header()
			{
				QSet<int> versions;
				bool badVersion = false;
				bool legacyHead = false;
				bool modernHead = false;
				bool uncertainHead = false;
				for (const auto &value : m_bento.values)
				{
					if (cancelled())
						return;
					const QString name = nameOf(m_properties, value.property);
					const QString type = nameOf(m_types, value.type);
					if (name == QLatin1String("OMFI:ByteOrder") || name == QLatin1String("OMFI:HEAD:ByteOrder"))
					{
						if (type == QLatin1String("omfi:Short") || type == QLatin1String("omfi:Int16"))
							m_orders[value.object].observe(value);
						else
						{
							m_orders[value.object].seen = true;
							m_orders[value.object].uncertain = true;
						}
					}
					if (value.object != 1)
						continue;
					if ((name == QLatin1String("OMFI:ObjID") || name == QLatin1String("OMFI:OOBJ:ObjClass")) &&
						(value.state != PropertyReadState::Present || value.bytes != "HEAD" || !classType(type)))
						uncertainHead = true;
					if (value.state != PropertyReadState::Present)
						continue;
					if (name == QLatin1String("OMFI:ObjID") && value.bytes == "HEAD")
						legacyHead = true;
					if (name == QLatin1String("OMFI:OOBJ:ObjClass") && value.bytes == "HEAD")
						modernHead = true;
				}
				for (const auto &value : m_bento.values)
				{
					if (cancelled())
						return;
					if (value.object != 1)
						continue;
					const QString name = nameOf(m_properties, value.property);
					if (name != QLatin1String("OMFI:Version") && name != QLatin1String("OMFI:HEAD:Version"))
						continue;
					if (value.state != PropertyReadState::Present || value.bytes.size() != 2 ||
						nameOf(m_types, value.type) != QLatin1String("omfi:VersionType"))
						badVersion = true;
					else if (static_cast<quint8>(value.bytes[0]) == 1)
						versions.insert(1);
					else if (value.bytes == QByteArray::fromHex("0200"))
						versions.insert(2);
					else if (name == QLatin1String("OMFI:Version") && legacyHead && !modernHead &&
							 value.bytes == QByteArray::fromHex("0001"))
						versions.insert(1); // Recorded Avid legacy spelling; never swap major/minor generically.
					else
						badVersion = true;
				}
				if (!badVersion && !uncertainHead && versions.size() == 1 && (legacyHead || modernHead))
					m_revision = *versions.cbegin();
				else
					m_result.diagnostics.append(QStringLiteral("OMF object revision is absent, unsupported or conflicting; revision-dependent values remain raw."));
				if (badVersion && m_result.outcome == Outcome::Complete)
					m_result.outcome = Outcome::Unsupported;
				if (!m_orders.value(1).established())
					m_result.diagnostics.append(QStringLiteral("HEAD metadata byte order is absent or conflicting; multi-byte numbers remain raw."));
				qsizetype ambiguous = 0;
				for (const auto &entry : m_properties)
					if (entry.ambiguous)
						++ambiguous;
				for (const auto &entry : m_types)
					if (entry.ambiguous)
						++ambiguous;
				if (ambiguous)
					m_result.diagnostics.append(QStringLiteral("%1 dictionary IDs have ambiguous definitions; all definitions are retained without choosing a name.").arg(ambiguous));
			}

			std::optional<bool> byteOrder(quint32 object) const
			{
				const auto head = m_orders.value(1).established();
				if (m_revision != 1 || object == 1)
					return head;
				const auto local = m_orders.value(object);
				// The legacy toolkit's override contract and implementation disagree.
				// A contrary local order needs a verified specimen before decoding it.
				if (local.seen && (!local.established() || local.established() != head))
					return std::nullopt;
				return head;
			}

			RawProperty property(const Value &value)
			{
				RawProperty property;
				property.locator.name = nameOf(m_properties, value.property);
				// A large database repeats its small dictionary thousands of times.
				// QByteArray sharing avoids a separate key allocation for every value.
				auto key = m_keys.constFind(value.property);
				if (key == m_keys.cend())
					key = m_keys.insert(value.property, numericKey(value.property));
				property.locator.key = *key; // Canonical numeric key, not the source's byte order.
				property.locator.objectNumber = value.object;
				property.locator.ranges = value.ranges;
				property.encoding = value.bytes;
				property.bytesRetained = value.bytesRetained;
				property.state = value.state;
				property.interpretation = value.problem;
				auto native = QSharedPointer<BentoPropertyContext>::create();
				native->property = value.property;
				native->type = value.type;
				native->generation = value.generation;
				native->referenceListObject = value.referenceListObject;
				native->typeName = nameOf(m_types, value.type);
				native->tocRanges = value.tocRanges;
				native->metadataBigEndian = byteOrder(value.object);
				property.bento = native;
				return property;
			}

			void objectFacts(AvidObject &object) const
			{
				QSet<QByteArray> classes;
				QSet<QByteArray> identities;
				bool uncertainIdentity = false;
				bool uncertainClass = false;
				for (const auto &property : object.properties)
				{
					const auto &name = property.locator.name;
					if (name == QLatin1String("OMFI:ObjID") || name == QLatin1String("OMFI:OOBJ:ObjClass"))
					{
						if (property.state != PropertyReadState::Present || !classType(property.bento->typeName))
							uncertainClass = true;
						else
							classes.insert(property.encoding);
					}
					if (name != QLatin1String("OMFI:MOBJ:MobID"))
						continue;
					if (property.state != PropertyReadState::Present || property.bento->typeName != QLatin1String("omfi:UID") ||
						(property.encoding.size() != 12 && property.encoding.size() != 32))
						uncertainIdentity = true;
					else
						identities.insert(property.encoding);
				}
				if (!uncertainClass && classes.size() == 1)
				{
					const auto &cls = *classes.cbegin();
					if (cls == "MMOB")
						object.role = AvidObject::Role::Master;
					if (cls == "CMOB")
						object.role = AvidObject::Role::Composition;
					// SMOB and legacy MOBJ need descriptor/usage relationships to establish their role.
				}
				if (!uncertainIdentity && identities.size() == 1)
				{
					object.recordedIdentity = *identities.cbegin();
					object.identityEncoding = object.recordedIdentity.size() == 12
												  ? QStringLiteral("OMF UID: three recorded 32-bit words; no normalization")
												  : QStringLiteral("Avid 32-byte MobID: recorded bytes; no normalization");
				}
			}

			void text(RawProperty &property, bool utf8)
			{
				const auto content = stringContent(property.encoding);
				if (utf8)
				{
					property.textEncoding = TextEncoding::Utf8;
					property.textEncodingBasis = EvidenceBasis::Recorded;
					QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
					const QString decoded = decoder.decode(content);
					if (decoder.hasError())
						unreadable(property, QStringLiteral("The named UTF8 property contains invalid UTF-8; original bytes retained."));
					else
						property.decoded = decoded;
				}
				else if (ascii(content))
				{
					property.textEncoding = TextEncoding::Ascii;
					property.textEncodingBasis = EvidenceBasis::Derived;
					property.decoded = QString::fromLatin1(content.data(), content.size());
					property.interpretation = QStringLiteral("All recorded text bytes are ASCII; the producer's broader character set is not established.");
				}
				else
				{
					property.textEncoding = TextEncoding::Unknown;
					property.interpretation = QStringLiteral("Untagged non-ASCII text. MacRoman fits known specimens but is not established for every OMF/MDB source; original bytes retained.");
				}
			}

			void buildReferenceTables()
			{
				QSet<quint32> unusable;
				for (const auto &value : m_bento.values)
				{
					if (cancelled())
						return;
					if (value.property != 31 || value.type != 32)
						continue;
					auto &table = m_referenceTables[value.object];
					if (value.state != PropertyReadState::Present || value.bytes.size() % 8 != 0)
					{
						unusable.insert(value.object);
						continue;
					}
					table.readable = true;
					for (qsizetype offset = 0; offset < value.bytes.size(); offset += 8)
					{
						if (cancelled())
							return;
						const quint32 key = number<quint32>(value.bytes.constData() + offset, m_bento.containerBigEndian);
						const quint32 target = number<quint32>(value.bytes.constData() + offset + 4, m_bento.containerBigEndian);
						if (table.targets.contains(key) && table.targets.value(key) != target)
							table.ambiguousKeys.insert(key);
						else
							table.targets.insert(key, target);
					}
				}
				for (quint32 object : unusable)
					m_referenceTables[object].readable = false;
			}

			void reference(RawProperty &property, qsizetype offset, qsizetype width, qsizetype index,
						   RangeCursor &cursor, QByteArray uid = {})
			{
				const quint32 key = number<quint32>(property.encoding.constData() + offset, m_bento.containerBigEndian);
				Relationship relationship;
				relationship.origin = property.locator.objectNumber;
				relationship.locator = property.locator;
				relationship.locator.ranges.clear();
				while (cursor.index < property.locator.ranges.size() &&
					   cursor.logicalStart + property.locator.ranges[cursor.index].length <= offset)
				{
					cursor.logicalStart += property.locator.ranges[cursor.index].length;
					++cursor.index;
				}
				qint64 skip = offset - cursor.logicalStart;
				qsizetype left = width;
				for (qsizetype part = cursor.index; part < property.locator.ranges.size(); ++part)
				{
					const auto &range = property.locator.ranges[part];
					if (skip >= range.length)
					{
						skip -= range.length;
						continue;
					}
					const qint64 count = qMin<qint64>(left, range.length - skip);
					relationship.locator.ranges.append({range.offset + skip, count});
					left -= count;
					skip = 0;
					if (!left)
						break;
				}
				QVariantMap recorded{{QStringLiteral("key"), key}, {QStringLiteral("index"), qlonglong(index)}, {QStringLiteral("bytes"), property.encoding.mid(offset, width)}};
				if (!uid.isEmpty())
					recorded.insert(QStringLiteral("mobId"), uid);
				relationship.recordedReference = recorded;
				relationship.referenceEncoding = QStringLiteral("OMF%1 %2-byte reference; key in Bento %3-endian order")
													 .arg(m_revision)
													 .arg(width)
													 .arg(m_bento.containerBigEndian ? QStringLiteral("big") : QStringLiteral("little"));
				quint32 target = key;
				const quint32 list = property.bento->referenceListObject;
				if (key == 0)
					relationship.explanation = QStringLiteral("Recorded null reference.");
				else if (m_bento.major >= 2 && list != 0)
				{
					const auto found = m_referenceTables.constFind(list);
					if (found == m_referenceTables.cend() || !found->readable ||
						!found->targets.contains(key) || found->ambiguousKeys.contains(key))
					{
						target = 0;
						relationship.explanation = QStringLiteral("Reference recording table is absent, unreadable or ambiguous for this key.");
					}
					else
					{
						target = found->targets.value(key);
						relationship.explanation = QStringLiteral("Resolved through recorded Bento reference table %1.").arg(list);
					}
				}
				if (target != 0 && m_objects.contains(target))
					relationship.target = target;
				else if (target != 0)
					relationship.explanation = QStringLiteral("Recorded object %1 was not found in this source.").arg(target);
				m_result.relationships.append(std::move(relationship));
			}

			void references(RawProperty &property, const QString &type)
			{
				if (!m_revision || (type == QLatin1String("omfi:MobIndex") && m_revision != 1))
				{
					property.interpretation = QStringLiteral("Reference layout requires an established, compatible OMF object revision; original bytes retained.");
					return;
				}
				const qsizetype width = m_revision == 1 ? 8 : 4;
				RangeCursor cursor;
				if (type == QLatin1String("omfi:ObjRef"))
				{
					if (property.encoding.size() != width)
						unreadable(property, QStringLiteral("Reference width does not match the recorded OMF revision; original bytes retained."));
					else
						reference(property, 0, width, 0, cursor);
					return;
				}
				const bool index = type == QLatin1String("omfi:MobIndex");
				const qsizetype stride = index ? 12 + width : width;
				if (property.encoding.size() < 2 || (property.encoding.size() - 2) % stride != 0)
				{
					unreadable(property, QStringLiteral("Reference array has incomplete framing; original bytes retained."));
					return;
				}
				const qsizetype count = (property.encoding.size() - 2) / stride;
				property.interpretation = QStringLiteral("%1 slots established by the value's byte extent, as in the OMF toolkit; recorded count prefix retained.").arg(count);
				if (property.bento->metadataBigEndian)
				{
					const quint16 prefix = number<quint16>(property.encoding.constData(), *property.bento->metadataBigEndian);
					if (prefix != 0xffff && prefix != count)
						property.interpretation += QStringLiteral(" Recorded prefix %1 disagrees with extent; no slots discarded.").arg(prefix);
				}
				for (qsizetype slot = 0; slot < count; ++slot)
				{
					if (cancelled())
						return;
					const qsizetype offset = 2 + slot * stride;
					reference(property, offset + (index ? 12 : 0), width, slot, cursor,
							  index ? property.encoding.mid(offset, 12) : QByteArray());
				}
			}

			void interpret(RawProperty &property)
			{
				if (property.state != PropertyReadState::Present || !property.bytesRetained)
					return;
				const QString &type = property.bento->typeName;
				const auto &bytes = property.encoding;
				const QString &name = property.locator.name;
				if (property.bento->property == 23 || property.bento->property == 24)
				{
					text(property, false);
					return;
				}
				if (type == QLatin1String("omfi:String") || type == QLatin1String("omfi:UniqueName"))
				{
					text(property, explicitUtf8(name));
					return;
				}
				if (type == QLatin1String("omfi:ObjRef") || type == QLatin1String("omfi:ObjRefArray") || type == QLatin1String("omfi:MobIndex"))
				{
					references(property, type);
					return;
				}
				if ((type == QLatin1String("omfi:ObjectTag") || type == QLatin1String("omfi:ClassID")) && bytes.size() == 4 && ascii(bytes))
				{
					property.decoded = QString::fromLatin1(bytes);
					return;
				}
				if (type == QLatin1String("omfi:VersionType") && bytes.size() == 2)
				{
					property.decoded = QVariantMap{{QStringLiteral("major"), quint8(bytes[0])}, {QStringLiteral("minor"), quint8(bytes[1])}};
					return;
				}
				if (type == QLatin1String("omfi:UID"))
				{
					property.decoded = bytes;
					property.interpretation = QStringLiteral("Recorded identity bytes; Avid MDBs contain both standard 12-byte UIDs and 32-byte identities. No byte-order conversion or cross-file matching performed.");
					return;
				}
				if ((type == QLatin1String("omfi:Char") || type == QLatin1String("omfi:Int8")) && bytes.size() == 1)
					property.decoded = int(static_cast<qint8>(bytes[0]));
				else if ((type == QLatin1String("omfi:Uchar") || type == QLatin1String("omfi:UInt8")) && bytes.size() == 1)
					property.decoded = uint(static_cast<quint8>(bytes[0]));
				else if (type == QLatin1String("omfi:Boolean") && bytes.size() == 1 && (bytes[0] == 0 || bytes[0] == 1))
					property.decoded = bytes[0] != 0;
				else if (property.bento->metadataBigEndian)
					numeric(property, *property.bento->metadataBigEndian);
				if (!property.decoded.isValid() && property.interpretation.isEmpty())
					property.interpretation = QStringLiteral("Native property/type and original bytes retained; this private type, shape or byte order has no established interpretation here.");
			}

			void numeric(RawProperty &property, bool bigEndian) const
			{
				const QString &type = property.bento->typeName;
				const auto &bytes = property.encoding;
				const char *data = bytes.constData();
				if (bytes.size() == 2)
				{
					if (type == QLatin1String("omfi:Short") || type == QLatin1String("omfi:Int16") ||
						type == QLatin1String("omfi:AttrKind") || type == QLatin1String("omfi:TrackType") ||
						type == QLatin1String("omfi:PhysicalMobType") || type == QLatin1String("omfi:LayoutType"))
						property.decoded = int(number<qint16>(data, bigEndian));
					else if (type == QLatin1String("omfi:Ushort") || type == QLatin1String("omfi:UInt16"))
						property.decoded = uint(number<quint16>(data, bigEndian));
				}
				else if (bytes.size() == 4)
				{
					if (type == QLatin1String("omfi:Long") || type == QLatin1String("omfi:Int32") || type == QLatin1String("omfi:UsageCodeType") ||
						type == QLatin1String("omfi:Position32") || type == QLatin1String("omfi:Length32"))
						property.decoded = number<qint32>(data, bigEndian);
					else if (type == QLatin1String("omfi:Ulong") || type == QLatin1String("omfi:UInt32"))
						property.decoded = number<quint32>(data, bigEndian);
				}
				else if (bytes.size() == 8)
				{
					if (type == QLatin1String("omfi:Int64") || type == QLatin1String("omfi:Position64") || type == QLatin1String("omfi:Length64"))
						property.decoded = number<qint64>(data, bigEndian);
					else if (type == QLatin1String("omfi:UInt64"))
						property.decoded = number<quint64>(data, bigEndian);
					else if (type == QLatin1String("omfi:Rational") || type == QLatin1String("omfi:ExactEditRate"))
						property.decoded = QVariantMap{{QStringLiteral("numerator"), number<qint32>(data, bigEndian)},
													   {QStringLiteral("denominator"), number<qint32>(data + 4, bigEndian)}};
				}
				else if (bytes.size() == 5 && type == QLatin1String("omfi:TimeStamp") && (bytes[4] == 0 || bytes[4] == 1))
				{
					property.decoded = QVariantMap{{QStringLiteral("secondsSince1970"), number<quint32>(data, bigEndian)},
												   {QStringLiteral("isGMT"), bytes[4] != 0}};
					property.interpretation = QStringLiteral("Recorded epoch seconds and GMT flag; local timestamps are not silently converted to UTC.");
				}
			}

			Detail::BentoReadResult &m_bento;
			ParsedSource &m_result;
			const Cancellation &m_cancellation;
			Dictionary m_properties;
			Dictionary m_types;
			QHash<quint32, QByteArray> m_keys;
			QHash<quint32, qsizetype> m_objects;
			QHash<quint32, ByteOrder> m_orders;
			QHash<quint32, ReferenceTable> m_referenceTables;
			int m_revision = 0;
		};
	}

	ParsedSource Detail::interpretOmfObjects(BentoReadResult bento, const ReaderContext &context, MetadataSource kind)
	{
		ParsedSource result;
		result.container = ParsedSource::Container::Bento;
		result.outcome = bento.outcome;
		result.unownedProperties = std::move(bento.structure);
		result.diagnostics = std::move(bento.diagnostics);
		ObjectReader reader(bento, result, context.cancellation);
		reader.read();
		result.omfRevision = reader.revision();
		if (kind == MetadataSource::Omf)
		{
			if (reader.hasOmfHeader())
				result.container = ParsedSource::Container::Omf;
			else if (result.outcome == Outcome::Complete)
			{
				result.outcome = Outcome::Unsupported;
				result.diagnostics.append(QStringLiteral("Bento values retained, but a supported OMF HEAD was not established."));
			}
		}
		if (context.cancellation.cancelled())
			result.outcome = Outcome::Cancelled;
		auto receipt = QSharedPointer<SourceSnapshot>::create(context.snapshot ? *context.snapshot : SourceSnapshot{});
		receipt->source = kind;
		receipt->readState = result.outcome == Outcome::Complete  ? SourceReadState::Complete
							 : result.outcome == Outcome::IoError ? SourceReadState::Unreadable
																  : SourceReadState::Incomplete;
		result.snapshot = receipt;
		for (auto &object : result.objects)
			object.snapshot = receipt;
		return result;
	}
}
