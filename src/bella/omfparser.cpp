#include "omfparser.h"
#include "audioformat_p.h"
#include "audioreader_p.h"
#include "avidtext.h"

#include <QByteArrayView>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringDecoder>
#include <QVector>
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
		bool equalValue(const OmfRate &a, const OmfRate &b)
		{
			return a.words.numerator == b.words.numerator && a.words.denominator == b.words.denominator &&
				   a.encoding == b.encoding;
		}
		bool equalValue(const Detail::AudioFormatValues &a, const Detail::AudioFormatValues &b)
		{
			return a.frequency == b.frequency && a.bits == b.bits && a.storageBits == b.storageBits && a.encoding == b.encoding;
		}

		quint16 littleHalf(const char *p) { return qFromLittleEndian<quint16>(p); }
		quint32 littleWord(const char *p) { return qFromLittleEndian<quint32>(p); }
		template <typename T>
		T number(const char *p, bool big) { return big ? qFromBigEndian<T>(p) : qFromLittleEndian<T>(p); }

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
			bool immediate = false;
		};
		struct Value
		{
			quint32 object = 0, property = 0, type = 0;
			quint16 generation = 0;
			qsizetype firstSpan = 0, spanCount = 0, size = 0;
			bool readable = true;
		};

		// Index the complete TOC once; fetch and join only requested metadata.
		class Image
		{
		public:
			Image(QFile &file, qint64 labelOffset, const std::atomic_bool &stop, QStringList &notes, QByteArray knownLabel)
				: m_file(file), m_stop(stop), m_knownLabel(std::move(knownLabel)), m_extent(file.size()),
				  m_labelOffset(labelOffset), m_notes(notes)
			{
			}

			OmfReadResult establish()
			{
				constexpr qsizetype labelSize = 24, entrySize = 24;
				if (m_extent < labelSize || m_labelOffset < 0 || m_labelOffset > m_extent - labelSize)
					return fail(OmfReadResult::Incomplete, QStringLiteral("OMF has no complete Bento label."));
				const qint64 labelOffset = m_labelOffset;
				const auto labelBytes = m_knownLabel.size() == labelSize ? std::optional<QByteArray>(std::move(m_knownLabel))
																		 : readFile(labelOffset, labelSize);
				if (!labelBytes)
					return failure();
				const char *label = labelBytes->constData();
				if (QByteArrayView(label, 8) != QByteArray::fromHex("a4434da5486472d7"))
					return fail(OmfReadResult::Malformed, QStringLiteral("OMF has no Bento signature at its final label."));
				if (littleHalf(label + 12) != 1 || littleHalf(label + 14) != 0)
					return fail(OmfReadResult::Unsupported, QStringLiteral("OMF requires the supported Bento 1.0 layout."));
				if (littleHalf(label + 8) & ~quint16(0x0101))
					return fail(OmfReadResult::Unsupported, QStringLiteral("OMF Bento label has unsupported flags."));
				m_tocOffset = littleWord(label + 16);
				const qsizetype tocSize = littleWord(label + 20);
				if (m_tocOffset > labelOffset || tocSize > labelOffset - m_tocOffset || tocSize % entrySize)
					return fail(OmfReadResult::Malformed,
								QStringLiteral("OMF table of contents is incomplete or outside its label."));
				const auto toc = readFile(m_tocOffset, tocSize);
				if (!toc)
					return failure();
				m_toc = *toc;
				m_values.reserve(tocSize / entrySize);
				m_spans.reserve(tocSize / entrySize);
				bool continuing = false;
				for (qsizetype position = 0; position < tocSize; position += entrySize)
				{
					const auto *p = m_toc.constData() + position;
					const quint32 object = littleWord(p), property = littleWord(p + 4), type = littleWord(p + 8);
					const quint16 generation = littleHalf(p + 20), flags = littleHalf(p + 22);
					if ((flags & ~quint16(3)) || ((property | type) & 0xff000000u))
						return fail(OmfReadResult::Unsupported,
									QStringLiteral("OMF has an unsupported Bento TOC extension at byte %1.")
										.arg(m_tocOffset + position));
					if ((object == 1 && property >= 8 && property <= 10) || property == 29 || type == 30)
						return fail(OmfReadResult::Unsupported,
									QStringLiteral("OMF contains Bento update instructions requiring "
												   "an unsupported update resolver."));
					if (continuing)
					{
						auto &previous = m_values.last();
						if (previous.object != object || previous.property != property || previous.type != type ||
							previous.generation != generation)
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
					const auto limit = immediate ? m_tocOffset + tocSize : internal ? labelOffset
																					: m_tocOffset;
					if ((immediate && length > 4) || offset > limit || length > limit - offset ||
						length > std::numeric_limits<qsizetype>::max() - value.size)
						value.readable = false;
					else
						value.size += length;
					m_spans.append({offset, length, immediate});
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
					const auto modern =
						field<QByteArrayView>(1, "OMFI:OOBJ:ObjClass",
											  [](QByteArrayView bytes, QByteArrayView type) -> Read<QByteArrayView>
											  {
												  return type == "omfi:ObjectTag" && bytes.size() == 4
															 ? Read<QByteArrayView>(bytes)
															 : Read<QByteArrayView>(Issue::Unreadable);
											  });
					if (valueOf(modern) && *valueOf(modern) == "HEAD")
						return fail(OmfReadResult::Unsupported,
									QStringLiteral("OMF uses the unsupported OMF 2 object-class layout."));
				}
				if (!valueOf(cls) || *valueOf(cls) != QByteArrayView("HEAD"))
					return fail(OmfReadResult::Malformed,
								QStringLiteral("OMF cannot establish its HEAD object and class dictionary."));
				const auto version = field<QByteArrayView>(1, "OMFI:Version",
														   [](QByteArrayView bytes, QByteArrayView type) -> Read<QByteArrayView>
														   {
															   if (type != "omfi:VersionType" || bytes.size() != 2)
																   return Issue::Unreadable;
															   return bytes;
														   });
				if (!valueOf(version))
					return fail(OmfReadResult::Malformed,
								QStringLiteral("OMF HEAD version is absent, unreadable or conflicting."));
				if (*valueOf(version) != QByteArrayView("\0\1", 2) && *valueOf(version) != QByteArrayView("\1\0", 2))
					return fail(OmfReadResult::Unsupported, QStringLiteral("OMF requires the legacy OMF 1 record layout."));
				const auto order = byteOrder(1);
				if (!valueOf(order))
					return fail(OmfReadResult::Malformed, QStringLiteral("OMF HEAD byte order is absent or conflicting."));
				m_metadataBig = *valueOf(order);
				const auto spine = references(1, "OMFI:ObjectSpine");
				if (!valueOf(spine))
					return fail(OmfReadResult::Malformed, QStringLiteral("OMF ObjectSpine does not establish a "
																		 "complete active catalogue."));
				for (const auto object : *valueOf(spine))
				{
					if (!object || !m_byObject.contains(object))
						return fail(OmfReadResult::Malformed,
									QStringLiteral("OMF ObjectSpine refers to an unavailable object."));
					const auto kind = tag(object);
					if (!valueOf(kind))
						return fail(OmfReadResult::Malformed,
									QStringLiteral("OMF ObjectSpine has an unresolved object class."));
					if (*valueOf(kind) == "MOBJ")
					{
						if (!m_activeSet.contains(object))
							m_active.append(object);
						m_activeSet.insert(object);
					}
					else if (named(*valueOf(kind), {"MDAT", "IDAT", "JPEG", "MPEG", "TIFF", "WAVE", "AIFC"}))
						m_media.append(object);
					else if (named(*valueOf(kind), {"HEAD", "TRAK", "TRKG", "SCLP", "SEQU", "FILL", "TCCP", "RSET", "PVOL",
													"NEST", "SLCT", "MGRP", "ERAT", "ATTR", "ATTB", "CDCI", "RGBA", "MPGI",
													"PCMA", "AIFD", "WAVD", "MDES", "MDTP", "MDFM", "JPED", "TIFD", "MPGA"}))
						return fail(OmfReadResult::Malformed, QStringLiteral("OMF ObjectSpine assigns an invalid class "
																			 "to an active catalogue owner."));
					else
					{
						m_referenceClassesKnown = false;
						problem(object, "OMFI:ObjID",
								QStringLiteral("Unknown active media class; file-reference "
											   "absence cannot be proved."));
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
						problem(1, "OMFI:MediaData",
								QStringLiteral("Optional media-data index does not agree with the active "
											   "catalogue; reference coverage is incomplete."));
					}
				}
				if (valueOf(mediaIndex))
				{
					const auto rows = m_byObject.constFind(1);
					const auto ids = m_propertyIds.constFind("OMFI:MediaData");
					if (rows != m_byObject.cend() && ids != m_propertyIds.cend())
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
								const auto cls = tag(object);
								if (!valueOf(cls))
									continue;
								const char *name = *valueOf(cls) == "WAVE"	 ? "OMFI:WAVE:MobID"
												   : *valueOf(cls) == "AIFC" ? "OMFI:AIFC:MobID"
												   : *valueOf(cls) == "TIFF" ? "OMFI:TIFF:MobID"
																			 : "OMFI:MDAT:MobID";
								const auto raw = this->bytes(object, name, {"omfi:UID"});
								if (!valueOf(raw) || valueOf(raw)->size() != 12)
									continue;
								const auto prefix =
									normalizeMobId(bytes->sliced(offset, 12), MobIdLayout::OmfUid12,
												   m_metadataBig ? ByteOrder::BigEndian : ByteOrder::LittleEndian);
								const auto actual = identity(object, name);
								if (!valueOf(actual) || !std::holds_alternative<MobId>(prefix) ||
									std::get<MobId>(prefix) != *valueOf(actual))
								{
									m_referenceClassesKnown = false;
									problem(object, "OMFI:MediaData",
											QStringLiteral(
												"Legacy media index identity conflicts with its legacy media owner identity."));
								}
							}
						}
				}
				return OmfReadResult::Completed;
			}

			template <typename T, typename Decoder>
			Read<T> locatedField(quint32 object, const char *name, Decoder decode, bool append = false) const
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
					if (!value.readable || m_badDictionary.contains(value.property) || m_badDictionary.contains(value.type) ||
						!m_types.contains(value.type))
					{
						bad = true;
						continue;
					}
					const auto decoded = decode(index, QByteArrayView(m_types[value.type]));
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

			template <typename T, typename Decoder>
			Read<T> field(quint32 object, const char *name, Decoder decode, bool append = false) const
			{
				return locatedField<T>(
					object, name,
					[&](qsizetype index, QByteArrayView type) -> Read<T>
					{
						const auto bytes = payload(index);
						return bytes ? decode(*bytes, type) : Read<T>(Issue::Unreadable);
					},
					append);
			}

			qsizetype valueSize(qsizetype index) const { return m_values[index].size; }
			OmfReadResult failure() const { return m_failure.value_or(OmfReadResult::Completed); }
			const QVector<quint32> &activeMedia() const { return m_media; }

			std::optional<QByteArray> readValue(qsizetype index, qsizetype begin, qsizetype length) const
			{
				const auto &value = m_values[index];
				if (!value.readable || begin < 0 || length < 0 || begin > value.size || length > value.size - begin)
					return std::nullopt;
				QByteArray bytes;
				bytes.reserve(length);
				qsizetype skip = begin;
				for (qsizetype i = 0; i < value.spanCount && bytes.size() < length; ++i)
				{
					const auto &span = m_spans[value.firstSpan + i];
					if (skip >= span.length)
					{
						skip -= span.length;
						continue;
					}
					const auto count = std::min(length - bytes.size(), span.length - skip);
					if (span.immediate)
						bytes.append(m_toc.constData() + span.offset - m_tocOffset + skip, count);
					else
					{
						const auto part = readFile(span.offset + skip, count);
						if (!part)
							return std::nullopt;
						bytes += *part;
					}
					skip = 0;
				}
				return bytes.size() == length ? std::optional<QByteArray>(bytes) : std::nullopt;
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
				return field<QByteArrayView>(object, "OMFI:ObjID",
											 [](QByteArrayView bytes, QByteArrayView type) -> Read<QByteArrayView>
											 {
												 return bytes.size() == 4 && named(type, {"omfi:ObjectTag", "omfi:ClassID"})
															? Read<QByteArrayView>(bytes)
															: Read<QByteArrayView>(Issue::Unreadable);
											 });
			}

			Read<bool> byteOrder(quint32 object) const
			{
				return field<bool>(object, "OMFI:ByteOrder",
								   [](QByteArrayView bytes, QByteArrayView type) -> Read<bool>
								   {
									   if (!named(type, {"omfi:Short", "omfi:Int16"}) || (bytes != "II" && bytes != "MM"))
										   return Issue::Unreadable;
									   return bytes == "MM";
								   });
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
				return field<qint64>(
					object, name,
					[big, name](QByteArrayView bytes, QByteArrayView type) -> Read<qint64>
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
						if (QByteArrayView(name) == "OMFI:MOBJ:UsageCode" &&
							(bytes.size() != 4 || type != "omfi:UsageCodeType"))
							return Issue::Unreadable;
						if (named(name, {"OMFI:CDCI:HorizontalSubsampling", "OMFI:CDCI:VerticalSubsampling"}) &&
							(bytes.size() != 4 ||
							 !named(type, {"omfi:Ulong", "omfi:ULong", "omfi:UInt32", "omfi:Long", "omfi:Int32"})))
							return Issue::Unreadable;
						if (named(name, {"OMFI:CDCI:ComponentWidth", "OMFI:TCCP:Flags", "OMFI:TCCP:StartTC",
										 "OMFI:SCLP:SourcePosition", "OMFI:ATTB:BobSize", "OMFI:DIDD:DIDResolutionID",
										 "OMFI:DIDD:StoredWidth", "OMFI:DIDD:StoredHeight", "OMFI:DIDD:DisplayWidth",
										 "OMFI:DIDD:DisplayHeight", "OMFI:DIDD:DisplayXOffset", "OMFI:DIDD:DisplayYOffset",
										 "OMFI:DIDD:SampledWidth", "OMFI:DIDD:SampledHeight", "OMFI:DIDD:SampledXOffset",
										 "OMFI:DIDD:SampledYOffset"}) &&
							(bytes.size() != 4 || !named(type, {"omfi:Long", "omfi:Int32"})))
							return Issue::Unreadable;
						if (bytes.size() == 2 && named(type, {"omfi:Short", "omfi:Int16", "omfi:TrackType",
															  "omfi:UsageCodeType", "omfi:AttrKind", "omfi:LayoutType"}))
							return number<qint16>(bytes.data(), *big);
						if (bytes.size() == 2 && named(type, {"omfi:UInt16", "omfi:Ushort", "omfi:UShort"}))
							return number<quint16>(bytes.data(), *big);
						if (bytes.size() == 4 &&
							named(type, {"omfi:Long", "omfi:Int32", "omfi:Length32", "omfi:Position32", "omfi:UsageCodeType"}))
							return number<qint32>(bytes.data(), *big);
						if (bytes.size() == 4 && named(type, {"omfi:UInt32", "omfi:Ulong", "omfi:ULong"}))
							return number<quint32>(bytes.data(), *big);
						if (bytes.size() == 8 && named(type, {"omfi:Int64", "omfi:Length64", "omfi:Position64"}))
							return number<qint64>(bytes.data(), *big);
						return Issue::Unreadable;
					});
			}

			Read<OmfRate> rate(quint32 object, const char *name) const
			{
				const auto big = bigEndian(object);
				return field<OmfRate>(
					object, name,
					[big](QByteArrayView bytes, QByteArrayView type) -> Read<OmfRate>
					{
						if (!big || bytes.size() != 8 || !named(type, {"omfi:Rational", "omfi:ExactEditRate"}))
							return Issue::Unreadable;
						const OmfRateWords words{number<qint32>(bytes.data(), *big), number<qint32>(bytes.data() + 4, *big)};
						if (words.numerator <= 0 || words.denominator <= 0)
							return Issue::Unreadable;
						return OmfRate{words, type == "omfi:ExactEditRate" ? OmfRateEncoding::ExactEditRate
																		   : OmfRateEncoding::Rational};
					});
			}

			Read<MobId> identity(quint32 object, const char *name = "OMFI:MOBJ:MobID") const
			{
				const auto big = bigEndian(object);
				return field<MobId>(object, name,
									[big](QByteArrayView bytes, QByteArrayView type) -> Read<MobId>
									{
										if (!big || type != "omfi:UID" || (bytes.size() != 12 && bytes.size() != 32) ||
											std::all_of(bytes.begin(), bytes.end(), [](char byte)
														{ return byte == 0; }))
											return Issue::Unreadable;
										const auto id = normalizeMobId(
											bytes, bytes.size() == 12 ? MobIdLayout::OmfUid12 : MobIdLayout::Aaf32,
											*big ? ByteOrder::BigEndian : ByteOrder::LittleEndian);
										return std::holds_alternative<MobId>(id) ? Read<MobId>(std::get<MobId>(id))
																				 : Read<MobId>(Issue::Unreadable);
									});
			}

			Read<QString> text(quint32 object, const char *name, bool utf8 = false) const
			{
				return field<QString>(object, name,
									  [utf8](QByteArrayView bytes, QByteArrayView type) -> Read<QString>
									  {
										  return named(type, {"omfi:String", "omfi:VarLenBytes"})
													 ? textValue(bytes, utf8)
													 : Read<QString>(Issue::Unreadable);
									  });
			}

			Read<QVector<quint32>> references(quint32 object, const char *name) const
			{
				const auto big = bigEndian(object);
				const bool index = named(name, {"OMFI:SourceMobs", "OMFI:CompositionMobs", "OMFI:MediaData"});
				const bool array =
					named(name, {"OMFI:ObjectSpine", "OMFI:TRKG:Tracks", "OMFI:SEQU:Sequence", "OMFI:SEQU:Components",
								 "OMFI:ATTR:AttrRefs", "OMFI:NEST:Slots", "OMFI:SLCT:Alternates", "OMFI:MGRP:Choices"});
				return field<QVector<quint32>>(
					object, name,
					[big, index, array](QByteArrayView bytes, QByteArrayView type) -> Read<QVector<quint32>>
					{
						if (!big)
							return Issue::Unreadable;
						if (type != (index ? "omfi:MobIndex" : array ? "omfi:ObjRefArray"
																	 : "omfi:ObjRef"))
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
						return targets;
					},
					index || array);
			}

			Read<QByteArrayView> bytes(quint32 object, const char *name, std::initializer_list<const char *> types) const
			{
				return field<QByteArrayView>(
					object, name, [types](QByteArrayView bytes, QByteArrayView type) -> Read<QByteArrayView>
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
					return; // Legacy ObjectSpine is sufficient when optional indexes are
							// absent.
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
					problem(1, name,
							QStringLiteral("Optional catalogue index does not safely agree with "
										   "ObjectSpine; affected ownership is withheld."));
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
							problem(object, name,
									QStringLiteral("Legacy index identity conflicts with its "
												   "legacy owner identity."));
						}
					}
				}
			}

			OmfReadResult fail(OmfReadResult result, const QString &reason)
			{
				m_notes.append(reason);
				return result;
			}
			std::optional<QByteArray> readFile(qint64 offset, qint64 length) const
			{
				if (m_failure)
					return std::nullopt;
				const auto stopped = [&]
				{
					if (!m_stop.load(std::memory_order_relaxed))
						return false;
					m_failure = OmfReadResult::Cancelled;
					m_notes.append(QStringLiteral("OMF reading cancelled at a file-read boundary."));
					return true;
				};
				if (stopped())
					return std::nullopt;
				if (offset < 0 || length < 0 || offset > m_extent || length > m_extent - offset)
					return std::nullopt;
				if (!m_file.seek(offset))
				{
					m_failure = OmfReadResult::IoError;
					m_notes.append(QStringLiteral("Cannot seek to OMF metadata: %1").arg(m_file.errorString()));
					return std::nullopt;
				}
				if (stopped())
					return std::nullopt;
				const QByteArray bytes = m_file.read(length);
				const bool ioError = m_file.error() != QFileDevice::NoError;
				if (stopped())
					return std::nullopt;
				if (ioError || bytes.size() != length)
				{
					m_failure = ioError ? OmfReadResult::IoError : OmfReadResult::Incomplete;
					m_notes.append(
						QStringLiteral("Cannot read the complete requested OMF metadata range: %1").arg(m_file.errorString()));
					return std::nullopt;
				}
				return bytes;
			}
			std::optional<QByteArrayView> payload(qsizetype index) const
			{
				if (!m_values[index].readable)
					return std::nullopt;
				if (!m_joined.contains(index))
				{
					const auto bytes = readValue(index, 0, m_values[index].size);
					if (!bytes)
						return std::nullopt;
					m_joined.insert(index, *bytes);
				}
				return QByteArrayView(m_joined[index]);
			}

			QFile &m_file;
			const std::atomic_bool &m_stop;
			QByteArray m_toc;
			QByteArray m_knownLabel;
			mutable std::optional<OmfReadResult> m_failure;
			qint64 m_extent = 0, m_labelOffset = 0;
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

		// These known format/unit meanings are independent of the Kind rule.
		bool audioDescriptor(QByteArrayView tag) { return named(tag, {"PCMA", "AIFD", "WAVD", "MPGA", "WAVE"}); }

		bool pictureDescriptor(QByteArrayView tag) { return named(tag, {"CDCI", "RGBA", "MPGI", "JPED", "TIFD"}); }

		bool mediaDescriptor(QByteArrayView tag) { return audioDescriptor(tag) || pictureDescriptor(tag); }

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

		bool componentClass(QByteArrayView cls)
		{
			return named(cls, {"TRKG", "SCLP", "SEQU", "FILL", "TCCP", "RSET", "PVOL", "NEST", "SLCT", "MGRP", "ERAT"});
		}

		Read<quint32> trackComponent(const Image &image, quint32 track)
		{
			const auto cls = image.tag(track);
			if (!valueOf(cls) || *valueOf(cls) != "TRAK")
			{
				image.problem(track, "OMFI:ObjID", QStringLiteral("Track reference is not a qualified TRAK object."));
				return Issue::Unreadable;
			}
			const auto component = target(image, track, "OMFI:TRAK:TrackComponent");
			if (!valueOf(component) || !*valueOf(component))
			{
				image.problem(track, "OMFI:TRAK:TrackComponent",
							  QStringLiteral("Required track component is missing, null, "
											 "unreadable or conflicting."));
				return Issue::Unreadable;
			}
			const auto componentTag = image.tag(*valueOf(component));
			if (!valueOf(componentTag) || !componentClass(*valueOf(componentTag)))
			{
				image.problem(*valueOf(component), "OMFI:ObjID",
							  QStringLiteral("Track component is not a supported component object."));
				return Issue::Unreadable;
			}
			return *valueOf(component);
		}

		struct Walk
		{
			QVector<quint32> objects;
			bool complete = true;
		};

		enum class TargetRole
		{
			Component,
			Track,
			SourceClip
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
					image.problem(item.first, "component graph",
								  QStringLiteral("Component cycle leaves relationship coverage incomplete."));
					continue;
				}
				if (colour.value(item.first) == 2)
					continue;
				colour[item.first] = 1;
				pending.append({item.first, true});
				const auto cls = image.tag(item.first);
				const bool rootMob = item.first == root && valueOf(cls) && *valueOf(cls) == "MOBJ";
				if (!valueOf(cls) || (!componentClass(*valueOf(cls)) && *valueOf(cls) != "TRAK" && !rootMob))
				{
					result.complete = false;
					image.problem(item.first, "OMFI:ObjID",
								  QStringLiteral("Component class is unknown or unreadable; "
												 "relationship coverage is incomplete."));
					continue;
				}
				result.objects.append(item.first);
				const auto follow = [&](const char *name, bool required, TargetRole role = TargetRole::Component)
				{
					const auto refs = image.references(item.first, name);
					if (const auto *targets = valueOf(refs))
						for (auto it = targets->crbegin(); it != targets->crend(); ++it)
						{
							const auto targetTag = *it ? image.tag(*it) : Read<QByteArrayView>(Issue::Unreadable);
							const bool qualified =
								valueOf(targetTag) && (role == TargetRole::Track		? *valueOf(targetTag) == "TRAK"
													   : role == TargetRole::SourceClip ? *valueOf(targetTag) == "SCLP"
																						: componentClass(*valueOf(targetTag)));
							if (!qualified)
							{
								result.complete = false;
								image.problem(item.first, name,
											  QStringLiteral("Component reference has a null, unreadable or wrong-class "
															 "target; dependent relationships are incomplete."));
								continue;
							}
							pending.append({*it, false});
						}
					else if (missing(refs))
					{
						if (required)
						{
							result.complete = false;
							image.problem(item.first, "component graph", QStringLiteral("Required component link is missing."));
						}
					}
					else
					{
						result.complete = false;
						image.problem(item.first, name,
									  QStringLiteral("Component references are unreadable or conflicting; "
													 "dependent relationships are incomplete."));
					}
				};
				const auto kind = *valueOf(cls);
				if (kind == "MOBJ")
				{
					const auto usage = image.integer(item.first, "OMFI:MOBJ:UsageCode");
					follow("OMFI:TRKG:Tracks", valueOf(usage) && (*valueOf(usage) == 1 || *valueOf(usage) == 7),
						   TargetRole::Track);
				}
				else if (named(kind, {"TRKG", "RSET", "PVOL"}) || (kind == "SLCT" && image.has(item.first, "OMFI:TRKG:Tracks")))
					follow("OMFI:TRKG:Tracks", true, TargetRole::Track);
				else if (kind == "TRAK")
				{
					const auto component = trackComponent(image, item.first);
					if (valueOf(component))
						pending.append({*valueOf(component), false});
					else
						result.complete = false;
				}
				else if (kind == "SEQU")
				{
					follow("OMFI:SEQU:Sequence", true);
				}
				else if (kind == "NEST")
					follow("OMFI:NEST:Slots", true);
				else if (kind == "SLCT")
				{
					follow("OMFI:SLCT:Selected", true);
					follow("OMFI:SLCT:Alternates", false);
				}
				else if (kind == "MGRP")
					follow("OMFI:MGRP:Choices", true, TargetRole::SourceClip);
				else if (kind == "ERAT")
					follow("OMFI:ERAT:InputSegment", true);
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
				{
					image.problem(*valueOf(list), "OMFI:ATTR:AttrRefs",
								  QStringLiteral("Attribute list contains a null entry; "
												 "dependent metadata is unavailable."));
					return Issue::Unreadable;
				}
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
			OmfProjectKey key = OmfProjectKey::None;
			bool conflict = false, complete = true;
		};

		Project ownProject(const Image &image, quint32 owner, const Read<AttributeList> &attrs)
		{
			const std::array<QPair<const char *, OmfProjectKey>, 2> keys{
				{{"_PJ", OmfProjectKey::Pj}, {"PROJNAME", OmfProjectKey::ProjName}}};
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
				return {{}, OmfProjectKey::None, false, missing(users)};
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
					result.key = OmfProjectKey::UserPj;
				}
			}
			if (result.conflict || !result.complete)
				result.text.clear();
			return result;
		}

		Read<QString> originalBin(const Image &image, quint32 master, const Read<AttributeList> &attrs)
		{
			const auto entries = attributes(attrs, QStringLiteral("_ORG_BIN"));
			if (!valueOf(entries))
			{
				if (!missing(entries))
					image.problem(master, "_ORG_BIN", QStringLiteral("Direct original-bin attributes cannot be established."));
				return std::get<Issue>(entries);
			}
			std::optional<QString> chosen;
			for (const auto entry : *valueOf(entries))
			{
				const auto kind = image.integer(entry, "OMFI:ATTB:Kind");
				const auto bin = target(image, entry, "OMFI:ATTB:ObjAttribute");
				if (!valueOf(kind) || *valueOf(kind) != 3 || !valueOf(bin))
				{
					image.problem(entry, "_ORG_BIN",
								  QStringLiteral("Original-bin attribute is not a readable object reference."));
					return Issue::Unreadable;
				}
				if (!*valueOf(bin))
					continue;
				const auto cls = image.tag(*valueOf(bin));
				if (!valueOf(cls) || *valueOf(cls) != "MCBR")
				{
					image.problem(*valueOf(bin), "OMFI:ObjID",
								  QStringLiteral("Original-bin target is not a qualified MCBR record."));
					return Issue::Unreadable;
				}
				const auto modern = image.text(*valueOf(bin), "OMFI:MCBR:MC:binNameUTF8", true);
				if (const auto *issue = std::get_if<Issue>(&modern); issue && *issue == Issue::Conflict)
				{
					image.problem(*valueOf(bin), "OMFI:MCBR:MC:binNameUTF8",
								  QStringLiteral("Conflicting Unicode bin names; bin is unavailable."));
					return Issue::Conflict;
				}
				QString name;
				if (valueOf(modern) && !valueOf(modern)->trimmed().isEmpty())
					name = *valueOf(modern);
				else
				{
					if (!missing(modern) && !valueOf(modern))
						image.problem(*valueOf(bin), "OMFI:MCBR:MC:binNameUTF8",
									  QStringLiteral("Invalid Unicode bin name; trying its separate legacy counterpart."));
					const auto legacy = image.text(*valueOf(bin), "OMFI:MCBR:MC:binName");
					if (!valueOf(legacy))
					{
						if (!missing(legacy) || (!missing(modern) && !valueOf(modern)))
							return Issue::Unreadable;
						continue;
					}
					name = *valueOf(legacy);
				}
				if (name.trimmed().isEmpty())
					continue;
				if (chosen && *chosen != name)
				{
					image.problem(master, "_ORG_BIN",
								  QStringLiteral("Conflicting direct original-bin names; bin is unavailable."));
					return Issue::Conflict;
				}
				chosen = name;
			}
			return chosen ? Read<QString>(*chosen) : Read<QString>(Issue::Missing);
		}

		OmfPrecomputeCategory precomputeCategory(const Image &image, quint32 master, const Read<AttributeList> &attrs)
		{
			const auto imports = attributes(attrs, QStringLiteral("_IMPORTSETTING"));
			if (!valueOf(imports))
				return missing(imports) ? OmfPrecomputeCategory::RenderedEffects : OmfPrecomputeCategory::Unknown;
			if (valueOf(imports)->isEmpty())
				return OmfPrecomputeCategory::RenderedEffects;
			for (const auto entry : *valueOf(imports))
			{
				const auto kind = image.integer(entry, "OMFI:ATTB:Kind");
				// Preserve duplicate-entry handling; only one known non-object entry proves
				// this absence.
				if (const auto *knownKind = valueOf(kind);
					knownKind && valueOf(imports)->size() == 1 && (*knownKind == 1 || *knownKind == 2 || *knownKind == 4))
					return OmfPrecomputeCategory::RenderedEffects;
				if (!valueOf(kind) || *valueOf(kind) != 3 || !valueOf(target(image, entry, "OMFI:ATTB:ObjAttribute")))
					return OmfPrecomputeCategory::Unknown;
			}
			const auto tracks = image.references(master, "OMFI:TRKG:Tracks");
			if (!valueOf(tracks))
				return OmfPrecomputeCategory::Unknown;
			qsizetype video = 0;
			for (const auto track : *valueOf(tracks))
			{
				const auto component = trackComponent(image, track);
				if (!valueOf(component))
					return OmfPrecomputeCategory::Unknown;
				const auto kind = image.integer(*valueOf(component), "OMFI:CPNT:TrackKind");
				if (!valueOf(kind))
					return OmfPrecomputeCategory::Unknown;
				video += *valueOf(kind) == 1;
			}
			return video >= 2 ? OmfPrecomputeCategory::TitlesAndMatteKeys : OmfPrecomputeCategory::RenderedEffects;
		}

		std::optional<qint64> integerValue(const Image &image, quint32 object, const char *name)
		{
			const auto read = image.integer(object, name);
			if (!valueOf(read) && !missing(read))
				image.problem(object, name,
							  QStringLiteral("Numeric value has an unsupported type, incomplete "
											 "width, byte order or conflicting values."));
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

		void codingFacts(const Image &image, quint32 descriptor, OmfFileInformation &file)
		{
			const auto coded = image.bytes(descriptor, "OMFI:DIDD:EssenceCompression", {"omfi:GUID"});
			const auto big = valueOf(coded) && valueOf(coded)->size() == 16 ? image.bigEndian(descriptor) : std::nullopt;
			if (missing(coded))
				file.compression.codingState = OmfCodingState::Absent;
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
				file.compression.codingState = OmfCodingState::Present;
			}
			else
				image.problem(descriptor, "OMFI:DIDD:EssenceCompression",
							  QStringLiteral("Coding identity is unavailable; it is not established absent."));
			const auto compression = image.bytes(descriptor, "OMFI:DIDD:Compression", {"omfi:String"});
			if (const auto *bytes = valueOf(compression))
			{
				if (bytes->size() == 4 || (bytes->size() == 5 && bytes->back() == '\0'))
					file.compression.legacyCompression = fixedBytes<4>(bytes->first(4));
				else
					image.problem(descriptor, "OMFI:DIDD:Compression",
								  QStringLiteral("Legacy compression identifier is not a "
												 "complete four-byte code."));
			}
			else if (!missing(compression))
				image.problem(descriptor, "OMFI:DIDD:Compression",
							  QStringLiteral("Legacy compression identifier has an "
											 "unreadable or conflicting typed value."));
			file.compression.resolutionId = integerValue(image, descriptor, "OMFI:DIDD:DIDResolutionID");
			const auto subsampling = [&](const char *name) -> std::optional<qint32>
			{
				const auto value = integerValue(image, descriptor, name);
				if (value && *value > 0 && *value <= std::numeric_limits<qint32>::max())
					return qint32(*value);
				if (value)
					image.problem(descriptor, name,
								  QStringLiteral("Subsampling is outside its supported positive integer range."));
				return std::nullopt;
			};
			file.compression.horizontalSubsampling = subsampling("OMFI:CDCI:HorizontalSubsampling");
			file.compression.verticalSubsampling = subsampling("OMFI:CDCI:VerticalSubsampling");
		}

		std::optional<qint64> dimension(const Image &image, quint32 owner, const char *name,
										std::optional<qint64> fallback = {})
		{
			const auto read = image.integer(owner, name);
			if (missing(read))
				return fallback;
			return optionalValue(read);
		}

		void geometry(const Image &image, quint32 descriptor, OmfFileInformation &file)
		{
			const auto width = dimension(image, descriptor, "OMFI:DIDD:StoredWidth");
			const auto height = dimension(image, descriptor, "OMFI:DIDD:StoredHeight");
			const auto layout = dimension(image, descriptor, "OMFI:DIDD:FrameLayout");
			if (layout && *layout >= 0 && *layout <= 3)
				file.compression.frameLayout = quint16(*layout);
			if (!width || !height || !layout || *width <= 0 || *height <= 0 || *layout < 0 || *layout > 3)
				return;
			const qint64 multiplier = (*layout == 1 || *layout == 3) ? 2 : 1;
			if (*height > std::numeric_limits<qint64>::max() / multiplier)
				return;
			file.compression.namingGeometry = OmfDimensions{*width, *height * multiplier};
			const auto displayWidth = dimension(image, descriptor, "OMFI:DIDD:DisplayWidth", width);
			const auto displayHeight = dimension(image, descriptor, "OMFI:DIDD:DisplayHeight", height);
			const auto x = dimension(image, descriptor, "OMFI:DIDD:DisplayXOffset", 0);
			const auto y = dimension(image, descriptor, "OMFI:DIDD:DisplayYOffset", 0);
			if (!displayWidth || !displayHeight || !x || !y)
			{
				image.problem(descriptor, "OMFI:DIDD:DisplayWidth",
							  QStringLiteral("Visible geometry has unreadable or "
											 "conflicting rectangle fields."));
				return;
			}
			if (*displayWidth > 0 && *displayHeight > 0 && *x >= 0 && *y >= 0 && *displayWidth <= *width &&
				*displayHeight <= *height && *x <= *width - *displayWidth && *y <= *height - *displayHeight)
			{
				const bool recorded =
					image.has(descriptor, "OMFI:DIDD:DisplayWidth") || image.has(descriptor, "OMFI:DIDD:DisplayHeight") ||
					image.has(descriptor, "OMFI:DIDD:DisplayXOffset") || image.has(descriptor, "OMFI:DIDD:DisplayYOffset");
				file.resolution = OmfRaster{{*displayWidth, *displayHeight * multiplier},
											recorded ? OmfRasterOrigin::Display : OmfRasterOrigin::Stored};
				return;
			}
			// This is the finite, evidenced proxy table; a small raster alone is not a
			// proxy.
			struct Proxy
			{
				qint64 id, width, height, displayWidth, displayHeight, layout;
			};
			constexpr std::array<Proxy, 7> proxies{{{3472, 480, 270, 1920, 540, 2},
													{3484, 480, 270, 1920, 1080, 0},
													{3487, 480, 270, 1920, 1080, 0},
													{3488, 320, 180, 1280, 720, 0},
													{3470, 352, 240, 720, 240, 2},
													{3491, 352, 240, 720, 240, 2},
													{3483, 352, 288, 720, 576, 0}}};
			const auto expected = QByteArray::fromHex("060e2b340401010d0401020201311101");
			const auto &facts = file.compression;
			if (facts.codingState == OmfCodingState::Present && facts.resolutionId &&
				QByteArrayView(reinterpret_cast<const char *>(facts.codingLabel.data()), 16) == expected && *x == 0 && *y == 0)
			{
				const auto sw = dimension(image, descriptor, "OMFI:DIDD:SampledWidth");
				const auto sh = dimension(image, descriptor, "OMFI:DIDD:SampledHeight");
				const auto sx = dimension(image, descriptor, "OMFI:DIDD:SampledXOffset", 0);
				const auto sy = dimension(image, descriptor, "OMFI:DIDD:SampledYOffset", 0);
				for (const auto &proxy : proxies)
					if (*facts.resolutionId == proxy.id && *layout == proxy.layout && *width == proxy.width &&
						*height == proxy.height && *displayWidth == proxy.displayWidth &&
						*displayHeight == proxy.displayHeight && sw == displayWidth && sh == displayHeight && sx == 0 &&
						sy == 0)
					{
						file.resolution = OmfRaster{*facts.namingGeometry, OmfRasterOrigin::VerifiedProxy};
						return;
					}
			}
			image.problem(descriptor, "OMFI:DIDD:DisplayWidth",
						  QStringLiteral("Visible rectangle is outside Stored and is not "
										 "a verified proxy configuration."));
		}

		void pictureDepth(const Image &image, quint32 descriptor, OmfFileInformation &file, QByteArrayView cls)
		{
			if (cls == "RGBA")
			{
				const auto codes = image.bytes(descriptor, "OMFI:RGBA:PixelLayout", {"omfi:VarLenBytes", "omfi:CompCodeArray"});
				const auto widths =
					image.bytes(descriptor, "OMFI:RGBA:PixelStructure", {"omfi:VarLenBytes", "omfi:CompSizeArray"});
				if (!valueOf(codes) || !valueOf(widths) || valueOf(codes)->isEmpty() || valueOf(codes)->size() > 8 ||
					valueOf(codes)->size() != valueOf(widths)->size())
				{
					image.problem(descriptor, "OMFI:RGBA:PixelLayout",
								  QStringLiteral("RGBA component arrays do not establish a "
												 "complete supported layout."));
					return;
				}
				std::optional<quint32> common;
				QSet<char> seen;
				bool ended = false;
				for (qsizetype i = 0; i < valueOf(codes)->size(); ++i)
				{
					const char code = (*valueOf(codes))[i];
					const auto bits = quint8((*valueOf(widths))[i]);
					if (!code)
						ended = true;
					if (ended)
					{
						if (code || bits)
							return;
						continue;
					}
					if (code == 'F' || code == '0')
						continue;
					if (!QByteArrayView("RGBA").contains(code) || seen.contains(code) || bits == 0 || bits > 64 ||
						(common && *common != bits))
					{
						image.problem(descriptor, "OMFI:RGBA:PixelStructure",
									  QStringLiteral("RGBA has no single supported common component depth."));
						return;
					}
					seen.insert(code);
					common = bits;
				}
				file.bitDepth.bits = common;
				if (common)
					file.bitDepth.representation = OmfDepthRepresentation::Integer;
				const std::array<quint8, 4> none{{'N', 'O', 'N', 'E'}};
				if (common && seen.size() == 1 && seen.contains('A') &&
					file.compression.codingState == OmfCodingState::Absent &&
					file.compression.legacyCompression == std::optional<std::array<quint8, 4>>(none))
					file.compression.alphaDepth = quint16(*common);
				return;
			}
			if (cls == "TIFD")
				return;
			const auto raw = integerValue(image, descriptor, "OMFI:CDCI:ComponentWidth");
			if (!raw || *raw < std::numeric_limits<qint32>::min() || *raw > std::numeric_limits<qint32>::max())
				return;
			file.bitDepth.rawComponentWidth = qint32(*raw);
			const auto coding = QByteArrayView(reinterpret_cast<const char *>(file.compression.codingLabel.data()), 16);
			const bool standard = file.compression.codingState == OmfCodingState::Present &&
								  coding == QByteArray::fromHex("060e2b340401010d0401020203070100");
			const bool fixed = file.compression.codingState == OmfCodingState::Present &&
							   coding == QByteArray::fromHex("060e2b340401010d0401020203070200");
			if (*raw == 253 && standard)
			{
				file.bitDepth.bits = 16;
				file.bitDepth.representation = OmfDepthRepresentation::HalfFloat;
			}
			else if (*raw == 254 && (standard || fixed))
			{
				file.bitDepth.bits = standard ? 32 : 16;
				file.bitDepth.representation = standard ? OmfDepthRepresentation::Float : OmfDepthRepresentation::S2_14Fixed;
			}
			else if (fixed && (*raw == 10 || *raw == 12))
			{
				file.bitDepth.representation =
					*raw == 10 ? OmfDepthRepresentation::Fixed10_6 : OmfDepthRepresentation::Fixed12_4;
			}
			else if (*raw > 0 && *raw <= 64)
			{
				file.bitDepth.bits = quint32(*raw);
				file.bitDepth.representation = OmfDepthRepresentation::Integer;
			}
			else
				image.problem(descriptor, "OMFI:CDCI:ComponentWidth",
							  QStringLiteral("Component width has no supported interpretation in its "
											 "complete coding context."));
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
			return valueOf(id) ? Read<std::optional<MobId>>(std::optional<MobId>(*valueOf(id)))
							   : Read<std::optional<MobId>>(std::get<Issue>(id));
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

		void selectProject(const Image &image, quint32 owner, const Owners &owners, OmfFileInformation &file)
		{
			const auto attrs = ownerAttributes(image, owner);
			if (!valueOf(attrs) && !missing(attrs))
			{
				if (std::get<Issue>(attrs) == Issue::Conflict)
				{
					file.projectSelection = OmfProjectSelection::Conflict;
					image.problem(owner, "OMFI:CPNT:Attributes",
								  QStringLiteral("Project attributes conflict; Project is left blank "
												 "without substituting a linked record."));
				}
				else
					image.problem(owner, "OMFI:CPNT:Attributes",
								  QStringLiteral("Project attributes cannot be read; Project is left "
												 "blank without substituting a linked record."));
				return;
			}
			auto selected = ownProject(image, owner, attrs);
			if (selected.conflict)
			{
				file.projectSelection = OmfProjectSelection::Conflict;
				if (selected.key == OmfProjectKey::Pj || selected.key == OmfProjectKey::ProjName)
					image.problem(owner, selected.key == OmfProjectKey::Pj ? "_PJ" : "PROJNAME",
								  QStringLiteral("Project names disagree; Project is left blank "
												 "without substituting another value."));
				else
					image.problem(owner, "OMFI:CPNT:Attributes",
								  QStringLiteral("Own Project candidates conflict; lower-priority "
												 "Project substitution is blocked."));
				return;
			}
			if (!selected.text.isEmpty())
			{
				file.project = selected.text;
				file.projectKey = selected.key;
				file.projectSelection = OmfProjectSelection::OwnFile;
				return;
			}
			if (!selected.complete)
			{
				image.problem(owner, "OMFI:CPNT:Attributes",
							  QStringLiteral("Incomplete own Project attributes prevent "
											 "lower-owner substitution."));
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
				if (project.conflict && (project.key == OmfProjectKey::Pj || project.key == OmfProjectKey::ProjName))
					image.problem(candidate, project.key == OmfProjectKey::Pj ? "_PJ" : "PROJNAME",
								  QStringLiteral("Project text is unreadable, unsupported or conflicting."));
				complete &= project.complete;
				selected.conflict |= project.conflict;
				if (project.text.isEmpty())
					continue;
				if (!selected.text.isEmpty() && selected.text != project.text)
					selected.conflict = true;
				selected.key = selected.text.isEmpty()		 ? project.key
							   : selected.key == project.key ? selected.key
															 : OmfProjectKey::Mixed;
				selected.text = project.text;
			}
			if (selected.conflict)
			{
				file.projectSelection = OmfProjectSelection::Conflict;
				image.problem(owner, "OMFI:SCLP:SourceID",
							  QStringLiteral("Immediate-source Project candidates "
											 "conflict; Project is withheld."));
			}
			else if (!complete)
				image.problem(owner, "OMFI:SCLP:SourceID",
							  QStringLiteral("Immediate-source Project relationships or attributes "
											 "are incomplete; Project is withheld."));
			else if (!selected.text.isEmpty())
			{
				file.project = selected.text;
				file.projectKey = selected.key;
				file.projectSelection = OmfProjectSelection::ImmediateSources;
			}
		}

		struct NumberingClock
		{
			qint64 numerator = 0, denominator = 1;
		};

		NumberingClock numberingClock(const OmfRate &rate)
		{
			NumberingClock clock{rate.words.numerator, rate.words.denominator};
			if (clock.numerator <= 0 || clock.denominator <= 0)
				return {};
			if (rate.encoding == OmfRateEncoding::ExactEditRate)
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
			return image.field<qint64>(owner, name,
									   [big](QByteArrayView bytes, QByteArrayView type) -> Read<qint64>
									   {
										   if (!big || bytes.size() != 4 ||
											   !named(type, {"omfi:Long", "omfi:Int32", "omfi:Length32"}))
											   return Issue::Unreadable;
										   return number<qint32>(bytes.data(), *big);
									   });
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

		Read<QVector<NumberingTrack>> numberingTracks(const Image &image, quint32 owner, qint64 essenceKind,
													  const NumberingClock &displayClock)
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
				// A complete different-clock TC track is inapplicable. Do not inspect its
				// flag.
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
					if (!valueOf(sequence))
						return Issue::Unreadable;
					leaves = *valueOf(sequence);
				}
				for (const auto leafOwner : leaves)
				{
					const auto leafClass = image.tag(leafOwner);
					const auto leafKind = numberingScalar(image, leafOwner, "OMFI:CPNT:TrackKind");
					const auto leafRate = image.rate(leafOwner, "OMFI:CPNT:EditRate");
					const auto length = numberingScalar(image, leafOwner, "OMFI:CLIP:Length");
					if (!valueOf(leafClass) || !named(*valueOf(leafClass), {"FILL", "SCLP", "TCCP"}) || !valueOf(leafKind) ||
						*valueOf(leafKind) != track.kind || !valueOf(leafRate) ||
						!sameNumberingClock(numberingClock(*valueOf(leafRate)), track.clock) || !valueOf(length) ||
						*valueOf(length) < 0)
						return Issue::Unreadable;
					if ((track.kind == 3 && *valueOf(leafClass) == "SCLP") ||
						(track.kind != 3 && *valueOf(leafClass) == "TCCP"))
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

		void numberingInterval(const Image &image, const QVector<NumberingTrack> &tracks, qint64 begin, qint64 end,
							   qint64 nominal, NumberingSelection &selection)
		{
			QVector<QPair<qint64, qint64>> covered;
			for (const auto &track : tracks)
			{
				if (track.kind != 3)
					continue;
				for (const auto &leaf : track.leaves)
				{
					const qint64 limit = leaf.begin + leaf.length;
					if (leaf.cls != "TCCP" ||
						(begin == end ? begin < leaf.begin || begin > limit : begin >= limit || end <= leaf.begin))
						continue;
					const auto fps = numberingScalar(image, leaf.owner, "OMFI:TCCP:FPS");
					const auto flags = numberingScalar(image, leaf.owner, "OMFI:TCCP:Flags");
					if (!valueOf(fps) || *valueOf(fps) != nominal || (!valueOf(flags) && !missing(flags)))
					{
						selection.unusable = true;
						continue;
					}
					if (missing(flags))
						selection.absent = true;
					else
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

		std::optional<qint64> physicalDisplayLength(const OmfFileInformation &file, const NumberingClock &display)
		{
			if (!file.length)
				return std::nullopt;
			if (file.kind == OmfKind::Video)
				return file.length;
			std::optional<Detail::AudioFormatFraction> rate;
			if (file.durationClockSource == OmfDurationClockSource::DescriptorSampleRate && file.sampleRate)
				rate = Detail::AudioFormatFraction{quint64(file.sampleRate->words.numerator),
												   quint64(file.sampleRate->words.denominator)};
			else if (file.durationClockSource == OmfDurationClockSource::SummaryFrequency && file.audioFrequency)
			{
				if (const auto *hz = std::get_if<quint32>(&*file.audioFrequency))
					rate = Detail::AudioFormatFraction{*hz, 1};
				else if (const auto *extended = std::get_if<std::array<quint8, 10>>(&*file.audioFrequency))
					rate = Detail::exactAiffFrequency(*extended);
			}
			if (!rate || !rate->numerator || !rate->denominator)
				return std::nullopt;
			std::array<quint64, 3> numerator{{quint64(*file.length), rate->denominator, quint64(display.numerator)}};
			std::array<quint64, 2> denominator{{rate->numerator, quint64(display.denominator)}};
			for (auto &n : numerator)
				for (auto &d : denominator)
				{
					const auto gcd = std::gcd(n, d);
					if (gcd)
					{
						n /= gcd;
						d /= gcd;
					}
				}
			quint64 n = 1, d = 1;
			for (const auto value : numerator)
			{
				if (value && n > std::numeric_limits<quint64>::max() / value)
					return std::nullopt;
				n *= value;
			}
			for (const auto value : denominator)
			{
				if (!value || d > std::numeric_limits<quint64>::max() / value)
					return std::nullopt;
				d *= value;
			}
			const auto quotient = n / d, remainder = n % d;
			const bool roundUp = remainder >= d - remainder;
			if (quotient > quint64(std::numeric_limits<qint64>::max()) - (roundUp ? 1u : 0u))
				return std::nullopt;
			return qint64(quotient + (roundUp ? 1u : 0u));
		}

		OmfNumbering numbering(const Image &image, quint32 fileOwner, const OmfFileInformation &file, const Owners &owners)
		{
			const auto cls = file.descriptorClass
								 ? QByteArrayView(reinterpret_cast<const char *>(file.descriptorClass->data()), 4)
								 : QByteArrayView{};
			const bool audio = audioDescriptor(cls), picture = pictureDescriptor(cls);
			const auto &sourceClock = audio ? file.audioEditRate : file.sampleRate;
			if ((!audio && !picture) || !sourceClock)
				return OmfNumbering::Unavailable;
			const auto clock = numberingClock(*sourceClock);
			if (clock.numerator <= 0)
				return OmfNumbering::Unavailable;
			const qint64 nominal = sameNumberingClock(clock, {30000, 1001})	  ? 30
								   : sameNumberingClock(clock, {60000, 1001}) ? 60
																			  : 0;
			if (!nominal)
				return OmfNumbering::NotRequired;
			const auto unavailable = [&]
			{
				image.problem(fileOwner, "OMFI:TCCP:Flags",
							  QStringLiteral("Required numbering cannot be established from a complete supported file/source, "
											 "physical interval, clock and flag relationship."));
				return OmfNumbering::Unavailable;
			};
			const auto frames = physicalDisplayLength(file, clock);
			if (!frames)
				return unavailable();
			const qint64 essenceKind = picture ? 1 : 2;
			const auto own = numberingTracks(image, fileOwner, essenceKind, clock);
			if (!valueOf(own))
				return unavailable();
			bool direct = false;
			for (const auto &track : *valueOf(own))
				if (track.kind == 3)
					for (const auto &leaf : track.leaves)
						direct |= leaf.cls == "TCCP" &&
								  (*frames == 0 ? leaf.begin == 0 : leaf.begin < *frames && leaf.begin + leaf.length > 0);
			NumberingSelection selection;
			bool intervalFound = false;
			for (const auto &track : *valueOf(own))
			{
				if (track.kind != essenceKind)
					continue;
				if (!sameNumberingClock(track.clock, clock) || *frames > track.length)
					return unavailable();
				if (direct)
				{
					intervalFound = true;
					numberingInterval(image, *valueOf(own), 0, *frames, nominal, selection);
				}
				for (const auto &leaf : track.leaves)
				{
					const qint64 begin = leaf.begin, end = std::min(*frames, leaf.begin + leaf.length);
					if ((*frames == 0 ? begin != 0 : begin >= *frames) || end < begin)
						continue;
					if (leaf.cls != "SCLP")
					{
						if (!direct)
							selection.absent = true;
						continue;
					}
					intervalFound = true;
					const auto source = sourceIdentity(image, leaf.owner);
					if (!valueOf(source))
						return unavailable();
					if (!*valueOf(source))
					{
						if (!direct)
							selection.absent = true;
						continue;
					}
					QVector<quint32> candidates;
					const auto found = owners.constFind(**valueOf(source));
					if (found != owners.cend())
						for (const auto candidate : *found)
							if (candidate != fileOwner && sourceOwner(image, candidate))
								candidates.append(candidate);
					if (candidates.size() != 1)
						return unavailable();
					const auto sourceTrack = numberingScalar(image, leaf.owner, "OMFI:SCLP:SourceTrack");
					const auto position = numberingScalar(image, leaf.owner, "OMFI:SCLP:SourcePosition");
					const qint64 count = end - begin;
					if (!valueOf(sourceTrack) || *valueOf(sourceTrack) <= 0 || !valueOf(position) || *valueOf(position) < 0 ||
						*valueOf(position) > std::numeric_limits<qint64>::max() - count)
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
					const qint64 limit = *valueOf(position) + count;
					if (!selected || !sameNumberingClock(selected->clock, clock) || limit > selected->length)
						return unavailable();
					NumberingSelection local;
					numberingInterval(image, *valueOf(sourceTracks), *valueOf(position), limit, nominal, local);
					// Only a terminal immediate source can establish absence within this scope.
					if (local.absent)
						for (const auto &part : selected->leaves)
						{
							const qint64 partEnd = part.begin + part.length;
							if (part.cls != "SCLP" ||
								(*valueOf(position) == limit ? *valueOf(position) < part.begin || *valueOf(position) > partEnd
															 : *valueOf(position) >= partEnd || limit <= part.begin))
								continue;
							const auto onward = sourceIdentity(image, part.owner);
							if (!valueOf(onward) || *valueOf(onward))
								return unavailable();
						}
					selection.unusable |= local.unusable;
					if (local.recorded)
						selection.accept(*local.recorded);
					if (!direct)
						selection.absent |= local.absent;
				}
			}
			if (direct && !intervalFound)
				return unavailable();
			if (selection.unusable || (selection.absent && selection.recorded && *selection.recorded))
				return unavailable();
			if (!selection.recorded || selection.absent)
				return OmfNumbering::MissingNdfFallback;
			return *selection.recorded ? OmfNumbering::RecordedDf : OmfNumbering::RecordedNdf;
		}

		using SummaryValues = Detail::AudioFormatValues;

		std::optional<Detail::AudioFormatFraction> frequencyFraction(const Detail::AudioFormatFrequency &value)
		{
			if (const auto *hz = std::get_if<quint32>(&value))
				return *hz ? std::optional<Detail::AudioFormatFraction>{{*hz, 1}} : std::nullopt;
			return Detail::exactAiffFrequency(std::get<std::array<quint8, 10>>(value));
		}

		template <typename T>
		void mergeFormatField(std::optional<T> &current, const std::optional<T> &next, const QString &field, QStringList &notes)
		{
			if (!current)
				return;
			if (!next || *current != *next)
			{
				if (next)
					notes.append(QStringLiteral("Repeated audio format chunks disagree on %1.").arg(field));
				current.reset();
			}
		}

		void mergeFormat(SummaryValues &current, const SummaryValues &next, QStringList &notes)
		{
			if (current.frequency)
			{
				const auto a = frequencyFraction(*current.frequency);
				const auto b = next.frequency ? frequencyFraction(*next.frequency) : std::nullopt;
				if (!a || !b || a->numerator != b->numerator || a->denominator != b->denominator)
				{
					if (a && b)
						notes.append(QStringLiteral("Repeated audio format chunks disagree on sample frequency."));
					current.frequency.reset();
				}
			}
			mergeFormatField(current.bits, next.bits, QStringLiteral("sample precision"), notes);
			mergeFormatField(current.storageBits, next.storageBits, QStringLiteral("sample storage width"), notes);
			mergeFormatField(current.encoding, next.encoding, QStringLiteral("audio encoding"), notes);
		}

		Read<SummaryValues> audioSummary(const Image &image, quint32 descriptor, const char *name, bool aiff)
		{
			return image.locatedField<SummaryValues>(
				descriptor, name,
				[&](qsizetype index, QByteArrayView type) -> Read<SummaryValues>
				{
					if (!named(type, {"omfi:VarLenBytes", "omfi:DataValue"}))
						return Issue::Unreadable;
					const auto size = image.valueSize(index);
					const auto header = image.readValue(index, 0, std::min<qsizetype>(12, size));
					if (!header || header->size() != 12 || QByteArrayView(*header).first(4) != (aiff ? "FORM" : "RIFF") ||
						(aiff ? !(QByteArrayView(*header).sliced(8, 4) == "AIFF" ||
								  QByteArrayView(*header).sliced(8, 4) == "AIFC")
							  : QByteArrayView(*header).sliced(8, 4) != "WAVE"))
					{
						image.problem(descriptor, name,
									  QStringLiteral("Audio Summary has an incomplete or "
													 "unsupported container header."));
						return Issue::Unreadable;
					}
					const quint32 declared = number<quint32>(header->constData() + 4, aiff);
					if (declared < 4)
						return Issue::Unreadable;
					const quint64 end = std::min(quint64(size), quint64(declared) + 8);
					const bool compressed = aiff && QByteArrayView(*header).sliced(8, 4) == "AIFC";
					SummaryValues values;
					bool found = false;
					QStringList notes;
					for (quint64 offset = 12; offset < end;)
					{
						if (end - offset < 8)
						{
							notes.append(QStringLiteral("Audio Summary has an incomplete metadata chunk header."));
							break;
						}
						const auto chunk = image.readValue(index, qsizetype(offset), 8);
						if (!chunk)
							break;
						const auto tag = QByteArrayView(*chunk).first(4);
						if (tag == (aiff ? "SSND" : "data"))
							break;
						const bool required = tag == (aiff ? "COMM" : "fmt ");
						const quint32 count = number<quint32>(chunk->constData() + 4, aiff);
						const quint64 payload = offset + 8, padded = quint64(count) + (count & 1);
						if (padded > end - payload)
						{
							notes.append(QStringLiteral("Audio Summary metadata chunk exceeds "
														"its available container range."));
							if (required)
								values = {};
							break;
						}
						if (required)
						{
							SummaryValues next;
							if (count < (aiff ? 18u : 16u))
								notes.append(QStringLiteral("Audio Summary format chunk has an "
															"incomplete required prefix."));
							else
							{
								const auto body = image.readValue(index, qsizetype(payload), count);
								if (!body)
									break;
								next = aiff ? Detail::decodeAiffSummaryCommon(*body, compressed, notes)
											: Detail::decodeWaveSummaryFormat(*body, notes);
							}
							if (found)
								mergeFormat(values, next, notes);
							else
								values = next;
							found = true;
						}
						offset = payload + padded;
					}
					if (!found)
						notes.append(QStringLiteral("Audio Summary has no complete required "
													"format chunk before sound data."));
					for (const auto &note : notes)
						image.problem(descriptor, name, note);
					return found ? Read<SummaryValues>(values) : Read<SummaryValues>(Issue::Unreadable);
				});
		}

		void descriptorFacts(const Image &image, quint32 owner, quint32 descriptor, QByteArrayView cls,
							 OmfFileInformation &file)
		{
			file.descriptorClass = fixedBytes<4>(cls);
			if (audioDescriptor(cls))
				file.kind = OmfKind::Audio;
			else if (pictureDescriptor(cls))
				file.kind = OmfKind::Video;
			const auto length = integerValue(image, descriptor, "OMFI:MDFL:Length");
			if (length && *length >= 0)
				file.length = length;
			else if (length)
				image.problem(descriptor, "OMFI:MDFL:Length", QStringLiteral("Negative length is unavailable."));
			const auto rate = image.rate(descriptor, "OMFI:MDFL:SampleRate");
			if (valueOf(rate))
			{
				file.sampleRate = *valueOf(rate);
				file.durationClockSource = OmfDurationClockSource::DescriptorSampleRate;
			}
			else if (!missing(rate))
				image.problem(descriptor, "OMFI:MDFL:SampleRate",
							  QStringLiteral("Sample rate is unreadable, nonpositive or conflicting."));
			codingFacts(image, descriptor, file);
			if (audioDescriptor(cls))
			{
				const auto clock = image.rate(owner, "OMFI:CPNT:EditRate");
				if (valueOf(clock))
					file.audioEditRate = *valueOf(clock);
				else if (!missing(clock))
					image.problem(owner, "OMFI:CPNT:EditRate",
								  QStringLiteral("Audio display clock is unreadable, "
												 "nonpositive or conflicting."));
				if (cls == "PCMA" || cls == "MPGA")
				{
					const auto *name = cls == "PCMA" ? "OMFI:PCMA:AudioSamplingRate" : "OMFI:MDAU:AudioSamplingRate";
					const auto frequency = image.rate(descriptor, name);
					if (valueOf(frequency) && valueOf(frequency)->encoding == OmfRateEncoding::ExactEditRate)
						file.audioFrequency = valueOf(frequency)->words;
					else if (!missing(frequency))
						image.problem(descriptor, name,
									  QStringLiteral("Concrete audio frequency has no supported positive "
													 "ExactEditRate value."));
					const auto bits = integerValue(image, descriptor, "OMFI:MDAU:BitsPerSample");
					if (bits && *bits > 0 && *bits <= 64)
					{
						file.bitDepth.bits = quint32(*bits);
						file.bitDepth.representation = OmfDepthRepresentation::Integer;
					}
					else if (bits)
						image.problem(descriptor, "OMFI:MDAU:BitsPerSample",
									  QStringLiteral("Audio sample precision is outside the supported range."));
					if (cls == "PCMA" && file.compression.codingState == OmfCodingState::Absent)
						file.compression.audioEncoding = OmfAudioEncoding::Pcm;
				}
				else
				{
					const bool aiff = cls == "AIFD";
					const auto *name = aiff ? "OMFI:AIFD:Summary" : "OMFI:WAVD:Summary";
					const auto summary = audioSummary(image, descriptor, name, aiff);
					if (const auto *values = valueOf(summary))
					{
						if (values->frequency)
							file.audioFrequency =
								std::visit([](const auto &value) -> OmfAudioSourceValue
										   { return value; }, *values->frequency);
						file.bitDepth.bits = values->bits;
						if (values->encoding)
						{
							const bool pcm = *values->encoding == Detail::AudioFormatEncoding::Pcm;
							file.compression.audioEncoding = pcm ? OmfAudioEncoding::Pcm : OmfAudioEncoding::IeeeFloat;
							file.bitDepth.representation =
								pcm ? OmfDepthRepresentation::Integer : OmfDepthRepresentation::Float;
						}
						// The retained source answer records only the already-approved
						// recovery.
						if (!file.sampleRate && file.audioFrequency && std::get_if<Issue>(&rate) &&
							std::get<Issue>(rate) != Issue::Conflict && image.namesKnown(descriptor))
							file.durationClockSource = OmfDurationClockSource::SummaryFrequency;
					}
					else if (!missing(summary))
						image.problem(descriptor, name, QStringLiteral("Audio summary is unreadable or conflicting."));
					if (cls == "WAVE" && missing(summary) && file.compression.codingState == OmfCodingState::Absent)
						file.compression.audioEncoding = OmfAudioEncoding::Pcm;
				}
			}
			else if (pictureDescriptor(cls))
			{
				geometry(image, descriptor, file);
				pictureDepth(image, descriptor, file, cls);
			}
		}

		Read<MobId> mediaIdentity(const Image &image, quint32 object)
		{
			const auto cls = image.tag(object);
			if (!valueOf(cls))
				return Issue::Unreadable;
			const char *name = *valueOf(cls) == "WAVE"	 ? "OMFI:WAVE:MobID"
							   : *valueOf(cls) == "AIFC" ? "OMFI:AIFC:MobID"
							   : *valueOf(cls) == "TIFF" ? "OMFI:TIFF:MobID"
														 : "OMFI:MDAT:MobID";
			return image.identity(object, name);
		}

		void extract(const Image &image, bool details, OmfReport &report)
		{
			Owners owners;
			for (const auto owner : image.activeMobs())
			{
				const auto id = image.identity(owner);
				if (valueOf(id))
					owners[*valueOf(id)].append(owner);
			}
			std::optional<MobId> physicalId;
			bool physicalComplete = image.referenceClassesKnown();
			for (const auto media : image.activeMedia())
			{
				const auto id = mediaIdentity(image, media);
				if (!valueOf(id))
				{
					physicalComplete = false;
					continue;
				}
				if (physicalId && *physicalId != *valueOf(id))
				{
					physicalComplete = false;
					continue;
				}
				physicalId = *valueOf(id);
			}
			if (!physicalComplete || !physicalId)
			{
				image.problem(1, "OMFI:MediaData",
							  QStringLiteral("Active media data does not establish one "
											 "unambiguous physical file identity."));
				return;
			}
			QVector<quint32> files;
			const auto candidates = owners.constFind(*physicalId);
			if (candidates != owners.cend())
				for (const auto owner : *candidates)
				{
					if (image.disputed(owner) || !image.has(owner, "OMFI:MOBJ:PhysicalMedia"))
						continue;
					const auto descriptor = target(image, owner, "OMFI:MOBJ:PhysicalMedia");
					const auto cls = valueOf(descriptor) && *valueOf(descriptor) ? image.tag(*valueOf(descriptor))
																				 : Read<QByteArrayView>(Issue::Unreadable);
					if (valueOf(cls) && mediaDescriptor(*valueOf(cls)) && !image.activeMedia().contains(*valueOf(descriptor)))
						files.append(owner);
				}
			if (files.isEmpty())
			{
				image.problem(1, "OMFI:MediaData",
							  QStringLiteral("Physical media identity has no qualified active file owner."));
				return;
			}
			report.fileInformation.fileMobId = physicalId;
			if (files.size() == 1)
			{
				const auto owner = files.first();
				const auto descriptor = target(image, owner, "OMFI:MOBJ:PhysicalMedia");
				const auto cls = valueOf(descriptor) && *valueOf(descriptor) ? image.tag(*valueOf(descriptor))
																			 : Read<QByteArrayView>(Issue::Unreadable);
				if (valueOf(cls) && mediaDescriptor(*valueOf(cls)))
				{
					descriptorFacts(image, owner, *valueOf(descriptor), *valueOf(cls), report.fileInformation);
					report.fileInformation.numbering = numbering(image, owner, report.fileInformation, owners);
				}
				else
					image.problem(owner, "OMFI:MOBJ:PhysicalMedia",
								  QStringLiteral("The matched file's own descriptor cannot be qualified."));
				selectProject(image, owner, owners, report.fileInformation);
			}
			else
				image.problem(1, "OMFI:MOBJ:PhysicalMedia",
							  QStringLiteral("Repeated active file owners do not establish "
											 "one descriptor/Project owner."));

			QVector<quint32> masters;
			std::optional<MobId> masterId;
			bool complete = image.referenceClassesKnown(), conflict = false;
			for (const auto owner : image.activeMobs())
			{
				if (image.has(owner, "OMFI:MOBJ:PhysicalMedia"))
					continue;
				const auto usage = image.integer(owner, "OMFI:MOBJ:UsageCode");
				if (missing(usage) || (valueOf(usage) && *valueOf(usage) != 1 && *valueOf(usage) != 7))
					continue;
				if (!valueOf(usage) || image.disputed(owner))
				{
					complete = false;
					continue;
				}
				const auto walk = components(image, owner);
				complete &= walk.complete;
				bool linked = false;
				for (const auto component : walk.objects)
				{
					const auto cls = image.tag(component);
					if (!valueOf(cls) || *valueOf(cls) != "SCLP")
						continue;
					const auto source = sourceIdentity(image, component);
					if (!valueOf(source))
					{
						complete = false;
						continue;
					}
					linked |= *valueOf(source) && **valueOf(source) == *physicalId;
				}
				if (!linked)
					continue;
				const auto id = image.identity(owner);
				if (!valueOf(id))
				{
					complete = false;
					continue;
				}
				if (masterId && *masterId != *valueOf(id))
					conflict = true;
				masterId = *valueOf(id);
				masters.append(owner);
			}
			if (!complete || conflict)
			{
				image.problem(1, "OMFI:CompositionMobs",
							  QStringLiteral("Selected master relationships are incomplete or conflicting; "
											 "associated clip information is unavailable."));
				return;
			}
			if (!masterId)
				return;
			report.associatedClipInformation.masterMobId = masterId;
			// One selected identity is required; never borrow another master's fields.
			std::optional<QString> name, bin;
			std::optional<OmfType> type;
			std::optional<OmfPrecomputeCategory> category;
			bool nameBad = false, binBad = false, typeBad = false, categoryBad = false;
			for (const auto owner : masters)
			{
				const auto text = image.text(owner, "OMFI:CPNT:Name");
				if (valueOf(text))
				{
					if (name && *name != *valueOf(text))
						nameBad = true;
					name = *valueOf(text);
				}
				else if (!missing(text))
					nameBad = true;
				const auto attrs = ownerAttributes(image, owner);
				const auto original = originalBin(image, owner, attrs);
				if (const auto *value = valueOf(original))
				{
					if (bin && *bin != *value)
						binBad = true;
					bin = *value;
				}
				else if (!missing(original))
					binBad = true;
				const auto usage = image.integer(owner, "OMFI:MOBJ:UsageCode");
				const auto candidate = *valueOf(usage) == 1 ? OmfType::Precompute : OmfType::Media;
				if (type && *type != candidate)
					typeBad = true;
				type = candidate;
				if (details && candidate == OmfType::Precompute)
				{
					const auto value = precomputeCategory(image, owner, attrs);
					if (category && *category != value)
						categoryBad = true;
					category = value;
				}
			}
			auto &clip = report.associatedClipInformation;
			if (!nameBad && name)
				clip.clipName = *name;
			if (!binBad && bin)
				clip.originalBin = *bin;
			if (!typeBad && type)
				clip.type = *type;
			if (!typeBad && !categoryBad && category)
				clip.precomputeCategory = *category;
			if (nameBad || binBad || typeBad || categoryBad)
				image.problem(1, "OMFI:CompositionMobs",
							  QStringLiteral("Repeated selected-master facts disagree; "
											 "affected clip values are unavailable."));
		}

		struct Stamp
		{
			std::optional<qint64> size;
			QDateTime modified;
		};
		Stamp observe(const QString &path)
		{
			const QFileInfo info(path);
			return info.exists()
					   ? Stamp{info.size() >= 0 ? std::optional<qint64>(info.size()) : std::nullopt, info.lastModified()}
					   : Stamp{};
		}
	} // namespace

	OmfReport parseOmf(const QString &path, const std::atomic_bool &stopRequested, bool includePrecomputeDetails)
	{
		OmfReport report;
		report.path = path;
		const auto stopped = [&]
		{
			if (!stopRequested.load(std::memory_order_relaxed))
				return false;
			if (report.nativeAudioReading == OmfReadResult::NotRead)
				report.nativeAudioReading = OmfReadResult::Cancelled;
			report.omfReading = OmfReadResult::Cancelled;
			report.problemNotes.append(QStringLiteral("OMF/native reading cancelled at a major stage boundary."));
			return true;
		};
		if (stopped())
			return report;
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
		{
			report.nativeAudioReading = report.omfReading = OmfReadResult::IoError;
			report.problemNotes.append(QStringLiteral("Cannot open OMF/native audio file: %1").arg(file.errorString()));
			return report;
		}
		const auto before = observe(path);
		const auto work = [&]
		{
			if (stopped())
				return;
			const auto extent = file.size();
			if (extent < 0)
			{
				report.nativeAudioReading = report.omfReading = OmfReadResult::IoError;
				report.problemNotes.append(QStringLiteral("Cannot establish OMF/native source length."));
				return;
			}
			if (stopped())
				return;
			const auto signature = file.read(std::min<qint64>(12, extent));
			const bool ioError = file.error() != QFileDevice::NoError;
			if (stopped())
				return;
			if (ioError || signature.size() != std::min<qint64>(12, extent))
			{
				report.nativeAudioReading = report.omfReading =
					ioError ? OmfReadResult::IoError : OmfReadResult::Incomplete;
				report.problemNotes.append(QStringLiteral("Cannot read complete media signature."));
				return;
			}
			const auto prefix = QByteArrayView(signature).first(std::min<qsizetype>(4, signature.size()));
			const bool standalone = prefix != "RIFF" && prefix != "RF64" && prefix != "FORM";
			Detail::NativeAudioRead native;
			if (standalone)
				native.reading = OmfReadResult::NotPresent;
			else
				native = Detail::readNativeAudio(file, signature, stopRequested);
			report.nativeAudioReading = native.reading;
			report.nativeAudioInformation = std::move(native.information);
			report.problemNotes += native.problemNotes;
			if (stopped())
				return;
			if (!standalone && !native.locationDiscoveryComplete)
			{
				report.omfReading = native.reading;
				report.problemNotes.append(QStringLiteral("OMF location discovery did not complete; absence or "
														  "a sole container is not established."));
				return;
			}
			if (native.multipleOmfLocations)
			{
				report.omfReading = OmfReadResult::Unsupported;
				report.problemNotes.append(QStringLiteral("Multiple distinct OMF containers are outside Bella's "
														  "supported single-container scope."));
				return;
			}
			std::optional<qint64> label = native.omfLocation ? native.omfLocation->labelOffset : std::nullopt;
			QByteArray knownLabel;
			const bool trailer = standalone || (native.declaredContainerEnd && extent - *native.declaredContainerEnd >= 24);
			if (trailer && extent >= 24 && label != std::optional<qint64>(extent - 24))
			{
				if (!file.seek(extent - 24))
				{
					report.omfReading = OmfReadResult::IoError;
					report.problemNotes.append(QStringLiteral("Cannot seek to possible OMF trailer."));
					return;
				}
				if (stopped())
					return;
				const auto tail = file.read(24);
				const bool failed = file.error() != QFileDevice::NoError;
				if (stopped())
					return;
				if (failed || tail.size() != 24)
				{
					report.omfReading = failed ? OmfReadResult::IoError : OmfReadResult::Incomplete;
					report.problemNotes.append(QStringLiteral("Cannot inspect complete OMF trailer."));
					return;
				}
				if (tail.startsWith(QByteArray::fromHex("a4434da5486472d7")))
				{
					if (native.omfLocation)
					{
						report.omfReading = OmfReadResult::Unsupported;
						report.problemNotes.append(QStringLiteral("Multiple distinct OMF containers are outside "
																  "Bella's supported single-container scope."));
						return;
					}
					label = extent - 24;
					knownLabel = tail;
				}
			}
			if (!label)
			{
				report.omfReading = native.omfLocation ? OmfReadResult::Incomplete
									: standalone	   ? (extent < 24 ? OmfReadResult::Incomplete : OmfReadResult::Unsupported)
													   : OmfReadResult::NotPresent;
				if (report.omfReading != OmfReadResult::NotPresent)
					report.problemNotes.append(QStringLiteral("No complete supported OMF label was established."));
				return;
			}
			Image image(file, *label, stopRequested, report.problemNotes, std::move(knownLabel));
			const auto established = image.establish();
			report.omfReading = image.failure() == OmfReadResult::Completed ? established : image.failure();
			if (stopped() || report.omfReading != OmfReadResult::Completed)
				return;
			extract(image, includePrecomputeDetails, report);
			if (image.failure() != OmfReadResult::Completed)
				report.omfReading = image.failure();
			if (stopRequested.load(std::memory_order_relaxed))
			{
				report.omfReading = OmfReadResult::Cancelled;
				report.problemNotes.append(QStringLiteral("OMF extraction cancelled at its major stage boundary."));
			}
		};
		work();
		const auto after = observe(path);
		const bool sizes = before.size && after.size;
		const bool dates = before.modified.isValid() && after.modified.isValid();
		if ((sizes && before.size != after.size) || (dates && before.modified != after.modified))
		{
			report.sizeDateCheck = OmfCheckResult::Changed;
			report.nativeAudioInformation = {};
			report.fileInformation = {};
			report.associatedClipInformation = {};
			report.problemNotes.append(QStringLiteral("Source size or modification time changed during "
													  "OMF/native reading; its metadata is excluded."));
		}
		else
		{
			report.sizeDateCheck = sizes && dates ? OmfCheckResult::Verified : OmfCheckResult::Unverified;
			if (!sizes)
				report.problemNotes.append(QStringLiteral("Size check unavailable; otherwise usable OMF/native "
														  "metadata remains unverified."));
			if (!dates)
				report.problemNotes.append(QStringLiteral("Modification-time check unavailable; otherwise "
														  "usable OMF/native metadata remains unverified."));
		}
		return report;
	}
} // namespace Bella
