#include "mdbparser.h"
#include "avidtext.h"

#include <QByteArrayView>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringDecoder>
#include <QtEndian>
#include <algorithm>
#include <cstring>
#include <limits>
#include <numeric>
#include <type_traits>

namespace Bella
{
	namespace
	{
		enum class Issue
		{
			Missing,
			Unreadable,
			Conflict
		};

		template <typename T>
		using Read = std::variant<Issue, T>;

		template <typename T>
		const T *valueOf(const Read<T> &read) { return std::get_if<T>(&read); }
		template <typename T>
		bool missing(const Read<T> &read)
		{
			const auto *issue = std::get_if<Issue>(&read);
			return issue && *issue == Issue::Missing;
		}

		template <typename T>
		bool equalValue(const T &a, const T &b) { return a == b; }
		bool equalValue(const MdbRate &a, const MdbRate &b)
		{
			return a.words.numerator == b.words.numerator && a.words.denominator == b.words.denominator && a.encoding == b.encoding;
		}

		quint16 littleHalf(const char *p) { return qFromLittleEndian<quint16>(p); }
		quint32 littleWord(const char *p) { return qFromLittleEndian<quint32>(p); }
		template <typename T>
		T number(const char *p, bool big)
		{
			return big ? qFromBigEndian<T>(p) : qFromLittleEndian<T>(p);
		}

		bool named(QByteArrayView name, std::initializer_list<const char *> names)
		{
			return std::any_of(names.begin(), names.end(), [name](const char *candidate)
							   { return name == candidate; });
		}

		QString roman(QByteArrayView bytes)
		{
			QString text;
			text.reserve(bytes.size());
			for (const auto byte : bytes)
			{
				const auto code = quint8(byte);
				text += QChar(code < 128 ? code : AvidText::kMacRomanHigh[code - 128]);
			}
			return text;
		}

		Read<QString> textValue(QByteArrayView bytes, bool utf8)
		{
			const auto end = bytes.indexOf('\0');
			if (end >= 0)
				bytes = bytes.first(end);
			if (!utf8)
				return roman(bytes);
			QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
			const QString text = decoder.decode(bytes);
			return decoder.hasError() ? Read<QString>(Issue::Unreadable) : Read<QString>(text);
		}

		struct Span
		{
			qsizetype offset = 0, length = 0;
		};
		struct Value
		{
			quint32 object = 0, property = 0, type = 0;
			quint16 generation = 0;
			qsizetype firstSpan = 0, spanCount = 0, size = 0;
			bool readable = true;
		};

		// Only TOC locations and dictionaries survive the foundation pass. Payloads
		// stay in the one capture; joined continuations are materialized on demand.
		class Image
		{
		public:
			explicit Image(QByteArrayView bytes, QStringList &notes) : m_bytes(bytes), m_notes(notes) {}

			MdbReadResult establish()
			{
				constexpr qsizetype labelSize = 24, entrySize = 24;
				if (m_bytes.size() < labelSize)
					return fail(MdbReadResult::Malformed, QStringLiteral("MDB has no complete Bento label."));
				const auto labelOffset = m_bytes.size() - labelSize;
				const auto *label = m_bytes.data() + labelOffset;
				if (QByteArrayView(label, 8) != QByteArray::fromHex("a4434da5486472d7"))
					return fail(MdbReadResult::Malformed, QStringLiteral("MDB has no Bento signature at its final label."));
				if (littleHalf(label + 12) != 1 || littleHalf(label + 14) != 0)
					return fail(MdbReadResult::Unsupported, QStringLiteral("MDB requires the supported Bento 1.0 layout."));
				if (littleHalf(label + 8) & ~quint16(0x0101))
					return fail(MdbReadResult::Unsupported, QStringLiteral("MDB Bento label has unsupported flags."));
				m_tocOffset = littleWord(label + 16);
				const qsizetype tocSize = littleWord(label + 20);
				if (m_tocOffset > labelOffset || tocSize > labelOffset - m_tocOffset || tocSize % entrySize)
					return fail(MdbReadResult::Malformed, QStringLiteral("MDB table of contents is incomplete or outside its label."));
				m_values.reserve(tocSize / entrySize);
				m_spans.reserve(tocSize / entrySize);
				bool continuing = false;
				for (qsizetype position = 0; position < tocSize; position += entrySize)
				{
					const auto *p = m_bytes.data() + m_tocOffset + position;
					const quint32 object = littleWord(p), property = littleWord(p + 4), type = littleWord(p + 8);
					const quint16 generation = littleHalf(p + 20), flags = littleHalf(p + 22);
					if ((flags & ~quint16(3)) || ((property | type) & 0xff000000u))
						return fail(MdbReadResult::Unsupported, QStringLiteral("MDB has an unsupported Bento TOC extension at byte %1.").arg(m_tocOffset + position));
					if ((object == 1 && property >= 8 && property <= 10) || property == 29 || type == 30)
						return fail(MdbReadResult::Unsupported, QStringLiteral("MDB contains Bento update instructions requiring an unsupported update resolver."));
					if (continuing)
					{
						auto &previous = m_values.last();
						if (previous.object != object || previous.property != property || previous.type != type || previous.generation != generation)
						{
							previous.readable = false;
							continuing = false;
						}
					}
					if (!continuing)
					{
						m_byObject[object].append(m_values.size());
						m_values.append({object, property, type, generation, m_spans.size(), 0, 0, true});
					}
					auto &value = m_values.last();
					const qsizetype length = littleWord(p + 16);
					const bool immediate = flags & 1;
					const qsizetype offset = immediate ? m_tocOffset + position + 12 : littleWord(p + 12);
					const bool internal = object == 1 && property >= 1 && property <= 6;
					const auto limit = immediate ? m_bytes.size() : internal ? labelOffset
																			 : m_tocOffset;
					if ((immediate && length > 4) || offset > limit || length > limit - offset ||
						length > std::numeric_limits<qsizetype>::max() - value.size)
						value.readable = false;
					else
						value.size += length;
					m_spans.append({offset, length});
					++value.spanCount;
					continuing = flags & 2;
				}
				if (continuing)
					m_values.last().readable = false;
				for (qsizetype i = 0; i < m_values.size(); ++i)
				{
					const auto &value = m_values[i];
					if ((value.property != 23 && value.property != 24) || value.type != 21)
						continue;
					const auto bytes = payload(i);
					if (!bytes || bytes->isEmpty() || bytes->back() != '\0' || bytes->indexOf('\0') != bytes->size() - 1)
					{
						m_badDictionary.insert(value.object);
						continue;
					}
					const QByteArray name(bytes->data(), bytes->size() - 1);
					if (value.property == 24 && !m_propertyIds[name].contains(value.object))
						m_propertyIds[name].append(value.object);
					auto &dictionary = value.property == 24 ? m_properties : m_types;
					if (dictionary.contains(value.object) && dictionary.value(value.object) != name)
						m_badDictionary.insert(value.object);
					else
						dictionary.insert(value.object, name);
				}
				const auto cls = tag(1);
				if (missing(cls))
				{
					const auto modern = field<QByteArrayView>(1, "OMFI:OOBJ:ObjClass", [](QByteArrayView bytes, QByteArrayView type) -> Read<QByteArrayView>
															  { return type == "omfi:ObjectTag" && bytes.size() == 4 ? Read<QByteArrayView>(bytes) : Read<QByteArrayView>(Issue::Unreadable); });
					if (valueOf(modern) && *valueOf(modern) == "HEAD")
						return fail(MdbReadResult::Unsupported, QStringLiteral("MDB uses the unsupported OMF 2 object-class layout."));
				}
				if (!valueOf(cls) || *valueOf(cls) != QByteArrayView("HEAD"))
					return fail(MdbReadResult::Malformed, QStringLiteral("MDB cannot establish its HEAD object and class dictionary."));
				const auto version = field<QByteArrayView>(1, "OMFI:Version", [](QByteArrayView bytes, QByteArrayView type) -> Read<QByteArrayView>
														   {
					if (type != "omfi:VersionType" || bytes.size() != 2)
						return Issue::Unreadable;
					return bytes; });
				if (!valueOf(version))
					return fail(MdbReadResult::Malformed, QStringLiteral("MDB HEAD version is absent, unreadable or conflicting."));
				if (*valueOf(version) != QByteArrayView("\0\1", 2) && *valueOf(version) != QByteArrayView("\1\0", 2))
					return fail(MdbReadResult::Unsupported, QStringLiteral("MDB requires the legacy OMF 1 record layout."));
				const auto order = byteOrder(1);
				if (!valueOf(order))
					return fail(MdbReadResult::Malformed, QStringLiteral("MDB HEAD byte order is absent or conflicting."));
				m_metadataBig = *valueOf(order);
				const auto spine = references(1, "OMFI:ObjectSpine");
				if (!valueOf(spine))
					return fail(MdbReadResult::Malformed, QStringLiteral("MDB ObjectSpine does not establish a complete active catalogue."));
				for (const auto object : *valueOf(spine))
				{
					if (!object || !m_byObject.contains(object))
						return fail(MdbReadResult::Malformed, QStringLiteral("MDB ObjectSpine refers to an unavailable object."));
					const auto kind = tag(object);
					if (!valueOf(kind))
						return fail(MdbReadResult::Malformed, QStringLiteral("MDB ObjectSpine has an unresolved object class."));
					if (*valueOf(kind) == "MOBJ")
					{
						if (!m_activeSet.contains(object))
							m_active.append(object);
						m_activeSet.insert(object);
					}
					else if (named(*valueOf(kind), {"MDAT", "IDAT", "JPEG", "TIFF", "WAVE", "AIFC"}))
						m_media.append(object);
					else if (named(*valueOf(kind), {"HEAD", "TRAK", "TRKG", "SCLP", "SEQU", "FILL", "TCCP", "RSET", "PVOL", "NEST", "SLCT", "MGRP", "ERAT", "ATTR", "ATTB", "CDCI", "RGBA", "MPGI", "PCMA", "AIFD", "WAVD", "MDES", "MDTP", "MDFM", "JPED", "TIFD", "MPGA"}))
						return fail(MdbReadResult::Malformed, QStringLiteral("MDB ObjectSpine assigns an invalid class to an active catalogue owner."));
					else
					{
						m_referenceClassesKnown = false;
						problem(object, "OMFI:ObjID", QStringLiteral("Unknown active media class; file-reference absence cannot be proved."));
					}
				}
				qualifyIndex("OMFI:SourceMobs", true);
				qualifyIndex("OMFI:CompositionMobs", false);
				const auto mediaIndex = references(1, "OMFI:MediaData");
				if (!missing(mediaIndex))
				{
					QSet<quint32> expected(m_media.cbegin(), m_media.cend()), present;
					if (valueOf(mediaIndex))
						for (const auto object : *valueOf(mediaIndex))
							present.insert(object);
					if (!valueOf(mediaIndex) || expected != present)
					{
						m_referenceClassesKnown = false;
						problem(1, "OMFI:MediaData", QStringLiteral("Optional media-data index does not agree with the active catalogue; reference coverage is incomplete."));
					}
				}
				return MdbReadResult::Completed;
			}

			template <typename T, typename Decoder>
			Read<T> field(quint32 object, const char *name, Decoder decode, bool append = false) const
			{
				const auto ids = m_propertyIds.constFind(name);
				if (ids == m_propertyIds.cend())
					return namesKnown(object) ? Issue::Missing : Issue::Unreadable;
				const auto rows = m_byObject.constFind(object);
				if (rows == m_byObject.cend())
					return Issue::Missing;
				std::optional<T> answer;
				bool bad = false;
				for (const auto index : *rows)
				{
					const auto &value = m_values[index];
					if (!ids->contains(value.property))
						continue;
					const auto bytes = payload(index);
					if (!bytes || m_badDictionary.contains(value.property) || m_badDictionary.contains(value.type) || !m_types.contains(value.type))
					{
						bad = true;
						continue;
					}
					const auto decoded = decode(*bytes, QByteArrayView(m_types[value.type]));
					if (const auto *candidate = valueOf(decoded))
					{
						if constexpr (std::is_same_v<T, QVector<quint32>>)
							if (append && answer)
							{
								*answer += *candidate;
								continue;
							}
						if (answer && !equalValue(*answer, *candidate))
							return Issue::Conflict;
						answer = *candidate;
					}
					else
						bad = true;
				}
				if (bad)
					return Issue::Unreadable;
				return answer ? Read<T>(*answer) : Read<T>(namesKnown(object) ? Issue::Missing : Issue::Unreadable);
			}

			bool namesKnown(quint32 object) const
			{
				const auto rows = m_byObject.constFind(object);
				if (rows == m_byObject.cend())
					return false;
				for (const auto index : *rows)
				{
					const auto property = m_values[index].property;
					if (property > 31 && (!m_properties.contains(property) || m_badDictionary.contains(property)))
						return false;
				}
				return true;
			}

			Read<QByteArrayView> tag(quint32 object) const
			{
				return field<QByteArrayView>(object, "OMFI:ObjID", [](QByteArrayView bytes, QByteArrayView type) -> Read<QByteArrayView>
											 { return bytes.size() == 4 && named(type, {"omfi:ObjectTag", "omfi:ClassID"}) ? Read<QByteArrayView>(bytes) : Read<QByteArrayView>(Issue::Unreadable); });
			}

			Read<bool> byteOrder(quint32 object) const
			{
				return field<bool>(object, "OMFI:ByteOrder", [](QByteArrayView bytes, QByteArrayView type) -> Read<bool>
								   {
					if (!named(type, {"omfi:Short", "omfi:Int16"}) || (bytes != "II" && bytes != "MM"))
						return Issue::Unreadable;
					return bytes == "MM"; });
			}

			std::optional<bool> bigEndian(quint32 object) const
			{
				const auto order = byteOrder(object);
				if (const auto *big = valueOf(order))
					return *big == m_metadataBig ? std::optional<bool>(*big) : std::nullopt;
				return missing(order) ? std::optional<bool>(m_metadataBig) : std::nullopt;
			}

			Read<qint64> integer(quint32 object, const char *name) const
			{
				const auto big = bigEndian(object);
				return field<qint64>(object, name, [big, name](QByteArrayView bytes, QByteArrayView type) -> Read<qint64>
									 {
					if (!big)
						return Issue::Unreadable;
					// A recognized scalar type elsewhere does not make it valid
					// for this property. Preserve the established OMF typed widths.
					if (QByteArrayView(name) == "OMFI:MDFL:Length" && (bytes.size() != 4 || type != "omfi:Long"))
						return Issue::Unreadable;
					if (QByteArrayView(name) == "OMFI:MDAU:BitsPerSample" &&
						(bytes.size() != 2 || !named(type, {"omfi:Ushort", "omfi:UShort", "omfi:UInt16"})))
						return Issue::Unreadable;
					if (QByteArrayView(name) == "OMFI:CPNT:TrackKind" && (bytes.size() != 2 || type != "omfi:TrackType"))
						return Issue::Unreadable;
					if (QByteArrayView(name) == "OMFI:ATTB:Kind" && (bytes.size() != 2 || type != "omfi:AttrKind"))
						return Issue::Unreadable;
					if (QByteArrayView(name) == "OMFI:DIDD:FrameLayout" && (bytes.size() != 2 || type != "omfi:LayoutType"))
						return Issue::Unreadable;
					if (named(name, {"OMFI:TRAK:LabelNumber", "OMFI:SCLP:SourceTrack", "OMFI:TCCP:FPS"}) &&
						(bytes.size() != 2 || !named(type, {"omfi:Short", "omfi:Int16"})))
						return Issue::Unreadable;
					if (QByteArrayView(name) == "OMFI:MOBJ:UsageCode" && (bytes.size() != 4 || type != "omfi:UsageCodeType"))
						return Issue::Unreadable;
					if (named(name, {"OMFI:CDCI:HorizontalSubsampling", "OMFI:CDCI:VerticalSubsampling"}) &&
						(bytes.size() != 4 || !named(type, {"omfi:Ulong", "omfi:ULong", "omfi:UInt32", "omfi:Long", "omfi:Int32"})))
						return Issue::Unreadable;
					if (named(name, {"OMFI:CDCI:ComponentWidth", "OMFI:TCCP:Flags", "OMFI:TCCP:StartTC", "OMFI:SCLP:SourcePosition",
						"OMFI:ATTB:BobSize", "OMFI:DIDD:DIDResolutionID",
						"OMFI:DIDD:StoredWidth", "OMFI:DIDD:StoredHeight", "OMFI:DIDD:DisplayWidth", "OMFI:DIDD:DisplayHeight",
						"OMFI:DIDD:DisplayXOffset", "OMFI:DIDD:DisplayYOffset", "OMFI:DIDD:SampledWidth", "OMFI:DIDD:SampledHeight",
						"OMFI:DIDD:SampledXOffset", "OMFI:DIDD:SampledYOffset"}) &&
						(bytes.size() != 4 || !named(type, {"omfi:Long", "omfi:Int32"})))
						return Issue::Unreadable;
					if (bytes.size() == 2 && named(type, {"omfi:Short", "omfi:Int16", "omfi:TrackType", "omfi:UsageCodeType", "omfi:AttrKind", "omfi:LayoutType"}))
						return number<qint16>(bytes.data(), *big);
					if (bytes.size() == 2 && named(type, {"omfi:UInt16", "omfi:Ushort", "omfi:UShort"}))
						return number<quint16>(bytes.data(), *big);
					if (bytes.size() == 4 && named(type, {"omfi:Long", "omfi:Int32", "omfi:Length32", "omfi:Position32", "omfi:UsageCodeType"}))
						return number<qint32>(bytes.data(), *big);
					if (bytes.size() == 4 && named(type, {"omfi:UInt32", "omfi:Ulong", "omfi:ULong"}))
						return number<quint32>(bytes.data(), *big);
					if (bytes.size() == 8 && named(type, {"omfi:Int64", "omfi:Length64", "omfi:Position64"}))
						return number<qint64>(bytes.data(), *big);
					return Issue::Unreadable; });
			}

			Read<MdbRate> rate(quint32 object, const char *name) const
			{
				const auto big = bigEndian(object);
				return field<MdbRate>(object, name, [big](QByteArrayView bytes, QByteArrayView type) -> Read<MdbRate>
									  {
					if (!big || bytes.size() != 8 || !named(type, {"omfi:Rational", "omfi:ExactEditRate"}))
						return Issue::Unreadable;
					const MdbRateWords words{number<qint32>(bytes.data(), *big), number<qint32>(bytes.data() + 4, *big)};
					if (words.numerator <= 0 || words.denominator <= 0)
						return Issue::Unreadable;
					return MdbRate{words, type == "omfi:ExactEditRate" ? MdbRateEncoding::ExactEditRate : MdbRateEncoding::Rational}; });
			}

			Read<MobId> identity(quint32 object, const char *name = "OMFI:MOBJ:MobID") const
			{
				const auto big = bigEndian(object);
				return field<MobId>(object, name, [big](QByteArrayView bytes, QByteArrayView type) -> Read<MobId>
									{
					if (!big || type != "omfi:UID" || (bytes.size() != 12 && bytes.size() != 32))
						return Issue::Unreadable;
					const auto id = normalizeMobId(bytes, bytes.size() == 12 ? MobIdLayout::OmfUid12 : MobIdLayout::Aaf32,
												  *big ? ByteOrder::BigEndian : ByteOrder::LittleEndian);
					return std::holds_alternative<MobId>(id) ? Read<MobId>(std::get<MobId>(id)) : Read<MobId>(Issue::Unreadable); });
			}

			Read<QString> text(quint32 object, const char *name, bool utf8 = false) const
			{
				return field<QString>(object, name, [utf8](QByteArrayView bytes, QByteArrayView type) -> Read<QString>
									  { return named(type, {"omfi:String", "omfi:VarLenBytes"}) ? textValue(bytes, utf8) : Read<QString>(Issue::Unreadable); });
			}

			Read<QVector<quint32>> references(quint32 object, const char *name) const
			{
				const auto big = bigEndian(object);
				const bool index = named(name, {"OMFI:SourceMobs", "OMFI:CompositionMobs", "OMFI:MediaData"});
				const bool array = named(name, {"OMFI:ObjectSpine", "OMFI:TRKG:Tracks", "OMFI:SEQU:Sequence", "OMFI:SEQU:Components",
												"OMFI:ATTR:AttrRefs", "OMFI:NEST:Slots", "OMFI:SLCT:Alternates", "OMFI:MGRP:Choices"});
				return field<QVector<quint32>>(object, name, [big, index, array](QByteArrayView bytes, QByteArrayView type) -> Read<QVector<quint32>>
											   {
					if (!big)
						return Issue::Unreadable;
					if (type != (index ? "omfi:MobIndex" : array ? "omfi:ObjRefArray" : "omfi:ObjRef"))
						return Issue::Unreadable;
					qsizetype count = 1, offset = 0, stride = 8, prefix = 0;
					if (type == "omfi:ObjRef")
					{
						if (bytes.size() != 8)
							return Issue::Unreadable;
					}
					else if (named(type, {"omfi:ObjRefArray", "omfi:MobIndex"}))
					{
						if (bytes.size() < 2)
							return Issue::Unreadable;
						offset = 2;
						if (type == "omfi:MobIndex")
						{
							stride = 20;
							prefix = 12;
						}
						if ((bytes.size() - offset) % stride)
							return Issue::Unreadable;
						// OMF establishes slots from the complete value extent. The
						// prefix may be 0xffff; it does not truncate valid slots.
						count = (bytes.size() - offset) / stride;
					}
					else
						return Issue::Unreadable;
					QVector<quint32> targets;
					targets.reserve(count);
					for (qsizetype i = 0; i < count; ++i)
						targets.append(littleWord(bytes.data() + offset + i * stride + prefix));
					return targets; }, index || array);
			}

			Read<QByteArrayView> bytes(quint32 object, const char *name, std::initializer_list<const char *> types) const
			{
				return field<QByteArrayView>(object, name, [types](QByteArrayView bytes, QByteArrayView type) -> Read<QByteArrayView>
											 { return named(type, types) ? Read<QByteArrayView>(bytes) : Read<QByteArrayView>(Issue::Unreadable); });
			}

			bool exists(quint32 object) const { return m_byObject.contains(object); }
			const QVector<quint32> &activeMobs() const { return m_active; }
			bool referenceClassesKnown() const { return m_referenceClassesKnown; }
			bool disputed(quint32 object) const { return m_disputed.contains(object); }
			bool has(quint32 object, const char *name) const
			{
				return !missing(field<bool>(object, name, [](QByteArrayView, QByteArrayView) -> Read<bool>
											{ return true; }));
			}

			void problem(quint32 object, const char *name, const QString &reason) const
			{
				m_notes.append(QStringLiteral("Object %1, %2: %3").arg(object).arg(QString::fromLatin1(name), reason));
			}

		private:
			void qualifyIndex(const char *name, bool sources)
			{
				const auto index = references(1, name);
				if (missing(index))
					return; // Legacy ObjectSpine is sufficient when optional indexes are absent.
				QSet<quint32> expected, present;
				for (const auto object : m_active)
					if (has(object, "OMFI:MOBJ:PhysicalMedia") == sources)
						expected.insert(object);
				if (const auto *entries = valueOf(index))
					for (const auto object : *entries)
						present.insert(object);
				if (!valueOf(index))
					m_disputed += expected;
				else
					m_disputed += (expected - present) + (present - expected);
				if (!valueOf(index) || expected != present)
					problem(1, name, QStringLiteral("Optional catalogue index does not safely agree with ObjectSpine; affected ownership is withheld."));
				// A legacy index UID can qualify a legacy 12-byte owner UID.
				// It cannot stand in for the owner's complete 32-byte ID.
				const auto rows = m_byObject.constFind(1);
				const auto ids = m_propertyIds.constFind(name);
				if (rows == m_byObject.cend() || ids == m_propertyIds.cend())
					return;
				for (const auto row : *rows)
				{
					if (!ids->contains(m_values[row].property))
						continue;
					const auto bytes = payload(row);
					if (!bytes || bytes->size() < 2 || (bytes->size() - 2) % 20)
						continue;
					for (qsizetype offset = 2; offset < bytes->size(); offset += 20)
					{
						const auto object = littleWord(bytes->data() + offset + 12);
						const auto owner = this->bytes(object, "OMFI:MOBJ:MobID", {"omfi:UID"});
						if (!valueOf(owner) || valueOf(owner)->size() != 12)
							continue;
						const auto stored = normalizeMobId(bytes->sliced(offset, 12), MobIdLayout::OmfUid12,
														   m_metadataBig ? ByteOrder::BigEndian : ByteOrder::LittleEndian);
						const auto actual = identity(object);
						if (!valueOf(actual) || std::get<MobId>(stored) != *valueOf(actual))
						{
							m_disputed.insert(object);
							problem(object, name, QStringLiteral("Legacy index identity conflicts with its legacy owner identity."));
						}
					}
				}
			}

			MdbReadResult fail(MdbReadResult result, const QString &reason)
			{
				m_notes.append(reason);
				return result;
			}
			std::optional<QByteArrayView> payload(qsizetype index) const
			{
				const auto &value = m_values[index];
				if (!value.readable)
					return std::nullopt;
				if (value.spanCount == 1)
				{
					const auto &span = m_spans[value.firstSpan];
					return m_bytes.sliced(span.offset, span.length);
				}
				if (!m_joined.contains(index))
				{
					QByteArray bytes;
					bytes.reserve(value.size);
					for (qsizetype i = 0; i < value.spanCount; ++i)
					{
						const auto &span = m_spans[value.firstSpan + i];
						bytes.append(m_bytes.data() + span.offset, span.length);
					}
					m_joined.insert(index, std::move(bytes));
				}
				return QByteArrayView(m_joined[index]);
			}

			QByteArrayView m_bytes;
			QStringList &m_notes;
			qsizetype m_tocOffset = 0;
			bool m_metadataBig = false;
			bool m_referenceClassesKnown = true;
			QVector<Value> m_values;
			QVector<Span> m_spans;
			QHash<quint32, QVector<qsizetype>> m_byObject;
			QHash<quint32, QByteArray> m_properties, m_types;
			QHash<QByteArray, QVector<quint32>> m_propertyIds;
			QSet<quint32> m_badDictionary;
			QVector<quint32> m_active, m_media;
			QSet<quint32> m_activeSet;
			QSet<quint32> m_disputed;
			mutable QHash<qsizetype, QByteArray> m_joined;
		};

		template <typename T>
		std::optional<T> optionalValue(const Read<T> &read)
		{
			return valueOf(read) ? std::optional<T>(*valueOf(read)) : std::nullopt;
		}

		// These known format/unit meanings are independent of the six-tag Kind rule.
		bool audioDescriptor(QByteArrayView tag)
		{
			return named(tag, {"PCMA", "AIFD", "WAVD", "MPGA", "WAVE"});
		}

		bool pictureDescriptor(QByteArrayView tag)
		{
			return named(tag, {"CDCI", "RGBA", "MPGI", "JPED", "TIFD"});
		}

		bool mediaDescriptor(QByteArrayView tag)
		{
			return audioDescriptor(tag) || pictureDescriptor(tag);
		}

		Read<quint32> target(const Image &image, quint32 owner, const char *name)
		{
			const auto refs = image.references(owner, name);
			if (const auto *items = valueOf(refs))
			{
				if (items->size() != 1)
					return Issue::Unreadable;
				if (items->first() && !image.exists(items->first()))
					return Issue::Unreadable;
				return items->first();
			}
			return std::get<Issue>(refs);
		}

		struct Walk
		{
			QVector<quint32> objects;
			bool complete = true;
		};

		Walk components(const Image &image, quint32 root)
		{
			Walk result;
			QVector<QPair<quint32, bool>> pending{{root, false}};
			QHash<quint32, quint8> colour;
			while (!pending.isEmpty())
			{
				const auto item = pending.takeLast();
				if (!item.first)
					continue;
				if (item.second)
				{
					colour[item.first] = 2;
					continue;
				}
				if (colour.value(item.first) == 1)
				{
					result.complete = false;
					image.problem(item.first, "component graph", QStringLiteral("Component cycle leaves relationship coverage incomplete."));
					continue;
				}
				if (colour.value(item.first) == 2)
					continue;
				colour[item.first] = 1;
				pending.append({item.first, true});
				const auto cls = image.tag(item.first);
				if (!valueOf(cls) || !named(*valueOf(cls), {"MOBJ", "TRAK", "TRKG", "SCLP", "SEQU", "FILL", "TCCP", "RSET", "PVOL", "NEST", "SLCT", "MGRP", "ERAT"}))
				{
					result.complete = false;
					image.problem(item.first, "OMFI:ObjID", QStringLiteral("Component class is unknown or unreadable; relationship coverage is incomplete."));
				}
				if (!image.exists(item.first))
					continue;
				result.objects.append(item.first);
				if (valueOf(cls))
				{
					const auto kind = *valueOf(cls);
					const auto usage = kind == "MOBJ" ? image.integer(item.first, "OMFI:MOBJ:UsageCode") : Read<qint64>(Issue::Missing);
					const bool master = valueOf(usage) && (*valueOf(usage) == 1 || *valueOf(usage) == 7);
					const bool required = kind == "TRAK" ? image.has(item.first, "OMFI:TRAK:TrackComponent") : kind == "SEQU"								 ? image.has(item.first, "OMFI:SEQU:Sequence") || image.has(item.first, "OMFI:SEQU:Components")
																										   : kind == "NEST"									 ? image.has(item.first, "OMFI:NEST:Slots")
																										   : kind == "SLCT"									 ? image.has(item.first, "OMFI:SLCT:Selected")
																										   : kind == "MGRP"									 ? image.has(item.first, "OMFI:MGRP:Choices")
																										   : kind == "ERAT"									 ? image.has(item.first, "OMFI:ERAT:InputSegment")
																										   : master || named(kind, {"TRKG", "RSET", "PVOL"}) ? image.has(item.first, "OMFI:TRKG:Tracks")
																																							 : true;
					if (!required)
					{
						result.complete = false;
						image.problem(item.first, "component graph", QStringLiteral("Required component link is missing."));
					}
				}
				for (const auto *name : {"OMFI:TRKG:Tracks", "OMFI:TRAK:TrackComponent", "OMFI:SEQU:Sequence", "OMFI:SEQU:Components",
										 "OMFI:NEST:Slots", "OMFI:SLCT:Selected", "OMFI:SLCT:Alternates", "OMFI:MGRP:Choices", "OMFI:ERAT:InputSegment"})
				{
					const auto refs = image.references(item.first, name);
					if (const auto *targets = valueOf(refs))
						for (auto it = targets->crbegin(); it != targets->crend(); ++it)
						{
							if (!*it)
								result.complete = false;
							pending.append({*it, false});
						}
					else if (!missing(refs))
					{
						result.complete = false;
						image.problem(item.first, name, QStringLiteral("Component references are unreadable or conflicting; dependent relationships are incomplete."));
					}
				}
			}
			return result;
		}

		struct NamedAttribute
		{
			quint32 object;
			QString name;
		};
		using AttributeList = QVector<NamedAttribute>;

		Read<AttributeList> ownerAttributes(const Image &image, quint32 owner)
		{
			const auto list = target(image, owner, "OMFI:CPNT:Attributes");
			if (!valueOf(list))
				return std::get<Issue>(list);
			if (!*valueOf(list))
				return AttributeList{};
			const auto cls = image.tag(*valueOf(list));
			if (!valueOf(cls) || *valueOf(cls) != "ATTR")
				return Issue::Unreadable;
			const auto entries = image.references(*valueOf(list), "OMFI:ATTR:AttrRefs");
			if (!valueOf(entries))
				return missing(entries) ? Read<AttributeList>(AttributeList{}) : Read<AttributeList>(std::get<Issue>(entries));
			AttributeList found;
			for (const auto entry : *valueOf(entries))
			{
				if (!entry)
					continue;
				const auto entryClass = image.tag(entry);
				const auto entryName = image.text(entry, "OMFI:ATTB:Name");
				if (!valueOf(entryClass) || *valueOf(entryClass) != "ATTB" || !valueOf(entryName))
					return Issue::Unreadable;
				found.append({entry, *valueOf(entryName)});
			}
			return found;
		}

		Read<QVector<quint32>> attributes(const Read<AttributeList> &list, const QString &name)
		{
			if (!valueOf(list))
				return std::get<Issue>(list);
			QVector<quint32> found;
			for (const auto &entry : *valueOf(list))
				if (entry.name == name)
					found.append(entry.object);
			return found;
		}

		Read<QString> attributeText(const Image &image, quint32 attribute)
		{
			const auto kind = image.integer(attribute, "OMFI:ATTB:Kind");
			if (!valueOf(kind))
				return Issue::Unreadable;
			if (*valueOf(kind) == 2)
				return image.text(attribute, "OMFI:ATTB:StringAttribute");
			if (*valueOf(kind) != 4)
				return Issue::Unreadable;
			const auto bytes = image.bytes(attribute, "OMFI:ATTB:BobData", {"omfi:VarLenBytes"});
			const auto size = image.integer(attribute, "OMFI:ATTB:BobSize");
			if (!valueOf(bytes) || !valueOf(size) || *valueOf(size) != valueOf(bytes)->size() || valueOf(bytes)->size() < 3 ||
				(*valueOf(bytes))[0] || (*valueOf(bytes))[1] || valueOf(bytes)->back() != '\0')
				return Issue::Unreadable;
			return textValue(valueOf(bytes)->sliced(2), true);
		}

		Read<QString> namedAttribute(const Image &image, const Read<AttributeList> &list, const QString &name)
		{
			const auto attrs = attributes(list, name);
			if (!valueOf(attrs))
				return std::get<Issue>(attrs);
			std::optional<QString> selected;
			bool bad = false;
			for (const auto attr : *valueOf(attrs))
			{
				const auto text = attributeText(image, attr);
				if (const auto *value = valueOf(text))
				{
					if (value->trimmed().isEmpty())
						continue;
					if (selected && *selected != *value)
						return Issue::Conflict;
					selected = *value;
				}
				else
				{
					if (std::get<Issue>(text) == Issue::Conflict)
						return Issue::Conflict;
					bad = true;
				}
			}
			if (bad)
				return Issue::Unreadable;
			return selected ? Read<QString>(*selected) : Read<QString>(Issue::Missing);
		}

		struct Project
		{
			QString text;
			MdbProjectKey key = MdbProjectKey::None;
			bool conflict = false, complete = true;
		};

		Project ownProject(const Image &image, quint32 owner, const Read<AttributeList> &attrs)
		{
			const std::array<QPair<const char *, MdbProjectKey>, 2> keys{{{"_PJ", MdbProjectKey::Pj}, {"PROJNAME", MdbProjectKey::ProjName}}};
			for (const auto &key : keys)
			{
				const auto text = namedAttribute(image, attrs, QString::fromLatin1(key.first));
				if (const auto *value = valueOf(text))
					return {*value, key.second, false, true};
				if (!missing(text))
				{
					// Keep the failed key for one note covering cause and consequence.
					if (std::get<Issue>(text) == Issue::Conflict)
						return {{}, key.second, true, false};
					image.problem(owner, key.first, QStringLiteral("Project text is unreadable, unsupported or conflicting."));
				}
			}
			const auto users = attributes(attrs, QStringLiteral("_USER"));
			if (!valueOf(users))
				return {{}, MdbProjectKey::None, false, missing(users)};
			Project result;
			for (const auto entry : *valueOf(users))
			{
				const auto kind = image.integer(entry, "OMFI:ATTB:Kind");
				const auto user = target(image, entry, "OMFI:ATTB:ObjAttribute");
				if (!valueOf(kind) || *valueOf(kind) != 3 || !valueOf(user))
				{
					result.complete = false;
					continue;
				}
				if (!*valueOf(user))
					continue;
				// _USER points directly to an ATTR list, not another mob owner.
				const auto userClass = image.tag(*valueOf(user));
				if (!valueOf(userClass) || *valueOf(userClass) != "ATTR")
				{
					result.complete = false;
					continue;
				}
				const auto entries = image.references(*valueOf(user), "OMFI:ATTR:AttrRefs");
				if (!valueOf(entries))
				{
					result.complete = false;
					continue;
				}
				for (const auto attr : *valueOf(entries))
				{
					const auto attrClass = image.tag(attr);
					if (!valueOf(attrClass) || *valueOf(attrClass) != "ATTB")
					{
						result.complete = false;
						continue;
					}
					const auto name = image.text(attr, "OMFI:ATTB:Name");
					if (!valueOf(name))
					{
						result.complete = false;
						continue;
					}
					if (*valueOf(name) != QStringLiteral("_PJ"))
						continue;
					const auto text = attributeText(image, attr);
					if (!valueOf(text))
					{
						result.complete = false;
						result.conflict |= std::get<Issue>(text) == Issue::Conflict;
						continue;
					}
					if (valueOf(text)->trimmed().isEmpty())
						continue;
					if (!result.text.isEmpty() && result.text != *valueOf(text))
						result.conflict = true;
					result.text = *valueOf(text);
					result.key = MdbProjectKey::UserPj;
				}
			}
			if (result.conflict || !result.complete)
				result.text.clear();
			return result;
		}

		QString originalBin(const Image &image, quint32 master, const Read<AttributeList> &attrs)
		{
			const auto entries = attributes(attrs, QStringLiteral("_ORG_BIN"));
			if (!valueOf(entries))
			{
				if (!missing(entries))
					image.problem(master, "_ORG_BIN", QStringLiteral("Direct original-bin attributes cannot be established."));
				return {};
			}
			std::optional<QString> chosen;
			for (const auto entry : *valueOf(entries))
			{
				const auto kind = image.integer(entry, "OMFI:ATTB:Kind");
				const auto bin = target(image, entry, "OMFI:ATTB:ObjAttribute");
				if (!valueOf(kind) || *valueOf(kind) != 3 || !valueOf(bin))
				{
					image.problem(entry, "_ORG_BIN", QStringLiteral("Original-bin attribute is not a readable object reference."));
					return {};
				}
				if (!*valueOf(bin))
					continue;
				const auto cls = image.tag(*valueOf(bin));
				if (!valueOf(cls) || *valueOf(cls) != "MCBR")
				{
					image.problem(*valueOf(bin), "OMFI:ObjID", QStringLiteral("Original-bin target is not a qualified MCBR record."));
					return {};
				}
				const auto modern = image.text(*valueOf(bin), "OMFI:MCBR:MC:binNameUTF8", true);
				if (const auto *issue = std::get_if<Issue>(&modern); issue && *issue == Issue::Conflict)
				{
					image.problem(*valueOf(bin), "OMFI:MCBR:MC:binNameUTF8", QStringLiteral("Conflicting Unicode bin names; bin is unavailable."));
					return {};
				}
				QString name;
				if (valueOf(modern) && !valueOf(modern)->trimmed().isEmpty())
					name = *valueOf(modern);
				else
				{
					if (!missing(modern) && !valueOf(modern))
						image.problem(*valueOf(bin), "OMFI:MCBR:MC:binNameUTF8", QStringLiteral("Invalid Unicode bin name; trying its separate legacy counterpart."));
					const auto legacy = image.text(*valueOf(bin), "OMFI:MCBR:MC:binName");
					if (!valueOf(legacy))
					{
						if (!missing(legacy))
							image.problem(*valueOf(bin), "OMFI:MCBR:MC:binName", QStringLiteral("Legacy bin counterpart is unreadable or conflicting."));
						continue;
					}
					name = *valueOf(legacy);
				}
				if (name.trimmed().isEmpty())
					continue;
				if (chosen && *chosen != name)
				{
					image.problem(master, "_ORG_BIN", QStringLiteral("Conflicting direct original-bin names; bin is unavailable."));
					return {};
				}
				chosen = name;
			}
			return chosen.value_or(QString{});
		}

		MdbPrecomputeCategory precomputeCategory(const Image &image, quint32 master, const Read<AttributeList> &attrs)
		{
			const auto imports = attributes(attrs, QStringLiteral("_IMPORTSETTING"));
			if (!valueOf(imports))
				return missing(imports) ? MdbPrecomputeCategory::RenderedEffects : MdbPrecomputeCategory::Unknown;
			if (valueOf(imports)->isEmpty())
				return MdbPrecomputeCategory::RenderedEffects;
			for (const auto entry : *valueOf(imports))
			{
				const auto kind = image.integer(entry, "OMFI:ATTB:Kind");
				// Preserve duplicate-entry handling; only one known non-object entry proves this absence.
				if (const auto *knownKind = valueOf(kind); knownKind && valueOf(imports)->size() == 1 &&
														   (*knownKind == 1 || *knownKind == 2 || *knownKind == 4))
					return MdbPrecomputeCategory::RenderedEffects;
				if (!valueOf(kind) || *valueOf(kind) != 3 || !valueOf(target(image, entry, "OMFI:ATTB:ObjAttribute")))
					return MdbPrecomputeCategory::Unknown;
			}
			const auto tracks = image.references(master, "OMFI:TRKG:Tracks");
			if (!valueOf(tracks))
				return MdbPrecomputeCategory::Unknown;
			qsizetype video = 0;
			for (const auto track : *valueOf(tracks))
			{
				const auto component = target(image, track, "OMFI:TRAK:TrackComponent");
				if (!valueOf(component) || !*valueOf(component))
					return MdbPrecomputeCategory::Unknown;
				const auto kind = image.integer(*valueOf(component), "OMFI:CPNT:TrackKind");
				if (!valueOf(kind))
					return MdbPrecomputeCategory::Unknown;
				video += *valueOf(kind) == 1;
			}
			return video >= 2 ? MdbPrecomputeCategory::TitlesAndMatteKeys : MdbPrecomputeCategory::RenderedEffects;
		}

		std::optional<qint64> integerValue(const Image &image, quint32 object, const char *name)
		{
			const auto read = image.integer(object, name);
			if (!valueOf(read) && !missing(read))
				image.problem(object, name, QStringLiteral("Numeric value has an unsupported type, incomplete width, byte order or conflicting values."));
			return optionalValue(read);
		}

		template <std::size_t N>
		std::optional<std::array<quint8, N>> fixedBytes(QByteArrayView bytes)
		{
			if (bytes.size() != qsizetype(N))
				return std::nullopt;
			std::array<quint8, N> result{};
			std::copy(bytes.begin(), bytes.end(), result.begin());
			return result;
		}

		void codingFacts(const Image &image, quint32 descriptor, MdbFileRecord &file)
		{
			const auto coded = image.bytes(descriptor, "OMFI:DIDD:EssenceCompression", {"omfi:GUID"});
			const auto big = valueOf(coded) && valueOf(coded)->size() == 16 ? image.bigEndian(descriptor) : std::nullopt;
			if (missing(coded))
				file.compression.codingState = MdbCodingState::Absent;
			else if (big)
			{
				const auto bytes = *valueOf(coded);
				auto &label = file.compression.codingLabel;
				std::copy(bytes.begin() + 8, bytes.end(), label.begin());
				for (int i = 0; i < 8; ++i)
				{
					constexpr std::array<int, 8> littleOffsets{3, 2, 1, 0, 5, 4, 7, 6};
					label[std::size_t(8 + i)] = quint8(bytes[*big ? i : littleOffsets[std::size_t(i)]]);
				}
				file.compression.codingState = MdbCodingState::Present;
			}
			else
				image.problem(descriptor, "OMFI:DIDD:EssenceCompression", QStringLiteral("Coding identity is unavailable; it is not established absent."));
			const auto compression = image.bytes(descriptor, "OMFI:DIDD:Compression", {"omfi:String"});
			if (const auto *bytes = valueOf(compression))
			{
				if (bytes->size() == 4 || (bytes->size() == 5 && bytes->back() == '\0'))
					file.compression.legacyCompression = fixedBytes<4>(bytes->first(4));
				else
					image.problem(descriptor, "OMFI:DIDD:Compression", QStringLiteral("Legacy compression identifier is not a complete four-byte code."));
			}
			else if (!missing(compression))
				image.problem(descriptor, "OMFI:DIDD:Compression", QStringLiteral("Legacy compression identifier has an unreadable or conflicting typed value."));
			file.compression.resolutionId = integerValue(image, descriptor, "OMFI:DIDD:DIDResolutionID");
			const auto subsampling = [&](const char *name) -> std::optional<qint32>
			{
				const auto value = integerValue(image, descriptor, name);
				if (value && *value > 0 && *value <= std::numeric_limits<qint32>::max())
					return qint32(*value);
				if (value)
					image.problem(descriptor, name, QStringLiteral("Subsampling is outside its supported positive integer range."));
				return std::nullopt;
			};
			file.compression.horizontalSubsampling = subsampling("OMFI:CDCI:HorizontalSubsampling");
			file.compression.verticalSubsampling = subsampling("OMFI:CDCI:VerticalSubsampling");
		}

		std::optional<qint64> dimension(const Image &image, quint32 owner, const char *name, std::optional<qint64> fallback = {})
		{
			const auto read = image.integer(owner, name);
			if (missing(read))
				return fallback;
			return optionalValue(read);
		}

		void geometry(const Image &image, quint32 descriptor, MdbFileRecord &file)
		{
			const auto width = dimension(image, descriptor, "OMFI:DIDD:StoredWidth");
			const auto height = dimension(image, descriptor, "OMFI:DIDD:StoredHeight");
			const auto layout = dimension(image, descriptor, "OMFI:DIDD:FrameLayout", 0);
			if (layout && *layout >= 0 && *layout <= 3)
				file.compression.frameLayout = quint16(*layout);
			if (!width || !height || !layout || *width <= 0 || *height <= 0 || *layout < 0 || *layout > 3)
				return;
			const qint64 multiplier = (*layout == 1 || *layout == 3) ? 2 : 1;
			if (*height > std::numeric_limits<qint64>::max() / multiplier)
				return;
			file.compression.namingGeometry = MdbDimensions{*width, *height * multiplier};
			const auto displayWidth = dimension(image, descriptor, "OMFI:DIDD:DisplayWidth", width);
			const auto displayHeight = dimension(image, descriptor, "OMFI:DIDD:DisplayHeight", height);
			const auto x = dimension(image, descriptor, "OMFI:DIDD:DisplayXOffset", 0);
			const auto y = dimension(image, descriptor, "OMFI:DIDD:DisplayYOffset", 0);
			if (!displayWidth || !displayHeight || !x || !y)
			{
				image.problem(descriptor, "OMFI:DIDD:DisplayWidth", QStringLiteral("Visible geometry has unreadable or conflicting rectangle fields."));
				return;
			}
			if (*displayWidth > 0 && *displayHeight > 0 && *x >= 0 && *y >= 0 &&
				*displayWidth <= *width && *displayHeight <= *height && *x <= *width - *displayWidth && *y <= *height - *displayHeight)
			{
				const bool recorded = image.has(descriptor, "OMFI:DIDD:DisplayWidth") || image.has(descriptor, "OMFI:DIDD:DisplayHeight") ||
									  image.has(descriptor, "OMFI:DIDD:DisplayXOffset") || image.has(descriptor, "OMFI:DIDD:DisplayYOffset");
				file.resolution = MdbRaster{{*displayWidth, *displayHeight * multiplier}, recorded ? MdbRasterOrigin::Display : MdbRasterOrigin::Stored};
				return;
			}
			// This is the finite, evidenced proxy table; a small raster alone is not a proxy.
			struct Proxy
			{
				qint64 id, width, height, displayWidth, displayHeight, layout;
			};
			constexpr std::array<Proxy, 7> proxies{{{3472, 480, 270, 1920, 540, 2}, {3484, 480, 270, 1920, 1080, 0}, {3487, 480, 270, 1920, 1080, 0}, {3488, 320, 180, 1280, 720, 0}, {3470, 352, 240, 720, 240, 2}, {3491, 352, 240, 720, 240, 2}, {3483, 352, 288, 720, 576, 0}}};
			const auto expected = QByteArray::fromHex("060e2b340401010d0401020201311101");
			const auto &facts = file.compression;
			if (facts.codingState == MdbCodingState::Present && facts.resolutionId &&
				QByteArrayView(reinterpret_cast<const char *>(facts.codingLabel.data()), 16) == expected && *x == 0 && *y == 0)
			{
				const auto sw = dimension(image, descriptor, "OMFI:DIDD:SampledWidth");
				const auto sh = dimension(image, descriptor, "OMFI:DIDD:SampledHeight");
				const auto sx = dimension(image, descriptor, "OMFI:DIDD:SampledXOffset", 0);
				const auto sy = dimension(image, descriptor, "OMFI:DIDD:SampledYOffset", 0);
				for (const auto &proxy : proxies)
					if (*facts.resolutionId == proxy.id && *layout == proxy.layout && *width == proxy.width && *height == proxy.height &&
						*displayWidth == proxy.displayWidth && *displayHeight == proxy.displayHeight && sw == displayWidth && sh == displayHeight && sx == 0 && sy == 0)
					{
						file.resolution = MdbRaster{*facts.namingGeometry, MdbRasterOrigin::VerifiedProxy};
						return;
					}
			}
			image.problem(descriptor, "OMFI:DIDD:DisplayWidth", QStringLiteral("Visible rectangle is outside Stored and is not a verified proxy configuration."));
		}

		void pictureDepth(const Image &image, quint32 descriptor, MdbFileRecord &file, QByteArrayView cls)
		{
			if (cls == "RGBA")
			{
				const auto readArray = [&](const char *name, const char *arrayType) -> std::optional<MdbComponentArray>
				{
					const auto read = image.bytes(descriptor, name, {"omfi:VarLenBytes", arrayType});
					if (!valueOf(read) || valueOf(read)->size() > 8)
						return std::nullopt;
					MdbComponentArray result;
					result.count = quint8(valueOf(read)->size());
					std::copy(valueOf(read)->begin(), valueOf(read)->end(), result.values.begin());
					return result;
				};
				file.bitDepth.pixelLayout = readArray("OMFI:RGBA:PixelLayout", "omfi:CompCodeArray");
				file.bitDepth.pixelStructure = readArray("OMFI:RGBA:PixelStructure", "omfi:CompSizeArray");
				const auto &codes = file.bitDepth.pixelLayout, &widths = file.bitDepth.pixelStructure;
				if (!codes || !widths || codes->count != widths->count)
				{
					image.problem(descriptor, "OMFI:RGBA:PixelLayout", QStringLiteral("RGBA's separate component arrays are incomplete, oversized or disagree in length."));
					return;
				}
				std::optional<quint32> common;
				QSet<quint8> seen;
				for (quint8 i = 0; i < codes->count && codes->values[i]; ++i)
				{
					const auto code = codes->values[i], bits = widths->values[i];
					if (code == 'F' || code == '0')
						continue;
					if (!QByteArrayView("RGBA").contains(char(code)) || seen.contains(code) || bits == 0 || bits > 64 || (common && *common != bits))
					{
						image.problem(descriptor, "OMFI:RGBA:PixelStructure", QStringLiteral("RGBA does not establish one supported common non-fill component depth."));
						return;
					}
					seen.insert(code);
					common = bits;
				}
				file.bitDepth.bits = common;
				if (common)
					file.bitDepth.representation = MdbDepthRepresentation::Integer;
				return;
			}
			const auto raw = integerValue(image, descriptor, "OMFI:CDCI:ComponentWidth");
			if (!raw || *raw < std::numeric_limits<qint32>::min() || *raw > std::numeric_limits<qint32>::max())
				return;
			file.bitDepth.rawComponentWidth = qint32(*raw);
			const auto coding = QByteArrayView(reinterpret_cast<const char *>(file.compression.codingLabel.data()), 16);
			const bool standard = file.compression.codingState == MdbCodingState::Present && coding == QByteArray::fromHex("060e2b340401010d0401020203070100");
			const bool fixed = file.compression.codingState == MdbCodingState::Present && coding == QByteArray::fromHex("060e2b340401010d0401020203070200");
			if (*raw == 253 && standard)
			{
				file.bitDepth.bits = 16;
				file.bitDepth.representation = MdbDepthRepresentation::HalfFloat;
			}
			else if (*raw == 254 && (standard || fixed))
			{
				file.bitDepth.bits = standard ? 32 : 16;
				file.bitDepth.representation = standard ? MdbDepthRepresentation::Float : MdbDepthRepresentation::S2_14Fixed;
			}
			else if (fixed && (*raw == 10 || *raw == 12))
			{
				file.bitDepth.storageBits = 16;
				file.bitDepth.representation = *raw == 10 ? MdbDepthRepresentation::Fixed10_6 : MdbDepthRepresentation::Fixed12_4;
			}
			else if (*raw > 0 && *raw <= 64)
			{
				file.bitDepth.bits = quint32(*raw);
				file.bitDepth.representation = MdbDepthRepresentation::Integer;
			}
			else
				image.problem(descriptor, "OMFI:CDCI:ComponentWidth", QStringLiteral("Component width has no supported interpretation in its complete coding context."));
		}

		using Owners = QHash<MobId, QVector<quint32>>;

		// The all-zero SourceID is a documented original-source terminator.
		// It has this meaning here, never in the shared MobId conversion.
		Read<std::optional<MobId>> sourceIdentity(const Image &image, quint32 clip)
		{
			const auto raw = image.bytes(clip, "OMFI:SCLP:SourceID", {"omfi:UID"});
			if (!valueOf(raw) || (valueOf(raw)->size() != 12 && valueOf(raw)->size() != 32))
				return Issue::Unreadable;
			if (std::all_of(valueOf(raw)->begin(), valueOf(raw)->end(), [](char c)
							{ return c == 0; }))
				return std::optional<MobId>{};
			const auto id = image.identity(clip, "OMFI:SCLP:SourceID");
			return valueOf(id) ? Read<std::optional<MobId>>(std::optional<MobId>(*valueOf(id))) : Read<std::optional<MobId>>(std::get<Issue>(id));
		}

		bool sourceOwner(const Image &image, quint32 object)
		{
			if (image.disputed(object))
				return false;
			if (image.has(object, "OMFI:MOBJ:PhysicalMedia"))
			{
				const auto descriptor = target(image, object, "OMFI:MOBJ:PhysicalMedia");
				if (!valueOf(descriptor) || !*valueOf(descriptor))
					return false;
				const auto cls = image.tag(*valueOf(descriptor));
				return valueOf(cls) && (mediaDescriptor(*valueOf(cls)) || named(*valueOf(cls), {"MDES", "MDTP", "MDFM"}));
			}
			const auto usage = image.integer(object, "OMFI:MOBJ:UsageCode");
			return missing(usage) || (valueOf(usage) && *valueOf(usage) != 1 && *valueOf(usage) != 7);
		}

		void selectProject(const Image &image, quint32 owner, const Owners &owners, MdbFileRecord &file)
		{
			const auto attrs = ownerAttributes(image, owner);
			if (!valueOf(attrs) && !missing(attrs))
			{
				if (std::get<Issue>(attrs) == Issue::Conflict)
				{
					file.projectSelection = MdbProjectSelection::Conflict;
					image.problem(owner, "OMFI:CPNT:Attributes", QStringLiteral("Project attributes conflict; Project is left blank without substituting a linked record."));
				}
				else
					image.problem(owner, "OMFI:CPNT:Attributes", QStringLiteral("Project attributes cannot be read; Project is left blank without substituting a linked record."));
				return;
			}
			auto selected = ownProject(image, owner, attrs);
			if (selected.conflict)
			{
				file.projectSelection = MdbProjectSelection::Conflict;
				if (selected.key == MdbProjectKey::Pj || selected.key == MdbProjectKey::ProjName)
					image.problem(owner, selected.key == MdbProjectKey::Pj ? "_PJ" : "PROJNAME", QStringLiteral("Project names disagree; Project is left blank without substituting another value."));
				else
					image.problem(owner, "OMFI:CPNT:Attributes", QStringLiteral("Own Project candidates conflict; lower-priority Project substitution is blocked."));
				return;
			}
			if (!selected.text.isEmpty())
			{
				file.project = selected.text;
				file.projectKey = selected.key;
				file.projectSelection = MdbProjectSelection::OwnFile;
				return;
			}
			if (!selected.complete)
			{
				image.problem(owner, "OMFI:CPNT:Attributes", QStringLiteral("Incomplete own Project attributes prevent lower-owner substitution."));
				return;
			}
			const auto walk = components(image, owner);
			bool complete = walk.complete;
			QSet<quint32> visited;
			for (const auto object : walk.objects)
			{
				const auto cls = image.tag(object);
				if (!valueOf(cls) || *valueOf(cls) != "SCLP")
					continue;
				const auto source = sourceIdentity(image, object);
				if (!valueOf(source))
				{
					complete = false;
					continue;
				}
				if (!*valueOf(source))
					continue;
				const auto found = owners.constFind(**valueOf(source));
				QVector<quint32> candidates;
				if (found != owners.cend())
					for (const auto candidate : *found)
						if (candidate != owner && sourceOwner(image, candidate))
							candidates.append(candidate);
				if (candidates.size() != 1)
				{
					complete = false;
					continue;
				}
				const auto candidate = candidates.first();
				if (visited.contains(candidate))
					continue;
				visited.insert(candidate);
				const auto project = ownProject(image, candidate, ownerAttributes(image, candidate));
				if (project.conflict && (project.key == MdbProjectKey::Pj || project.key == MdbProjectKey::ProjName))
					image.problem(candidate, project.key == MdbProjectKey::Pj ? "_PJ" : "PROJNAME", QStringLiteral("Project text is unreadable, unsupported or conflicting."));
				complete &= project.complete;
				selected.conflict |= project.conflict;
				if (project.text.isEmpty())
					continue;
				if (!selected.text.isEmpty() && selected.text != project.text)
					selected.conflict = true;
				selected.key = selected.text.isEmpty() ? project.key : selected.key == project.key ? selected.key
																								   : MdbProjectKey::Mixed;
				selected.text = project.text;
			}
			if (selected.conflict)
			{
				file.projectSelection = MdbProjectSelection::Conflict;
				image.problem(owner, "OMFI:SCLP:SourceID", QStringLiteral("Immediate-source Project candidates conflict; Project is withheld."));
			}
			else if (!complete)
				image.problem(owner, "OMFI:SCLP:SourceID", QStringLiteral("Immediate-source Project relationships or attributes are incomplete; Project is withheld."));
			else if (!selected.text.isEmpty())
			{
				file.project = selected.text;
				file.projectKey = selected.key;
				file.projectSelection = MdbProjectSelection::ImmediateSources;
			}
		}

		struct AudioFraction
		{
			quint64 numerator;
			quint64 denominator;
		};

		std::optional<AudioFraction> mdbAudioFraction(const MdbAudioSourceValue &source)
		{
			if (const auto *words = std::get_if<MdbRateWords>(&source))
			{
				if (words->numerator <= 0 || words->denominator <= 0)
					return std::nullopt;
				const auto numerator = quint64(words->numerator);
				const auto denominator = quint64(words->denominator);
				const auto divisor = std::gcd(numerator, denominator);
				return AudioFraction{numerator / divisor, denominator / divisor};
			}
			if (const auto *integer = std::get_if<quint32>(&source))
				return *integer ? std::optional<AudioFraction>{AudioFraction{*integer, 1}} : std::nullopt;

			const auto *bytes = std::get_if<std::array<quint8, 10>>(&source);
			if (!bytes)
				return std::nullopt;
			const quint16 signExponent = qFromBigEndian<quint16>(bytes->data());
			const quint16 exponent = signExponent & 0x7fff;
			quint64 significand = qFromBigEndian<quint64>(bytes->data() + 2);
			if ((signExponent & 0x8000) || exponent == 0x7fff || significand == 0)
				return std::nullopt;
			// The stored integer bit distinguishes normal numbers from subnormals.
			// Pseudo-denormals and unnormal encodings are not supported AIFF numbers.
			const bool integerBit = (significand & (quint64{1} << 63)) != 0;
			if ((exponent == 0 && integerBit) || (exponent != 0 && !integerBit))
				return std::nullopt;
			int power = int(exponent == 0 ? 1 : exponent) - 16383 - 63;
			while ((significand & 1) == 0)
			{
				significand >>= 1;
				++power;
			}
			if (power >= 0)
			{
				if (power >= 64 || significand > (std::numeric_limits<quint64>::max() >> power))
					return std::nullopt;
				return AudioFraction{significand << power, 1};
			}
			if (power < -63)
				return std::nullopt;
			return AudioFraction{significand, quint64{1} << -power};
		}

		bool audioTag(QByteArrayView bytes, qsizetype offset, const char *tag)
		{
			return offset >= 0 && offset <= bytes.size() && bytes.size() - offset >= 4 &&
				   std::memcmp(bytes.data() + offset, tag, 4) == 0;
		}

		struct AudioSummaryValues
		{
			std::optional<MdbAudioSourceValue> frequency;
			std::optional<quint32> bits;
			std::optional<quint32> storageBits;
			std::optional<MdbDepthRepresentation> representation;
			std::optional<MdbAudioEncoding> encoding;
		};

		AudioSummaryValues mdbWaveFormat(QByteArrayView bytes, QStringList &notes)
		{
			AudioSummaryValues result;
			// The traversal established the complete fmt chunk and its 16-byte prefix.
			const quint16 format = qFromLittleEndian<quint16>(bytes.data());
			const quint32 frequency = qFromLittleEndian<quint32>(bytes.data() + 4);
			const quint16 storage = qFromLittleEndian<quint16>(bytes.data() + 14);
			if (frequency != 0)
				result.frequency = MdbAudioSourceValue{frequency};
			else
				notes.append(QStringLiteral("WAVE fmt sample frequency is zero."));
			if (format == 1 || format == 3 || format == 0xfffe)
			{
				if (storage != 0)
					result.storageBits = storage;
				else
					notes.append(QStringLiteral("Audio bit depth is unavailable because the recorded sample storage width is zero."));
			}

			quint16 subtype = format;
			std::optional<quint16> precision;
			if (format == 0xfffe)
			{
				if (bytes.size() < 18)
				{
					notes.append(QStringLiteral("WAVE extensible format has no complete extension-size field; precision and encoding are unavailable."));
					return result;
				}
				const quint16 extension = qFromLittleEndian<quint16>(bytes.data() + 16);
				if (extension < 22 || extension > bytes.size() - 18)
				{
					notes.append(QStringLiteral("WAVE extensible format has an incomplete required extension; precision and encoding are unavailable."));
					return result;
				}
				constexpr std::array<quint8, 16> pcmGuid = {1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
				constexpr std::array<quint8, 16> floatGuid = {3, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
				if (std::memcmp(bytes.data() + 24, pcmGuid.data(), pcmGuid.size()) == 0)
					subtype = 1;
				else if (std::memcmp(bytes.data() + 24, floatGuid.data(), floatGuid.size()) == 0)
					subtype = 3;
				else
				{
					notes.append(QStringLiteral("WAVE extensible subtype is unsupported; storage width does not establish sample precision."));
					return result;
				}
				precision = qFromLittleEndian<quint16>(bytes.data() + 18);
			}
			else
				precision = storage;

			if (subtype == 1)
			{
				result.encoding = MdbAudioEncoding::Pcm;
				result.representation = MdbDepthRepresentation::Integer;
			}
			else if (subtype == 3)
			{
				result.encoding = MdbAudioEncoding::IeeeFloat;
				result.representation = MdbDepthRepresentation::Float;
			}
			else
			{
				notes.append(QStringLiteral("WAVE encoding is unsupported for sample precision and compression naming."));
				return result;
			}
			if (storage == 0 || storage > 64 || *precision == 0 || *precision > storage || *precision > 64 ||
				(subtype == 3 && storage != 32 && storage != 64))
			{
				if (storage != 0)
					notes.append(QStringLiteral("WAVE sample precision is invalid or unsupported for its storage width and encoding."));
			}
			else
				result.bits = *precision;
			return result;
		}

		AudioSummaryValues mdbAiffCommon(QByteArrayView bytes, bool compressed, QStringList &notes)
		{
			AudioSummaryValues result;
			std::array<quint8, 10> frequency{};
			std::memcpy(frequency.data(), bytes.data() + 8, frequency.size());
			const MdbAudioSourceValue source{frequency};
			if (mdbAudioFraction(source))
				result.frequency = source;
			else
				notes.append(QStringLiteral("AIFF COMM sample frequency is invalid or outside the supported exact unsigned-64 fraction range."));

			const qint16 precision = qFromBigEndian<qint16>(bytes.data() + 6);
			bool integer = !compressed;
			bool floating = false;
			std::optional<quint32> requiredWidth;
			if (compressed)
			{
				if (bytes.size() < 22)
				{
					notes.append(QStringLiteral("AIFF-C COMM encoding identifier is incomplete; sample precision and encoding are unavailable."));
					return result;
				}
				integer = audioTag(bytes, 18, "NONE") || audioTag(bytes, 18, "twos") ||
						  audioTag(bytes, 18, "sowt") || audioTag(bytes, 18, "in24") || audioTag(bytes, 18, "in32");
				floating = audioTag(bytes, 18, "fl32") || audioTag(bytes, 18, "FL32") ||
						   audioTag(bytes, 18, "fl64") || audioTag(bytes, 18, "FL64");
				if (audioTag(bytes, 18, "in24"))
					requiredWidth = 24;
				else if (audioTag(bytes, 18, "in32") || audioTag(bytes, 18, "fl32") || audioTag(bytes, 18, "FL32"))
					requiredWidth = 32;
				else if (audioTag(bytes, 18, "fl64") || audioTag(bytes, 18, "FL64"))
					requiredWidth = 64;
				result.storageBits = requiredWidth;
			}
			if (integer)
			{
				result.encoding = MdbAudioEncoding::Pcm;
				result.representation = MdbDepthRepresentation::Integer;
			}
			else if (floating)
			{
				result.encoding = MdbAudioEncoding::IeeeFloat;
				result.representation = MdbDepthRepresentation::Float;
			}
			else
			{
				notes.append(QStringLiteral("AIFF-C encoding is unsupported for sample precision and compression naming."));
				return result;
			}
			if (precision <= 0 || precision > 64 || (requiredWidth && quint32(precision) != *requiredWidth))
				notes.append(QStringLiteral("AIFF COMM sample precision is invalid or unsupported for its encoding."));
			else
				result.bits = quint32(precision);
			return result;
		}

		template <typename Value>
		void mergeAudioValue(std::optional<Value> &current, const std::optional<Value> &next,
							 const QString &field, QStringList &notes)
		{
			// An unavailable value stays unavailable through later format claims.
			if (!current)
				return;
			if (!next || *current != *next)
			{
				if (next)
					notes.append(QStringLiteral("Repeated audio format chunks disagree on %1.").arg(field));
				current.reset();
			}
		}

		void mergeAudioSummary(AudioSummaryValues &current, const AudioSummaryValues &next,
							   QStringList &notes)
		{
			if (current.frequency)
			{
				const auto first = mdbAudioFraction(*current.frequency);
				const auto second = next.frequency ? mdbAudioFraction(*next.frequency) : std::nullopt;
				if (!first || !second || first->numerator != second->numerator || first->denominator != second->denominator)
				{
					if (first && second)
						notes.append(QStringLiteral("Repeated audio format chunks disagree on sample frequency."));
					current.frequency.reset();
				}
			}
			mergeAudioValue(current.bits, next.bits, QStringLiteral("sample precision"), notes);
			mergeAudioValue(current.storageBits, next.storageBits, QStringLiteral("sample storage width"), notes);
			mergeAudioValue(current.representation, next.representation, QStringLiteral("sample representation"), notes);
			mergeAudioValue(current.encoding, next.encoding, QStringLiteral("audio encoding"), notes);
		}

		void readMdbAudioSummary(QByteArrayView bytes, bool aiff, MdbFileRecord &file, QStringList &notes)
		{
			file.audioFrequency.reset();
			file.bitDepth.bits.reset();
			file.bitDepth.storageBits.reset();
			file.bitDepth.representation = MdbDepthRepresentation::Unknown;
			file.compression.audioEncoding = MdbAudioEncoding::Unknown;
			if (bytes.size() < 12 || !audioTag(bytes, 0, aiff ? "FORM" : "RIFF") ||
				(aiff ? !(audioTag(bytes, 8, "AIFF") || audioTag(bytes, 8, "AIFC")) : !audioTag(bytes, 8, "WAVE")))
			{
				notes.append(QStringLiteral("Audio Summary has an incomplete or unsupported container header."));
				return;
			}
			const quint32 declared = aiff ? qFromBigEndian<quint32>(bytes.data() + 4) : qFromLittleEndian<quint32>(bytes.data() + 4);
			if (declared < 4)
			{
				notes.append(QStringLiteral("Audio Summary container length cannot contain its form identifier."));
				return;
			}
			// A Summary can omit recording data. Bound each metadata chunk against
			// both its declared container and the bytes that this property supplies.
			const quint64 end = std::min(quint64(bytes.size()), quint64(declared) + 8);
			const bool compressed = aiff && audioTag(bytes, 8, "AIFC");
			AudioSummaryValues values;
			bool found = false;
			quint64 offset = 12;
			while (offset < end)
			{
				if (end - offset < 8)
				{
					notes.append(found ? QStringLiteral("Audio Summary has an incomplete metadata chunk header.")
									   : QStringLiteral("Audio Summary has an incomplete metadata chunk header. No complete required format chunk could be read before sound data."));
					if (!found)
						return;
					break;
				}
				const auto index = qsizetype(offset);
				const bool required = audioTag(bytes, index, aiff ? "COMM" : "fmt ");
				const quint32 size = aiff ? qFromBigEndian<quint32>(bytes.data() + index + 4) : qFromLittleEndian<quint32>(bytes.data() + index + 4);
				// Do not require an omitted sound payload, or look inside it for tags.
				if (audioTag(bytes, index, aiff ? "SSND" : "data"))
					break;
				const quint64 payload = offset + 8;
				const quint64 paddedSize = quint64(size) + (size & 1);
				if (paddedSize > end - payload)
				{
					notes.append(found ? QStringLiteral("Audio Summary metadata chunk exceeds its available container range.")
									   : QStringLiteral("Audio Summary metadata chunk exceeds its available container range. No complete required format chunk could be read before sound data."));
					if (required)
					{
						// A damaged additional required claim cannot establish agreement
						// with an earlier format claim. Unrelated damage keeps it intact.
						values = {};
					}
					if (!found)
						return;
					break;
				}
				if (required)
				{
					AudioSummaryValues next;
					if (size < (aiff ? 18u : 16u))
						notes.append(QStringLiteral("Audio Summary format chunk has an incomplete required prefix."));
					else
					{
						const QByteArrayView body(bytes.data() + qsizetype(payload), qsizetype(size));
						next = aiff ? mdbAiffCommon(body, compressed, notes) : mdbWaveFormat(body, notes);
					}
					if (found)
						mergeAudioSummary(values, next, notes);
					else
						values = next;
					found = true;
				}
				offset = payload + paddedSize;
			}
			if (!found)
			{
				notes.append(QStringLiteral("Audio Summary has no complete required format chunk before sound data."));
				return;
			}
			file.audioFrequency = values.frequency;
			file.bitDepth.bits = values.bits;
			file.bitDepth.storageBits = values.storageBits;
			file.bitDepth.representation = values.representation.value_or(MdbDepthRepresentation::Unknown);
			file.compression.audioEncoding = values.encoding.value_or(MdbAudioEncoding::Unknown);
		}

		void readMdbWaveSummary(QByteArrayView bytes, MdbFileRecord &file, QStringList &notes)
		{
			readMdbAudioSummary(bytes, false, file, notes);
		}

		void readMdbAiffSummary(QByteArrayView bytes, MdbFileRecord &file, QStringList &notes)
		{
			readMdbAudioSummary(bytes, true, file, notes);
		}
		struct NumberingClock
		{
			qint64 numerator = 0, denominator = 1;
		};

		NumberingClock numberingClock(const MdbRate &rate)
		{
			NumberingClock clock{rate.words.numerator, rate.words.denominator};
			if (clock.numerator <= 0 || clock.denominator <= 0)
				return {};
			if (rate.encoding == MdbRateEncoding::ExactEditRate)
				for (qint64 multiple = 1; multiple <= 10; ++multiple)
				{
					if (clock.numerator * 1000 == clock.denominator * 23976 * multiple)
						return {24000 * multiple, 1001};
					if (clock.numerator * 100 == clock.denominator * 2997 * multiple)
						return {30000 * multiple, 1001};
				}
			const auto common = std::gcd(clock.numerator, clock.denominator);
			clock.numerator /= common;
			clock.denominator /= common;
			return clock;
		}

		bool sameNumberingClock(const NumberingClock &left, const NumberingClock &right)
		{
			// Inputs are positive signed32 source words or bounded finite Avid aliases.
			return left.numerator > 0 && right.numerator > 0 &&
				   left.numerator * right.denominator == right.numerator * left.denominator;
		}

		Read<qint64> numberingScalar(const Image &image, quint32 owner, const char *name)
		{
			if (QByteArrayView(name) != "OMFI:CLIP:Length")
				return image.integer(owner, name);
			const auto big = image.bigEndian(owner);
			return image.field<qint64>(owner, name, [big](QByteArrayView bytes, QByteArrayView type) -> Read<qint64>
									   {
				if (!big || bytes.size() != 4 || !named(type, {"omfi:Long", "omfi:Int32", "omfi:Length32"}))
					return Issue::Unreadable;
				return number<qint32>(bytes.data(), *big); });
		}

		struct NumberingLeaf
		{
			quint32 owner = 0;
			QByteArrayView cls;
			qint64 begin = 0, length = 0;
		};

		struct NumberingTrack
		{
			std::optional<qint64> label;
			qint64 kind = 0, length = 0;
			NumberingClock clock;
			QVector<NumberingLeaf> leaves;
		};

		Read<QVector<NumberingTrack>> numberingTracks(const Image &image, quint32 owner,
													  qint64 essenceKind, const NumberingClock &displayClock)
		{
			const auto refs = image.references(owner, "OMFI:TRKG:Tracks");
			if (missing(refs))
				return QVector<NumberingTrack>{};
			if (!valueOf(refs))
				return std::get<Issue>(refs);
			QVector<NumberingTrack> result;
			for (const auto handle : *valueOf(refs))
			{
				const auto cls = image.tag(handle);
				const auto component = target(image, handle, "OMFI:TRAK:TrackComponent");
				if (!valueOf(cls) || *valueOf(cls) != "TRAK" || !valueOf(component) || !*valueOf(component))
					return Issue::Unreadable;
				const auto kind = numberingScalar(image, *valueOf(component), "OMFI:CPNT:TrackKind");
				if (!valueOf(kind))
					return Issue::Unreadable;
				if (*valueOf(kind) != essenceKind && *valueOf(kind) != 3)
					continue;
				const auto rate = image.rate(*valueOf(component), "OMFI:CPNT:EditRate");
				if (!valueOf(rate))
					return Issue::Unreadable;
				const auto clock = numberingClock(*valueOf(rate));
				// A complete different-clock TC track is inapplicable. Do not inspect its flag.
				if (*valueOf(kind) == 3 && !sameNumberingClock(clock, displayClock))
					continue;
				NumberingTrack track;
				track.kind = *valueOf(kind);
				track.clock = clock;
				const auto label = numberingScalar(image, handle, "OMFI:TRAK:LabelNumber");
				if (valueOf(label))
					track.label = *valueOf(label);
				else if (!missing(label))
					return Issue::Unreadable;
				QVector<quint32> leaves{*valueOf(component)};
				const auto rootClass = image.tag(*valueOf(component));
				if (!valueOf(rootClass))
					return Issue::Unreadable;
				if (*valueOf(rootClass) == "SEQU")
				{
					const auto sequence = image.references(*valueOf(component), "OMFI:SEQU:Sequence");
					const auto components = image.references(*valueOf(component), "OMFI:SEQU:Components");
					if ((!missing(sequence) && !valueOf(sequence)) || (!missing(components) && !valueOf(components)))
						return Issue::Unreadable;
					if (valueOf(sequence) && valueOf(components) && *valueOf(sequence) != *valueOf(components))
						return Issue::Conflict;
					if (!valueOf(sequence) && !valueOf(components))
						return Issue::Unreadable;
					leaves = valueOf(sequence) ? *valueOf(sequence) : *valueOf(components);
				}
				for (const auto leafOwner : leaves)
				{
					const auto leafClass = image.tag(leafOwner);
					const auto leafKind = numberingScalar(image, leafOwner, "OMFI:CPNT:TrackKind");
					const auto leafRate = image.rate(leafOwner, "OMFI:CPNT:EditRate");
					const auto length = numberingScalar(image, leafOwner, "OMFI:CLIP:Length");
					if (!valueOf(leafClass) || !named(*valueOf(leafClass), {"FILL", "SCLP", "TCCP"}) ||
						!valueOf(leafKind) || *valueOf(leafKind) != track.kind || !valueOf(leafRate) ||
						!sameNumberingClock(numberingClock(*valueOf(leafRate)), track.clock) || !valueOf(length) || *valueOf(length) < 0)
						return Issue::Unreadable;
					if ((track.kind == 3 && *valueOf(leafClass) == "SCLP") || (track.kind != 3 && *valueOf(leafClass) == "TCCP"))
						return Issue::Unreadable;
					if (track.length > std::numeric_limits<qint64>::max() - *valueOf(length))
						return Issue::Unreadable;
					track.leaves.append({leafOwner, *valueOf(leafClass), track.length, *valueOf(length)});
					track.length += *valueOf(length);
				}
				if (*valueOf(rootClass) == "SEQU")
				{
					const auto declared = numberingScalar(image, *valueOf(component), "OMFI:CLIP:Length");
					if ((!missing(declared) && !valueOf(declared)) || (valueOf(declared) && *valueOf(declared) != track.length))
						return Issue::Unreadable;
				}
				result.append(std::move(track));
			}
			return result;
		}

		struct NumberingSelection
		{
			std::optional<bool> recorded;
			bool absent = false, unusable = false;

			void accept(bool drop)
			{
				if (recorded && *recorded != drop)
					unusable = true;
				recorded = drop;
			}
		};

		void numberingInterval(const Image &image, const QVector<NumberingTrack> &tracks,
							   qint64 begin, qint64 end, qint64 nominal, NumberingSelection &selection)
		{
			QVector<QPair<qint64, qint64>> covered;
			for (const auto &track : tracks)
			{
				if (track.kind != 3)
					continue;
				for (const auto &leaf : track.leaves)
				{
					const qint64 limit = leaf.begin + leaf.length;
					if (leaf.cls != "TCCP" || (begin == end ? begin < leaf.begin || begin > limit : begin >= limit || end <= leaf.begin))
						continue;
					const auto fps = numberingScalar(image, leaf.owner, "OMFI:TCCP:FPS");
					const auto flags = numberingScalar(image, leaf.owner, "OMFI:TCCP:Flags");
					if (!valueOf(fps) || *valueOf(fps) != nominal || !valueOf(flags))
					{
						selection.unusable = true;
						continue;
					}
					selection.accept((quint32(*valueOf(flags)) & 1u) != 0);
					covered.append({std::max(begin, leaf.begin), std::min(end, limit)});
				}
			}
			std::sort(covered.begin(), covered.end());
			qint64 position = begin;
			for (const auto &span : covered)
			{
				selection.absent |= span.first > position;
				position = std::max(position, span.second);
			}
			selection.absent |= covered.isEmpty() || position < end;
		}

		MdbNumbering numbering(const Image &image, quint32 fileOwner, const MdbFileRecord &file, const Owners &owners)
		{
			const auto cls = file.descriptorClass ? QByteArrayView(reinterpret_cast<const char *>(file.descriptorClass->data()), 4) : QByteArrayView{};
			const bool audio = audioDescriptor(cls);
			const bool picture = pictureDescriptor(cls);
			const auto &sourceClock = audio ? file.audioEditRate : file.sampleRate;
			if ((!audio && !picture) || !sourceClock)
				return MdbNumbering::Unavailable;
			const auto clock = numberingClock(*sourceClock);
			if (clock.numerator <= 0)
				return MdbNumbering::Unavailable;
			qint64 nominal = 0;
			if (sameNumberingClock(clock, {30000, 1001}))
				nominal = 30;
			else if (sameNumberingClock(clock, {60000, 1001}))
				nominal = 60;
			else
				return MdbNumbering::NotRequired;
			const auto unavailable = [&image, fileOwner]()
			{
				image.problem(fileOwner, "OMFI:TCCP:Flags", QStringLiteral("Required numbering cannot be established from a complete supported file/source, track, interval, clock and flag relationship."));
				return MdbNumbering::Unavailable;
			};
			const qint64 essenceKind = picture ? 1 : 2;
			const auto own = numberingTracks(image, fileOwner, essenceKind, clock);
			if (!valueOf(own))
				return unavailable();
			bool direct = false;
			for (const auto &track : *valueOf(own))
				if (track.kind == 3)
					for (const auto &leaf : track.leaves)
						direct |= leaf.cls == "TCCP";
			NumberingSelection selection;
			bool intervalFound = false;
			for (const auto &track : *valueOf(own))
			{
				if (track.kind != essenceKind)
					continue;
				if (!sameNumberingClock(track.clock, clock))
					return unavailable();
				if (direct)
				{
					intervalFound = true;
					numberingInterval(image, *valueOf(own), 0, track.length, nominal, selection);
					continue;
				}
				for (const auto &leaf : track.leaves)
				{
					if (leaf.cls != "SCLP")
						continue;
					intervalFound = true;
					const auto source = sourceIdentity(image, leaf.owner);
					if (!valueOf(source))
						return unavailable();
					if (!*valueOf(source))
					{
						selection.absent = true;
						continue;
					}
					const auto found = owners.constFind(**valueOf(source));
					QVector<quint32> candidates;
					if (found != owners.cend())
						for (const auto candidate : *found)
							if (candidate != fileOwner && sourceOwner(image, candidate))
								candidates.append(candidate);
					if (candidates.size() != 1)
						return unavailable();
					const auto sourceTrack = numberingScalar(image, leaf.owner, "OMFI:SCLP:SourceTrack");
					const auto sourcePosition = numberingScalar(image, leaf.owner, "OMFI:SCLP:SourcePosition");
					if (!valueOf(sourceTrack) || *valueOf(sourceTrack) <= 0 || !valueOf(sourcePosition) || *valueOf(sourcePosition) < 0 ||
						*valueOf(sourcePosition) > std::numeric_limits<qint64>::max() - leaf.length)
						return unavailable();
					const auto sourceTracks = numberingTracks(image, candidates.first(), essenceKind, clock);
					if (!valueOf(sourceTracks))
						return unavailable();
					const NumberingTrack *selected = nullptr;
					for (const auto &candidate : *valueOf(sourceTracks))
						if (candidate.kind == essenceKind && candidate.label && *candidate.label == *valueOf(sourceTrack))
						{
							if (selected)
								return unavailable();
							selected = &candidate;
						}
					const qint64 end = *valueOf(sourcePosition) + leaf.length;
					if (!selected || !sameNumberingClock(selected->clock, clock) || end > selected->length)
						return unavailable();
					numberingInterval(image, *valueOf(sourceTracks), *valueOf(sourcePosition), end, nominal, selection);
				}
			}
			if (direct && !intervalFound)
				return unavailable();
			if (selection.unusable || (selection.absent && selection.recorded && *selection.recorded))
				return unavailable();
			if (!selection.recorded || selection.absent)
				return MdbNumbering::MissingNdfFallback;
			return *selection.recorded ? MdbNumbering::RecordedDf : MdbNumbering::RecordedNdf;
		}

		void descriptorFacts(const Image &image, quint32 owner, quint32 descriptor, QByteArrayView cls,
							 MdbFileRecord &file)
		{
			file.descriptorClass = fixedBytes<4>(cls);
			file.ownDescriptorQualified = true;
			if (named(cls, {"PCMA", "AIFD", "WAVD"}))
				file.kind = MdbKind::Audio;
			else if (named(cls, {"CDCI", "RGBA", "MPGI"}))
				file.kind = MdbKind::Video;
			const auto length = integerValue(image, descriptor, "OMFI:MDFL:Length");
			if (length && *length >= 0)
				file.length = length;
			else if (length)
				image.problem(descriptor, "OMFI:MDFL:Length", QStringLiteral("Negative length is unavailable."));
			const auto rate = image.rate(descriptor, "OMFI:MDFL:SampleRate");
			if (valueOf(rate))
				file.sampleRate = *valueOf(rate);
			else if (!missing(rate))
				image.problem(descriptor, "OMFI:MDFL:SampleRate", QStringLiteral("Sample rate is unreadable, nonpositive or conflicting."));
			codingFacts(image, descriptor, file);
			if (audioDescriptor(cls))
			{
				const auto clock = image.rate(owner, "OMFI:CPNT:EditRate");
				if (valueOf(clock))
					file.audioEditRate = *valueOf(clock);
				else if (!missing(clock))
					image.problem(owner, "OMFI:CPNT:EditRate", QStringLiteral("Audio display clock is unreadable, nonpositive or conflicting."));
				if (cls == "PCMA")
				{
					const auto frequency = image.rate(descriptor, "OMFI:PCMA:AudioSamplingRate");
					if (valueOf(frequency))
						file.audioFrequency = valueOf(frequency)->words;
					else if (!missing(frequency))
						image.problem(descriptor, "OMFI:PCMA:AudioSamplingRate", QStringLiteral("Concrete audio frequency is unreadable, nonpositive or conflicting."));
					const auto bits = integerValue(image, descriptor, "OMFI:MDAU:BitsPerSample");
					if (bits && *bits > 0 && *bits <= 64)
					{
						file.bitDepth.bits = quint32(*bits);
						file.bitDepth.representation = MdbDepthRepresentation::Integer;
					}
					else if (bits)
						image.problem(descriptor, "OMFI:MDAU:BitsPerSample", QStringLiteral("Audio component precision is outside the supported range."));
					if (file.compression.codingState == MdbCodingState::Absent)
						file.compression.audioEncoding = MdbAudioEncoding::Pcm;
				}
				else if (cls == "WAVD" || cls == "AIFD")
				{
					const auto *name = cls == "WAVD" ? "OMFI:WAVD:Summary" : "OMFI:AIFD:Summary";
					const auto summary = image.bytes(descriptor, name, {"omfi:VarLenBytes", "omfi:DataValue"});
					if (valueOf(summary))
					{
						QStringList localNotes;
						if (cls == "WAVD")
							readMdbWaveSummary(*valueOf(summary), file, localNotes);
						else
							readMdbAiffSummary(*valueOf(summary), file, localNotes);
						for (const auto &note : localNotes)
							image.problem(descriptor, name, note);
					}
					else if (!missing(summary))
						image.problem(descriptor, name, QStringLiteral("Audio summary is unreadable or conflicting."));
				}
			}
			else if (pictureDescriptor(cls))
			{
				geometry(image, descriptor, file);
				pictureDepth(image, descriptor, file, cls);
			}
		}

		MdbCatalogue extract(const Image &image, bool details)
		{
			MdbCatalogue result;
			result.fileReferencesComplete = image.referenceClassesKnown();
			Owners owners;
			for (const auto object : image.activeMobs())
			{
				const auto id = image.identity(object);
				if (valueOf(id))
					owners[*valueOf(id)].append(object);
			}
			QVector<quint32> masterOwners;
			bool allMasterLinks = image.referenceClassesKnown();
			for (const auto object : image.activeMobs())
			{
				const auto id = image.identity(object);
				if (image.has(object, "OMFI:MOBJ:PhysicalMedia"))
				{
					const auto descriptor = target(image, object, "OMFI:MOBJ:PhysicalMedia");
					const auto cls = valueOf(descriptor) && *valueOf(descriptor) ? image.tag(*valueOf(descriptor)) : Read<QByteArrayView>(Issue::Unreadable);
					if (valueOf(cls) && named(*valueOf(cls), {"MDES", "MDTP", "MDFM"}))
						continue; // Original source descriptor; no physical media row.
					if (!valueOf(id))
					{
						result.fileReferencesComplete = false;
						image.problem(object, "OMFI:MOBJ:MobID", QStringLiteral("Active file identity is unavailable; file-reference coverage is incomplete."));
						continue;
					}
					MdbFileRecord file{*valueOf(id)};
					if (image.disputed(object) || !valueOf(cls) || !mediaDescriptor(*valueOf(cls)) || !image.bigEndian(*valueOf(descriptor)))
					{
						result.fileReferencesComplete = false;
						image.problem(object, "OMFI:MOBJ:PhysicalMedia", QStringLiteral("File ID is retained, but its own descriptor/context cannot be qualified."));
					}
					else
					{
						descriptorFacts(image, object, *valueOf(descriptor), *valueOf(cls), file);
						file.numbering = numbering(image, object, file, owners);
					}
					if (!image.disputed(object))
						selectProject(image, object, owners, file);
					result.files.append(std::move(file));
					continue;
				}
				const auto usage = image.integer(object, "OMFI:MOBJ:UsageCode");
				if (missing(usage) || (valueOf(usage) && *valueOf(usage) != 1 && *valueOf(usage) != 7))
					continue;
				if (!valueOf(usage) || !valueOf(id) || image.disputed(object))
				{
					allMasterLinks = false;
					image.problem(object, "OMFI:MOBJ:UsageCode", QStringLiteral("Potential master identity or ownership is unresolved; master relationships are incomplete."));
					continue;
				}
				MdbMasterRecord master{*valueOf(id)};
				const auto name = image.text(object, "OMFI:CPNT:Name");
				if (valueOf(name))
					master.clipName = *valueOf(name);
				else if (!missing(name))
					image.problem(object, "OMFI:CPNT:Name", QStringLiteral("Master name is unreadable or conflicting."));
				const auto attrs = ownerAttributes(image, object);
				master.originalBin = originalBin(image, object, attrs);
				master.type = *valueOf(usage) == 1 ? MdbType::Precompute : MdbType::Media;
				if (details && master.type == MdbType::Precompute)
					master.precomputeCategory = precomputeCategory(image, object, attrs);
				masterOwners.append(object);
				result.masters.append(std::move(master));
			}
			QHash<MobId, QVector<qsizetype>> filesById;
			for (qsizetype i = 0; i < result.files.size(); ++i)
				if (result.files[i].ownDescriptorQualified)
					filesById[result.files[i].fileMobId].append(i);
			for (qsizetype master = 0; master < masterOwners.size(); ++master)
			{
				const auto walk = components(image, masterOwners[master]);
				allMasterLinks &= walk.complete;
				for (const auto component : walk.objects)
				{
					const auto cls = image.tag(component);
					if (!valueOf(cls) || *valueOf(cls) != "SCLP")
						continue;
					const auto id = sourceIdentity(image, component);
					if (!valueOf(id))
					{
						allMasterLinks = false;
						image.problem(component, "OMFI:SCLP:SourceID", QStringLiteral("Master source identity is unreadable or conflicting; relationships are incomplete."));
						continue;
					}
					if (!*valueOf(id))
						continue;
					const auto matches = filesById.constFind(**valueOf(id));
					if (matches != filesById.cend())
						for (const auto index : *matches)
							if (!result.files[index].directMasterRecords.contains(master))
								result.files[index].directMasterRecords.append(master);
				}
			}
			for (auto &file : result.files)
				file.masterLinksComplete = allMasterLinks && file.ownDescriptorQualified;
			return result;
		}

		struct Stamp
		{
			std::optional<qint64> size;
			QDateTime modified;
		};
		Stamp observe(const QString &path)
		{
			const QFileInfo info(path);
			return info.exists() ? Stamp{info.size() >= 0 ? std::optional<qint64>(info.size()) : std::nullopt, info.lastModified()} : Stamp{};
		}

		bool cancelled(MdbReport &report, const std::atomic_bool &stop)
		{
			if (!stop.load(std::memory_order_relaxed))
				return false;
			report.reading = MdbReadResult::Cancelled;
			report.problemNotes.append(QStringLiteral("MDB reading cancelled at a whole-file or major parsing-stage boundary."));
			return true;
		}
	}

	MdbReport parseMdb(const QString &path, const std::atomic_bool &stopRequested, bool includePrecomputeDetails)
	{
		MdbReport report;
		report.path = path;
		if (cancelled(report, stopRequested))
			return report;
		QFile source(path);
		if (!source.open(QIODevice::ReadOnly))
		{
			report.reading = MdbReadResult::IoError;
			report.problemNotes.append(QStringLiteral("Cannot open MDB: %1").arg(source.errorString()));
			return report;
		}
		const auto before = observe(path);
		if (cancelled(report, stopRequested))
			return report;
		const QByteArray capture = source.readAll();
		const bool readFailed = source.error() != QFileDevice::NoError;
		if (readFailed)
			report.problemNotes.append(QStringLiteral("Cannot read complete MDB: %1").arg(source.errorString()));
		if (cancelled(report, stopRequested))
			return report;
		const auto after = observe(path);
		source.close();
		const bool sizes = before.size && after.size;
		const bool dates = before.modified.isValid() && after.modified.isValid();
		if ((sizes && before.size != after.size) || (dates && before.modified != after.modified))
		{
			report.reading = MdbReadResult::Changed;
			report.sizeDateCheck = MdbCheckResult::Changed;
			if (sizes && before.size != after.size)
				report.problemNotes.append(QStringLiteral("MDB size changed during capture from %1 to %2 bytes; its data is excluded.").arg(*before.size).arg(*after.size));
			if (dates && before.modified != after.modified)
				report.problemNotes.append(QStringLiteral("MDB modification time changed during capture from %1 to %2; its data is excluded.")
											   .arg(before.modified.toUTC().toString(Qt::ISODateWithMs), after.modified.toUTC().toString(Qt::ISODateWithMs)));
			return report;
		}
		report.sizeDateCheck = sizes && dates ? MdbCheckResult::Verified : MdbCheckResult::Unverified;
		if (!sizes)
			report.problemNotes.append(QStringLiteral("Size check unavailable before or after capture; otherwise usable metadata remains unverified."));
		if (!dates)
			report.problemNotes.append(QStringLiteral("Modification-time check unavailable before or after capture; otherwise usable metadata remains unverified."));
		const bool wrongSize = (before.size && capture.size() != *before.size) || (after.size && capture.size() != *after.size);
		if (readFailed || wrongSize)
		{
			report.reading = MdbReadResult::IoError;
			if (wrongSize)
				report.problemNotes.append(QStringLiteral("Captured %1 bytes, which does not match the available expected MDB size.").arg(capture.size()));
			return report;
		}
		if (cancelled(report, stopRequested))
			return report;
		Image image(capture, report.problemNotes);
		report.reading = image.establish();
		if (cancelled(report, stopRequested) || report.reading != MdbReadResult::Completed)
			return report;
		report.catalogue = extract(image, includePrecomputeDetails);
		cancelled(report, stopRequested);
		return report;
	}
}
