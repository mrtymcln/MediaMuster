// MXF stores typed properties inside metadata sets. The Primer maps each local
// tag to its real property identity; fixed tag numbers are never a substitute.
// Keep raw bytes alongside decoded values and resolve references only within
// the same partition's metadata copy. File matching and display come later.

#include "mxfobjects_p.h"
#include "mxfcatalogue_p.h"

#include <QHash>
#include <QStringDecoder>
#include <QtEndian>
#include <cstring>
#include <optional>
#include <utility>

namespace MediaEngine
{
	namespace
	{
		namespace Schema = Detail::MxfSchema;
		using Type = Schema::Type;
		using Outcome = ParsedSource::Outcome;

		QByteArray registryKey(QByteArray key, bool set = false)
		{
			// Only SMPTE ULs have a registry-version byte. Private UUIDs do not.
			if (key.size() == 16 && key.startsWith(QByteArray::fromHex("060e2b34")))
			{
				key[7] = 0;
				if (set && key[4] == 2 && (key[5] == 0x13 || key[5] == 0x53))
					key[5] = 0x53;
			}
			return key;
		}

		struct Catalogue
		{
			QHash<QByteArray, const Schema::SetDefinition *> sets;
			QHash<QString, const Schema::SetDefinition *> namedSets;
			QHash<QByteArray, QVector<const Schema::ItemDefinition *>> items;
			QHash<const Schema::SetDefinition *, QString> setNames;
			QHash<const Schema::ItemDefinition *, QString> propertyNames;
			QVector<QString> typeNames;

			Catalogue()
			{
				for (const auto &set : Schema::sets)
				{
					sets.insert(registryKey(QByteArray::fromHex(set.key), true), &set);
					const QString name = QString::fromLatin1(set.name);
					namedSets.insert(name, &set);
					setNames.insert(&set, name);
				}
				for (const auto &item : Schema::items)
				{
					items[registryKey(QByteArray::fromHex(item.key))].append(&item);
					propertyNames.insert(&item, QString::fromLatin1(item.owner) + QLatin1Char('.') + QString::fromLatin1(item.name));
				}
				// Qt shares these immutable strings across every parsed occurrence.
				for (const auto &type : Schema::types)
					typeNames.append(QString::fromLatin1(type.name));
			}

			bool inherits(const Schema::SetDefinition *set, const char *name) const
			{
				while (set)
				{
					if (std::strcmp(set->name, name) == 0)
						return true;
					if (std::strcmp(set->name, set->parent) == 0)
						break;
					set = namedSets.value(QString::fromLatin1(set->parent));
				}
				return false;
			}

			const Schema::ItemDefinition *item(const QByteArray &auid, const Schema::SetDefinition *set) const
			{
				const auto found = items.constFind(registryKey(auid));
				if (found == items.cend())
					return nullptr;
				for (const auto *candidate : *found)
					if (inherits(set, candidate->owner))
						return candidate;
				// A registered AUID still establishes type in an unknown/private set.
				// Multiple named uses require known owner context to choose a name.
				return found->size() == 1 ? found->first() : nullptr;
			}
		};

		bool avidRoot(const AvidObject &object)
		{
			// This exact private UUID is specified by libMXF's Avid writer. Its
			// fields reuse ULs with different layouts from the normal metadata sets.
			static const QByteArray key = QByteArray::fromHex("8053080036210804b3b398a51c9011d4");
			return object.mxf && object.mxf->key == key;
		}

		const Catalogue &catalogue()
		{
			static const Catalogue value;
			return value;
		}

		const Schema::TypeDefinition &definition(Type type)
		{
			return Schema::types[static_cast<unsigned>(type)];
		}

		const QString &typeName(Type type)
		{
			return catalogue().typeNames[static_cast<unsigned>(type)];
		}

		qsizetype fixedSize(Type type)
		{
			const auto &def = definition(type);
			switch (def.shape)
			{
			case Schema::Shape::Basic:
				return def.size;
			case Schema::Shape::Interpreted:
				return def.size ? def.size : fixedSize(def.base);
			case Schema::Shape::Array:
				return def.size * fixedSize(def.base);
			case Schema::Shape::Compound:
			{
				qsizetype size = 0;
				for (unsigned i = 0; i < def.memberCount; ++i)
				{
					const auto memberSize = fixedSize(Schema::members[def.firstMember + i].type);
					if (!memberSize)
						return 0;
					size += memberSize;
				}
				return size;
			}
			}
			return 0;
		}

		struct Decoded
		{
			QVariant value;
			QString explanation;
			std::optional<TextEncoding> text;
			bool valid = true;
		};

		Decoded invalid(const QString &reason)
		{
			return {{}, reason, {}, false};
		}

		Decoded utf16(QByteArrayView bytes, bool bigEndian)
		{
			Decoded result;
			result.text = bigEndian ? TextEncoding::Utf16BE : TextEncoding::Utf16LE;
			if (bytes.size() % 2)
			{
				result.valid = false;
				result.explanation = QStringLiteral("UTF-16 has an incomplete two-byte code unit.");
				return result;
			}
			QString text;
			text.reserve(bytes.size() / 2);
			bool pendingHigh = false;
			for (qsizetype offset = 0; offset < bytes.size(); offset += 2)
			{
				const char *at = bytes.data() + offset;
				const quint16 unit = bigEndian ? qFromBigEndian<quint16>(at) : qFromLittleEndian<quint16>(at);
				if (!unit)
					break;
				const bool high = unit >= 0xd800 && unit <= 0xdbff;
				const bool low = unit >= 0xdc00 && unit <= 0xdfff;
				if ((pendingHigh && !low) || (low && !pendingHigh))
				{
					result.valid = false;
					break;
				}
				pendingHigh = high;
				text.append(QChar(unit));
			}
			result.valid = result.valid && !pendingHigh;
			if (result.valid)
			{
				result.value = text;
				result.explanation = QStringLiteral("UTF-16 text ends at the first null code unit; original terminator and any padding remain in the encoded value.");
			}
			else
				result.explanation = QStringLiteral("UTF-16 has an unpaired surrogate; original bytes are retained.");
			return result;
		}

		Decoded indirect(QByteArrayView bytes)
		{
			if (bytes.size() < 16)
				return invalid(QStringLiteral("Indirect lacks the minimum 16 bytes needed to contain its type identifier; original bytes retained."));
			// These exact Avid/AAF prefixes are documented by libMXF mxf_avid.c.
			static const QByteArray stringBe = QByteArray::fromHex("420110020000000000060e2b3401040101");
			static const QByteArray stringLe = QByteArray::fromHex("4c0002100100000000060e2b3401040101");
			static const QByteArray intBe = QByteArray::fromHex("420101070000000000060e2b3401040101");
			static const QByteArray intLe = QByteArray::fromHex("4c0007010100000000060e2b3401040101");
			if (bytes.startsWith(stringBe) || bytes.startsWith(stringLe))
			{
				auto result = utf16(bytes.sliced(17), bytes[0] == 'B');
				result.explanation.prepend(QStringLiteral("Recorded Avid/AAF Indirect string type and byte order. "));
				return result;
			}
			if (bytes.startsWith(intBe) || bytes.startsWith(intLe))
			{
				if (bytes.size() != 21)
					return invalid(QStringLiteral("Avid/AAF Indirect Int32 requires exactly four value bytes after its 17-byte prefix."));
				const qint32 value = bytes[0] == 'B' ? qFromBigEndian<qint32>(bytes.data() + 17)
													 : qFromLittleEndian<qint32>(bytes.data() + 17);
				return {value, QStringLiteral("Recorded Avid/AAF Indirect Int32 type and byte order."), {}, true};
			}
			return {{}, QStringLiteral("Indirect type is not one of the verified Avid string/Int32 encodings; original bytes retained without a guessed value."), {}, true};
		}

		Decoded decode(Type type, QByteArrayView bytes, const Cancellation &cancellation)
		{
			if (cancellation.cancelled())
				return {{}, QStringLiteral("Typed decoding was cancelled; source bytes remain retained."), {}, true};
			if (type == Type::Indirect)
				return indirect(bytes);
			if (type == Type::Utf16String)
				return utf16(bytes, true);
			if (type == Type::Utf16Stringarray)
			{
				if (bytes.size() % 2)
					return {{}, QStringLiteral("UTF-16 string array has an incomplete code unit."), TextEncoding::Utf16BE, false};
				QVariantList strings;
				qsizetype start = 0;
				for (qsizetype offset = 0; offset < bytes.size(); offset += 2)
				{
					if (cancellation.cancelled())
						return {};
					if (bytes[offset] || bytes[offset + 1])
						continue;
					const auto value = utf16(bytes.sliced(start, offset + 2 - start), true);
					if (!value.valid)
						return value;
					strings.append(value.value);
					start = offset + 2;
				}
				if (start != bytes.size())
					return {{}, QStringLiteral("Avid UTF-16 string-array member lacks its null terminator."), TextEncoding::Utf16BE, false};
				return {strings, QStringLiteral("Concatenated null-terminated UTF-16 strings, as recorded by Avid's metadictionary; no array-count prefix."), TextEncoding::Utf16BE, true};
			}
			if (type == Type::Utf8String || type == Type::Iso7String)
			{
				const qsizetype zero = bytes.indexOf('\0');
				const auto content = zero < 0 ? bytes : bytes.first(zero);
				if (type == Type::Iso7String)
				{
					for (char byte : content)
						if (static_cast<unsigned char>(byte) > 127)
							return {{}, QStringLiteral("ISO7 string contains a non-seven-bit byte."), TextEncoding::Ascii, false};
					return {QString::fromLatin1(content.data(), content.size()), {}, TextEncoding::Ascii, true};
				}
				QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
				const QString text = decoder(content);
				return {decoder.hasError() ? QVariant{} : QVariant(text),
						decoder.hasError() ? QStringLiteral("Recorded UTF-8 is invalid; original bytes retained.") : QString{},
						TextEncoding::Utf8, !decoder.hasError()};
			}
			if (type == Type::Raw || type == Type::Stream || type == Type::Datavalue ||
				type == Type::Identifier || type == Type::Opaque || type == Type::J2KExtendedCapabilities)
				return {{}, QStringLiteral("The schema names this binary value; its internal semantics remain uninterpreted and its original bytes are retained."), {}, true};

			const auto &def = definition(type);
			const qsizetype expected = fixedSize(type);
			if (type == Type::Productversion && bytes.size() == 9)
			{
				QVariantMap fields;
				for (unsigned i = 0; i < 4; ++i)
					fields.insert(QString::fromLatin1(Schema::members[def.firstMember + i].name),
								  qFromBigEndian<quint16>(bytes.data() + i * 2));
				fields.insert(QStringLiteral("Release"), static_cast<quint8>(bytes[8]));
				return {fields, QStringLiteral("Recorded nine-byte AAF/Avid ProductVersion variant: Release is UInt8; the standard MXF form uses UInt16."), {}, true};
			}
			if (expected && bytes.size() != expected)
				return invalid(QStringLiteral("%1 requires %2 bytes; %3 are recorded.")
								   .arg(typeName(type))
								   .arg(expected)
								   .arg(bytes.size()));

			switch (def.shape)
			{
			case Schema::Shape::Basic:
				switch (type)
				{
				case Type::Int8:
					return {static_cast<qint8>(bytes[0]), {}, {}, true};
				case Type::Int16:
					return {qFromBigEndian<qint16>(bytes.data()), {}, {}, true};
				case Type::Int32:
					return {qFromBigEndian<qint32>(bytes.data()), {}, {}, true};
				case Type::Int64:
					return {qFromBigEndian<qint64>(bytes.data()), {}, {}, true};
				case Type::Uint8:
					return {static_cast<quint8>(bytes[0]), {}, {}, true};
				case Type::Uint16:
					return {qFromBigEndian<quint16>(bytes.data()), {}, {}, true};
				case Type::Uint32:
					return {qFromBigEndian<quint32>(bytes.data()), {}, {}, true};
				case Type::Uint64:
					return {qFromBigEndian<quint64>(bytes.data()), {}, {}, true};
				default:
					return {};
				}
			case Schema::Shape::Interpreted:
				if (def.size)
					return {QByteArray(bytes.data(), bytes.size()), {}, {}, true};
				return decode(def.base, bytes, cancellation);
			case Schema::Shape::Compound:
			{
				QVariantMap fields;
				qsizetype offset = 0;
				for (unsigned i = 0; i < def.memberCount; ++i)
				{
					const auto &member = Schema::members[def.firstMember + i];
					const qsizetype size = fixedSize(member.type);
					if (!size)
						return {{}, QStringLiteral("Variable-length compound members are retained without guessed boundaries."), {}, true};
					const auto value = decode(member.type, bytes.sliced(offset, size), cancellation);
					if (!value.valid)
						return value;
					fields.insert(QString::fromLatin1(member.name), value.value);
					offset += size;
				}
				return {fields, {}, {}, true};
			}
			case Schema::Shape::Array:
			{
				const qsizetype elementSize = fixedSize(def.base);
				if (!elementSize)
					return {{}, QStringLiteral("Array element boundaries are not established; original bytes retained."), {}, true};
				quint32 count = def.size;
				qsizetype start = 0;
				QString explanation;
				if (!count)
				{
					if (bytes.size() < 8)
						return invalid(QStringLiteral("MXF array/batch lacks its eight-byte count and element-size header."));
					count = qFromBigEndian<quint32>(bytes.data());
					const quint32 recordedSize = qFromBigEndian<quint32>(bytes.data() + 4);
					if ((count && recordedSize != elementSize) ||
						quint64(count) * recordedSize != quint64(bytes.size() - 8))
						return invalid(QStringLiteral("MXF array/batch count or element size disagrees with its retained value and registered type."));
					start = 8;
					if (!count && recordedSize != elementSize)
						explanation = QStringLiteral("Empty array/batch records element size %1 rather than the registered %2; no elements are present. Original header bytes are retained.").arg(recordedSize).arg(elementSize);
				}
				QVariantList values;
				values.reserve(count);
				for (quint32 i = 0; i < count; ++i)
				{
					if (cancellation.cancelled())
						return {};
					const auto value = decode(def.base, bytes.sliced(start + qsizetype(i) * elementSize, elementSize), cancellation);
					if (!value.valid)
						return value;
					values.append(value.value);
				}
				return {values, explanation, {}, true};
			}
			}
			return {};
		}

		PropertyLocator elementLocator(const PropertyLocator &property, qsizetype start, qsizetype length)
		{
			PropertyLocator result = property;
			result.ranges.clear();
			qint64 logical = 0;
			for (const auto &range : property.ranges)
			{
				const qint64 first = qMax<qint64>(start, logical);
				const qint64 last = qMin<qint64>(start + length, logical + range.length);
				if (last > first)
					result.ranges.append({range.offset + first - logical, last - first});
				logical += range.length;
			}
			return result;
		}

		void qualify(ParsedSource &source, const QString &reason)
		{
			if (source.outcome == Outcome::Complete || source.outcome == Outcome::NotRead)
				source.outcome = Outcome::Incomplete;
			source.diagnostics.append(reason);
		}

		class ObjectInterpreter
		{
		public:
			ObjectInterpreter(ParsedSource &source, const Cancellation &cancellation)
				: m_source(source), m_cancellation(cancellation) {}

			void run()
			{
				for (auto &object : m_source.objects)
				{
					if (cancelled())
						return;
					interpret(object);
				}
				for (const auto &object : std::as_const(m_source.objects))
				{
					if (cancelled())
						return;
					addReferences(object);
				}
			}

		private:
			bool cancelled()
			{
				if (!m_cancellation.cancelled())
					return false;
				m_source.outcome = Outcome::Cancelled;
				m_source.diagnostics.append(QStringLiteral("MXF property interpretation was cancelled; retained source bytes and earlier interpretations remain available."));
				return true;
			}

			const Schema::SetDefinition *setDefinition(const AvidObject &object) const
			{
				return object.mxf ? catalogue().sets.value(registryKey(object.mxf->key, true)) : nullptr;
			}

			const Schema::ItemDefinition *itemDefinition(const RawProperty &property, const AvidObject &object) const
			{
				const auto *item = property.mxf && property.mxf->mappedAuid.size() == 16
									   ? catalogue().item(property.mxf->mappedAuid, setDefinition(object))
									   : nullptr;
				if (avidRoot(object) && item &&
					!(std::strcmp(item->owner, "InterchangeObject") == 0 && std::strcmp(item->name, "InstanceUID") == 0))
					return nullptr;
				return item;
			}

			void interpret(AvidObject &object)
			{
				if (!object.mxf)
					return;
				const auto *set = setDefinition(object);
				if (set)
				{
					auto context = QSharedPointer<MxfSetContext>::create(*object.mxf);
					context->name = catalogue().setNames.value(set);
					object.mxf = context;
					if (catalogue().inherits(set, "GenericDescriptor") || catalogue().inherits(set, "SubDescriptor"))
						object.role = AvidObject::Role::Descriptor;
					else if (catalogue().inherits(set, "GenericTrack"))
						object.role = AvidObject::Role::Track;
					else if (catalogue().inherits(set, "StructuralComponent"))
						object.role = AvidObject::Role::Component;
				}
				else if (avidRoot(object))
				{
					auto context = QSharedPointer<MxfSetContext>::create(*object.mxf);
					context->name = QStringLiteral("AvidMetadataRoot");
					object.mxf = context;
				}
				unsigned identityCount = 0;
				QVector<QByteArray> identities;
				for (auto &property : object.properties)
				{
					if (cancelled())
						return;
					const auto *item = itemDefinition(property, object);
					if (!item)
					{
						if (avidRoot(object))
							property.interpretation.append(QStringLiteral(" Avid private-root fields use their own native layout; mapping and bytes are retained without applying another metadata set's type definition."));
						continue;
					}
					property.locator.name = catalogue().propertyNames.value(item);
					auto context = QSharedPointer<MxfPropertyContext>::create(*property.mxf);
					context->typeName = typeName(item->type);
					property.mxf = context;
					const bool isIdentity = property.locator.name == QLatin1String("InterchangeObject.InstanceUID");
					if (isIdentity)
						++identityCount;
					if (property.state != PropertyReadState::Present || !property.bytesRetained)
						continue;
					const auto decoded = decode(item->type, property.encoding, m_cancellation);
					property.decoded = decoded.value;
					property.textEncoding = decoded.text;
					if (decoded.text)
						property.textEncodingBasis = EvidenceBasis::Recorded;
					if (!decoded.explanation.isEmpty())
					{
						if (property.interpretation.isEmpty())
							property.interpretation = decoded.explanation;
						else
						{
							property.interpretation.append(QLatin1Char(' '));
							property.interpretation.append(decoded.explanation);
						}
					}
					if (!decoded.valid)
					{
						property.state = PropertyReadState::Unreadable;
						qualify(m_source, QStringLiteral("%1 on object %2: %3")
											  .arg(property.locator.name)
											  .arg(object.handle)
											  .arg(decoded.explanation));
					}
					else if (isIdentity && property.encoding.size() == 16)
						identities.append(property.encoding);
				}
				if (identityCount == 1 && identities.size() == 1)
				{
					const auto &identity = identities.first();
					object.recordedIdentity = identity;
					object.identityEncoding = QStringLiteral("MXF InstanceUID: 16 original UUID bytes; no byte-order normalization");
					m_identities[object.mxf->partitionOffset][identity].append(object.handle);
					m_setsByHandle.insert(object.handle, set);
				}
				else if (identityCount > 1)
				{
					// Ambiguous objects still make their candidate IDs ambiguous for
					// other references; excluding them would create false uniqueness.
					for (const auto &identity : identities)
						m_identities[object.mxf->partitionOffset][identity].append(0);
					m_source.diagnostics.append(QStringLiteral("MXF object %1 has repeated InstanceUID properties; no single object identity was selected.").arg(object.handle));
				}
			}

			ObjectHandle targetFor(const AvidObject &object, const QByteArray &identity) const
			{
				if (object.mxf->partitionOffset < 0 || identity == QByteArray(16, '\0'))
					return 0;
				const auto partition = m_identities.constFind(object.mxf->partitionOffset);
				if (partition == m_identities.cend())
					return 0;
				const auto found = partition->constFind(identity);
				return found != partition->cend() && found->size() == 1 ? found->first() : 0;
			}

			void reference(const AvidObject &object, const RawProperty &property, const QByteArray &identity,
						   Type type, qsizetype offset)
			{
				Relationship relationship;
				relationship.origin = object.handle;
				relationship.target = type == Type::Strongref ? targetFor(object, identity) : 0;
				relationship.locator = elementLocator(property.locator, offset, 16);
				relationship.recordedReference = identity;
				relationship.referenceEncoding = type == Type::Weakref
													 ? QStringLiteral("MXF WeakRef: 16 original reference bytes; no byte-order normalization")
													 : QStringLiteral("MXF StrongRef: 16 original reference bytes; no byte-order normalization");
				relationship.explanation = relationship.target
											   ? QStringLiteral("A unique InstanceUID target was found in this partition's metadata copy.")
											   : QStringLiteral("No unique InstanceUID target was established in this partition's metadata copy; the recorded reference remains unresolved.");
				if (type == Type::Weakref)
					relationship.explanation = QStringLiteral("This weak reference remains unresolved: its property-specific target identifier is not established. Weak references may use other unique properties or external definitions; matching InstanceUID bytes alone does not prove a target.");
				m_source.relationships.append(std::move(relationship));
			}

			void addReferences(const AvidObject &object)
			{
				if (!object.mxf)
					return;
				for (const auto &property : object.properties)
				{
					if (cancelled())
						return;
					const auto *item = itemDefinition(property, object);
					if (!item || property.state != PropertyReadState::Present || !property.decoded.isValid())
						continue;
					if (item->type == Type::Strongref || item->type == Type::Weakref)
						reference(object, property, property.encoding, item->type, 0);
					else if (item->type == Type::Strongrefarray || item->type == Type::Strongrefbatch ||
							 item->type == Type::Weakrefarray || item->type == Type::Weakrefbatch)
					{
						for (qsizetype offset = 8; offset < property.encoding.size(); offset += 16)
						{
							if (cancelled())
								return;
							reference(object, property, property.encoding.sliced(offset, 16), definition(item->type).base, offset);
						}
					}
					else if (property.locator.name == QLatin1String("StructuralComponent.DataDefinition"))
					{
						const ObjectHandle target = targetFor(object, property.encoding);
						if (target && catalogue().inherits(m_setsByHandle.value(target), "DataDefinition"))
						{
							reference(object, property, property.encoding, Type::Weakref, 0);
							auto &relationship = m_source.relationships.last();
							relationship.target = target;
							relationship.basis = EvidenceBasis::Derived;
							relationship.explanation = QStringLiteral("Verified Avid DataDefinition alternative: these bytes uniquely match a dictionary DataDefinition in this partition. The original registered UL-typed observation is retained separately.");
						}
					}
				}
			}

			ParsedSource &m_source;
			const Cancellation &m_cancellation;
			QHash<qint64, QHash<QByteArray, QVector<ObjectHandle>>> m_identities;
			QHash<ObjectHandle, const Schema::SetDefinition *> m_setsByHandle;
		};
	}

	void Detail::interpretMxfObjects(ParsedSource &source, const Cancellation &cancellation)
	{
		ObjectInterpreter(source, cancellation).run();
	}
}
