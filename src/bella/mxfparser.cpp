/*
 *
 * Copyright (C) 2006, 2009, 2012, British Broadcasting Corporation
 * All Rights Reserved.
 *
 * Author: Philip de Nier
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the British Broadcasting Corporation nor the names
 *       of its contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "mxfparser.h"

#include <QByteArrayView>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QtEndian>
#include <algorithm>
#include <cstring>
#include <limits>
#include <numeric>
#include <variant>

namespace Bella
{
	namespace
	{
		enum class Issue
		{
			Missing,
			Unreadable,
			Conflict,
			Unsupported
		};
		template <class T>
		using Read = std::variant<T, Issue>;
		template <class T>
		const T *valueOf(const Read<T> &value)
		{
			return std::get_if<T>(&value);
		}
		template <class T>
		bool missing(const Read<T> &value)
		{
			return std::get_if<Issue>(&value) && std::get<Issue>(value) == Issue::Missing;
		}
		template <class T>
		std::optional<T> optionalValue(const Read<T> &value)
		{
			if (const auto *found = valueOf(value))
				return *found;
			return std::nullopt;
		}

		QByteArray hex(const char *value)
		{
			return QByteArray::fromHex(value);
		}
		template <class T>
		bool sameValue(const T &left, const T &right)
		{
			return left == right;
		}
		bool sameValue(const MxfRate &left, const MxfRate &right)
		{
			return qint64(left.numerator) * right.denominator == qint64(right.numerator) * left.denominator;
		}

		// Registry-version differences do not change standard metadata vocabulary.
		// Recorded coding labels are kept intact and never passed through this key.
		QByteArray vocabularyKey(QByteArray value)
		{
			if (value.size() == 16 && value.first(4) == hex("060e2b34"))
				value[7] = 0;
			return value;
		}

		Read<QString> decodeUtf16(QByteArrayView bytes, bool littleEndian = false)
		{
			if (bytes.size() % 2)
				return Issue::Unreadable;
			QString text;
			text.reserve(bytes.size() / 2);
			const auto unit = [&](qsizetype at)
			{
				return littleEndian ? qFromLittleEndian<quint16>(bytes.data() + at)
									: qFromBigEndian<quint16>(bytes.data() + at);
			};
			for (qsizetype at = 0; at < bytes.size(); at += 2)
			{
				const quint16 word = unit(at);
				if (!word)
					break;
				if (QChar::isHighSurrogate(word))
				{
					if (at + 4 > bytes.size() || !QChar::isLowSurrogate(unit(at + 2)))
						return Issue::Unreadable;
					text.append(QChar(word));
					text.append(QChar(unit(at + 2)));
					at += 2;
				}
				else if (QChar::isLowSurrogate(word))
					return Issue::Unreadable;
				else
					text.append(QChar(word));
			}
			return text;
		}

		enum class Wire
		{
			Bytes,
			Unsigned,
			Signed,
			Rate,
			Text,
			Reference,
			References
		};
		struct Definition
		{
			const char *name;
			const char *key;
			Wire wire;
			int width;
		};
		struct ClassDefinition
		{
			const char *parent;
			const char *name;
			const char *key;
		};

		// Static wire vocabulary is populated from the primary definitions used
		// in the recorded format review, not from an OldScanner runtime object.
		const ClassDefinition classes[] = {
			{"root", "root", "00000000000000000000000000000000"},
			{"root", "InterchangeObject", "060e2b34025301010d01010101010100"},
			{"InterchangeObject", "Preface", "060e2b34025301010d01010101012f00"},
			{"InterchangeObject", "ContentStorage", "060e2b34025301010d01010101011800"},
			{"InterchangeObject", "EssenceContainerData", "060e2b34025301010d01010101012300"},
			{"InterchangeObject", "GenericPackage", "060e2b34025301010d01010101013400"},
			{"InterchangeObject", "GenericTrack", "060e2b34025301010d01010101013800"},
			{"GenericTrack", "StaticTrack", "060e2b34025301010d01010101013a00"},
			{"GenericTrack", "Track", "060e2b34025301010d01010101013b00"},
			{"GenericTrack", "EventTrack", "060e2b34025301010d01010101013900"},
			{"InterchangeObject", "StructuralComponent", "060e2b34025301010d01010101010200"},
			{"StructuralComponent", "Sequence", "060e2b34025301010d01010101010f00"},
			{"StructuralComponent", "TimecodeComponent", "060e2b34025301010d01010101011400"},
			{"StructuralComponent", "EdgeCode", "060e2b34025301010d01010101010400"},
			{"StructuralComponent", "SourceClip", "060e2b34025301010d01010101011100"},
			{"StructuralComponent", "DMSegment", "060e2b34025301010d01010101014100"},
			{"SourceClip", "DMSourceClip", "060e2b34025301010d01010101014500"},
			{"GenericPackage", "MaterialPackage", "060e2b34025301010d01010101013600"},
			{"GenericPackage", "SourcePackage", "060e2b34025301010d01010101013700"},
			{"InterchangeObject", "GenericDescriptor", "060e2b34025301010d01010101012400"},
			{"GenericDescriptor", "FileDescriptor", "060e2b34025301010d01010101012500"},
			{"FileDescriptor", "GenericPictureEssenceDescriptor", "060e2b34025301010d01010101012700"},
			{"GenericPictureEssenceDescriptor", "CDCIEssenceDescriptor", "060e2b34025301010d01010101012800"},
			{"CDCIEssenceDescriptor", "MPEGVideoDescriptor", "060e2b34025301010d01010101015100"},
			{"GenericPictureEssenceDescriptor", "RGBAEssenceDescriptor", "060e2b34025301010d01010101012900"},
			{"FileDescriptor", "GenericSoundEssenceDescriptor", "060e2b34025301010d01010101014200"},
			{"FileDescriptor", "GenericDataEssenceDescriptor", "060e2b34025301010d01010101014300"},
			{"FileDescriptor", "MultipleDescriptor", "060e2b34025301010d01010101014400"},
			{"GenericSoundEssenceDescriptor", "WaveAudioDescriptor", "060e2b34025301010d01010101014800"},
			{"WaveAudioDescriptor", "AES3AudioDescriptor", "060e2b34025301010d01010101014700"},
			{"StructuralComponent", "Filler", "060e2b34025301010d01010101010900"},
			{"InterchangeObject", "TaggedValue", "060e2b34025301010d01010101013f00"},
			{"StructuralComponent", "EssenceGroup", "060e2b34025301010d01010101010500"},
			{"InterchangeObject", "MetaDictionary", "060e2b34025301010d01010102250000"},
			{"MetaDefinition", "ClassDefinition", "060e2b34025301010d01010102010000"},
			{"MetaDefinition", "PropertyDefinition", "060e2b34025301010d01010102020000"},
			{"MetaDefinition", "TypeDefinition", "060e2b34025301010d01010102030000"},
			{"InterchangeObject", "DefinitionObject", "060e2b34025301010d01010101011a00"},
			{"DefinitionObject", "DataDefinition", "060e2b34025301010d01010101011b00"},
		};
		const Definition definitions[] = {
			{"FileDescriptor.LinkedTrackID", "060e2b34010101050601010305000000", Wire::Unsigned, 4},
			{"InterchangeObject.InstanceUID", "060e2b34010101010101150200000000", Wire::Bytes, 16},
			{"Preface.ContentStorage", "060e2b34010101020601010402010000", Wire::Reference, 16},
			{"Preface.EssenceContainers", "060e2b34010101050102021002010000", Wire::Bytes, 0},
			{"ContentStorage.Packages", "060e2b34010101020601010405010000", Wire::References, 16},
			{"ContentStorage.EssenceContainerData", "060e2b34010101020601010405020000", Wire::References, 16},
			{"EssenceContainerData.LinkedPackageUID", "060e2b34010101020601010601000000", Wire::Bytes, 32},
			{"EssenceContainerData.BodySID", "060e2b34010101040103040400000000", Wire::Unsigned, 4},
			{"GenericPackage.PackageUID", "060e2b34010101010101151000000000", Wire::Bytes, 32},
			{"GenericPackage.Name", "060e2b34010101010103030201000000", Wire::Text, 0},
			{"GenericPackage.Tracks", "060e2b34010101020601010406050000", Wire::References, 16},
			{"GenericTrack.TrackID", "060e2b34010101020107010100000000", Wire::Unsigned, 4},
			{"GenericTrack.Sequence", "060e2b34010101020601010402040000", Wire::Reference, 16},
			{"Track.EditRate", "060e2b34010101020530040500000000", Wire::Rate, 8},
			{"Track.Origin", "060e2b34010101020702010301030000", Wire::Signed, 8},
			{"StructuralComponent.DataDefinition", "060e2b34010101020407010000000000", Wire::Bytes, 16},
			{"StructuralComponent.Duration", "060e2b34010101020702020101030000", Wire::Signed, 8},
			{"Sequence.StructuralComponents", "060e2b34010101020601010406090000", Wire::References, 16},
			{"TimecodeComponent.RoundedTimecodeBase", "060e2b34010101020404010102060000", Wire::Unsigned, 2},
			{"TimecodeComponent.StartTimecode", "060e2b34010101020702010301050000", Wire::Signed, 8},
			{"TimecodeComponent.DropFrame", "060e2b34010101010404010105000000", Wire::Unsigned, 1},
			{"SourceClip.StartPosition", "060e2b34010101020702010301040000", Wire::Signed, 8},
			{"SourceClip.SourcePackageID", "060e2b34010101020601010301000000", Wire::Bytes, 32},
			{"SourceClip.SourceTrackID", "060e2b34010101020601010302000000", Wire::Unsigned, 4},
			{"SourcePackage.Descriptor", "060e2b34010101020601010402030000", Wire::Reference, 16},
			{"FileDescriptor.SampleRate", "060e2b34010101010406010100000000", Wire::Rate, 8},
			{"FileDescriptor.ContainerDuration", "060e2b34010101010406010200000000", Wire::Signed, 8},
			{"FileDescriptor.Codec", "060e2b34010101020601010401030000", Wire::Bytes, 16},
			{"GenericPictureEssenceDescriptor.FrameLayout", "060e2b34010101010401030104000000", Wire::Unsigned, 1},
			{"GenericPictureEssenceDescriptor.StoredWidth", "060e2b34010101010401050202000000", Wire::Unsigned, 4},
			{"GenericPictureEssenceDescriptor.StoredHeight", "060e2b34010101010401050201000000", Wire::Unsigned, 4},
			{"GenericPictureEssenceDescriptor.SampledWidth", "060e2b34010101010401050108000000", Wire::Unsigned, 4},
			{"GenericPictureEssenceDescriptor.SampledHeight", "060e2b34010101010401050107000000", Wire::Unsigned, 4},
			{"GenericPictureEssenceDescriptor.SampledXOffset", "060e2b34010101010401050109000000", Wire::Signed, 4},
			{"GenericPictureEssenceDescriptor.SampledYOffset", "060e2b3401010101040105010a000000", Wire::Signed, 4},
			{"GenericPictureEssenceDescriptor.DisplayHeight", "060e2b3401010101040105010b000000", Wire::Unsigned, 4},
			{"GenericPictureEssenceDescriptor.DisplayWidth", "060e2b3401010101040105010c000000", Wire::Unsigned, 4},
			{"GenericPictureEssenceDescriptor.DisplayXOffset", "060e2b3401010101040105010d000000", Wire::Signed, 4},
			{"GenericPictureEssenceDescriptor.DisplayYOffset", "060e2b3401010101040105010e000000", Wire::Signed, 4},
			{"GenericPictureEssenceDescriptor.PictureEssenceCoding", "060e2b34010101020401060100000000", Wire::Bytes,
			 16},
			{"CDCIEssenceDescriptor.ComponentDepth", "060e2b3401010102040105030a000000", Wire::Unsigned, 4},
			{"CDCIEssenceDescriptor.HorizontalSubsampling", "060e2b34010101010401050105000000", Wire::Unsigned, 4},
			{"CDCIEssenceDescriptor.VerticalSubsampling", "060e2b34010101020401050110000000", Wire::Unsigned, 4},
			{"RGBAEssenceDescriptor.PixelLayout", "060e2b34010101020401050306000000", Wire::Bytes, 16},
			{"GenericSoundEssenceDescriptor.AudioSamplingRate", "060e2b34010101050402030101010000", Wire::Rate, 8},
			{"GenericSoundEssenceDescriptor.QuantizationBits", "060e2b34010101040402030304000000", Wire::Unsigned, 4},
			{"GenericSoundEssenceDescriptor.SoundEssenceCompression", "060e2b34010101020402040200000000", Wire::Bytes,
			 16},
			{"MultipleDescriptor.SubDescriptorUIDs", "060e2b340101010406010104060b0000", Wire::References, 16},
			{"TaggedValue.Name", "060e2b34010101020302010209010000", Wire::Text, 0},
			{"TaggedValue.Value", "060e2b3401010102030201020a010000", Wire::Bytes, 0},
			{"EssenceGroup.Choices", "060e2b34010101020601010406010000", Wire::References, 16},
			{"EssenceGroup.StillFrame", "060e2b34010101020601010402080000", Wire::Reference, 16},
			{"DefinitionObject.Identification", "060e2b34010101020101150300000000", Wire::Bytes, 16},
			{"DefinitionObject.Name", "060e2b34010101020107010203010000", Wire::Text, 0},
			{"Preface.ProjectName", "a5fb7b25f61594b962fc3717492d42bf", Wire::Text, 0},
			{"Preface.ProjectEditRate", "8c70a718467ae486f36546b1387c4ee9", Wire::Rate, 8},
			{"GenericPackage.AppCode", "a022006094eb75cb96c469924f6211d3", Wire::Signed, 4},
			{"GenericPackage.MobAttributeList", "a01c0004ac969f506095818347b111d4", Wire::References, 16},
			{"GenericPictureEssenceDescriptor.ResolutionID", "a024006094eb75cbce2aca4d51ab11d3", Wire::Signed, 4},
			{"TaggedValue.TaggedValueAttributeList", "a01c0004ac969f506095818547b111d4", Wire::References, 16},
			{"GenericPackage.UsageCode", "060e2b34010101070501010800000000", Wire::Bytes, 16},
		};

		struct Span
		{
			qint64 offset;
			qint64 length;
			mutable std::optional<QByteArray> cache{};
		};
		struct Object
		{
			QByteArray key;
			const ClassDefinition *definition = nullptr;
			QHash<QByteArray, QVector<Span>> properties;
			QSet<QByteArray> unusableMappings;
			bool mapped = true;
			bool instanceUsable = false;
		};

		struct Input
		{
			QFile &file;
			const std::atomic_bool &stop;
			qint64 extent;
			MxfReadResult failure = MxfReadResult::NotRead;
			QString explanation;
			Input(QFile &source, const std::atomic_bool &requested)
				: file(source), stop(requested), extent(source.size())
			{
			}

			bool fail(MxfReadResult outcome, const QString &reason)
			{
				if (failure == MxfReadResult::NotRead)
				{
					failure = outcome;
					explanation = reason;
				}
				return false;
			}
			bool cancelled()
			{
				if (!stop.load(std::memory_order_relaxed))
					return false;
				fail(MxfReadResult::Cancelled, QStringLiteral("MXF metadata read cancelled."));
				return true;
			}
			Read<QByteArray> read(qint64 offset, qint64 length)
			{
				if (cancelled() || failure != MxfReadResult::NotRead)
					return Issue::Unreadable;
				if (offset < 0 || offset > extent || length < 0 || length > extent - offset)
				{
					fail(MxfReadResult::Malformed, QStringLiteral("Required MXF range is outside the physical file."));
					return Issue::Unreadable;
				}
				if (!file.seek(offset))
				{
					fail(MxfReadResult::IoError,
						 QStringLiteral("Cannot seek to MXF byte %1: %2").arg(offset).arg(file.errorString()));
					return Issue::Unreadable;
				}
				if (cancelled())
					return Issue::Unreadable;
				const QByteArray bytes = file.read(length);
				if (cancelled())
					return Issue::Unreadable;
				if (bytes.size() != length)
				{
					fail(MxfReadResult::IoError, QStringLiteral("Cannot read the complete MXF range at byte %1: %2")
													 .arg(offset)
													 .arg(file.errorString()));
					return Issue::Unreadable;
				}
				return bytes;
			}
		};

		class Image
		{
		public:
			Input &input;
			QStringList &notes;
			QVector<Object> objects;
			QHash<QByteArray, QVector<int>> instances;
			QVector<QByteArray> partitionContainers;
			quint32 bodySid = 0;

			Image(Input &source, QStringList &problems) : input(source), notes(problems)
			{
			}

			void problem(int object, const char *name, const QString &reason) const
			{
				if (input.failure != MxfReadResult::NotRead)
					return;
				notes.append(QStringLiteral("Object %1, %2: %3").arg(object).arg(QString::fromLatin1(name), reason));
			}
			bool is(int object, const char *name) const
			{
				if (object < 0 || object >= objects.size())
					return false;
				const ClassDefinition *current = objects[object].definition;
				while (current)
				{
					if (std::strcmp(current->name, name) == 0)
						return true;
					if (std::strcmp(current->name, current->parent) == 0)
						return false;
					const auto parent = std::find_if(std::begin(classes), std::end(classes), [&](const auto &item)
													 { return std::strcmp(item.name, current->parent) == 0; });
					current = parent == std::end(classes) ? nullptr : parent;
				}
				return false;
			}
			QByteArray classKey(int object) const
			{
				return object >= 0 && object < objects.size() ? objects[object].key : QByteArray{};
			}
			bool has(int object, const char *name) const
			{
				return object >= 0 && object < objects.size() &&
					   (objects[object].properties.contains(name) || objects[object].unusableMappings.contains(name));
			}

			template <class T, class Decode>
			Read<T> get(int object, const char *name, Decode decode) const
			{
				if (object < 0 || object >= objects.size())
					return Issue::Unreadable;
				const auto &owner = objects[object];
				if (owner.unusableMappings.contains(name))
					return Issue::Unreadable;
				const auto found = owner.properties.constFind(name);
				if (found == owner.properties.cend())
					return owner.mapped ? Issue::Missing : Issue::Unsupported;
				std::optional<T> result;
				for (const auto &span : *found)
				{
					const auto definition =
						std::find_if(std::begin(definitions), std::end(definitions),
									 [&](const auto &item)
									 { return std::strcmp(item.name, name) == 0; });
					if (definition != std::end(definitions) && definition->width > 0 &&
						definition->wire != Wire::References && span.length != definition->width)
						return Issue::Unreadable;
					if (definition != std::end(definitions) && definition->wire == Wire::Text && span.length % 2)
						return Issue::Unreadable;
					if (!span.cache)
					{
						if (std::strcmp(name, "TaggedValue.Value") == 0)
						{
							// The only consumed Indirect payload is the supported String.
							// Inspect its type before fetching an opaque settings body.
							if (span.length < 17)
								return Issue::Unreadable;
							const auto type = input.read(span.offset, 17);
							if (!valueOf(type))
								return Issue::Unreadable;
							if (*valueOf(type) != hex("420110020000000000060e2b3401040101") &&
								*valueOf(type) != hex("4c0002100100000000060e2b3401040101"))
								return Issue::Unsupported;
							const auto text = input.read(span.offset + 17, span.length - 17);
							if (!valueOf(text))
								return Issue::Unreadable;
							span.cache = *valueOf(type) + *valueOf(text);
						}
						else
						{
							const auto data = input.read(span.offset, span.length);
							if (!valueOf(data))
								return Issue::Unreadable;
							span.cache = *valueOf(data);
						}
					}
					const auto value = decode(QByteArrayView(*span.cache));
					if (!valueOf(value))
						return std::get<Issue>(value);
					if (result && !sameValue(*result, *valueOf(value)))
						return Issue::Conflict;
					if (!result)
						result = *valueOf(value);
				}
				return result ? Read<T>(*result) : Read<T>(Issue::Unreadable);
			}
			Read<QByteArray> bytes(int object, const char *name) const
			{
				return get<QByteArray>(object, name,
									   [](QByteArrayView value) -> Read<QByteArray>
									   { return value.toByteArray(); });
			}
			Read<qint64> integer(int object, const char *name) const
			{
				const auto def = std::find_if(std::begin(definitions), std::end(definitions),
											  [&](const auto &item)
											  { return std::strcmp(item.name, name) == 0; });
				if (def == std::end(definitions) || (def->wire != Wire::Unsigned && def->wire != Wire::Signed))
					return Issue::Unsupported;
				return get<qint64>(object, name, [&](QByteArrayView value) -> Read<qint64>
								   {
					if (value.size() != def->width)
						return Issue::Unreadable;
					quint64 number = 0;
					for (const char byte : value)
						number = (number << 8) | quint8(byte);
					if (def->wire == Wire::Signed)
					{
						if (def->width < 8 && (number & (quint64(1) << (def->width * 8 - 1))))
							number |= ~quint64(0) << (def->width * 8);
						qint64 signedNumber;
						std::memcpy(&signedNumber, &number, sizeof signedNumber);
						return signedNumber;
					}
					return number <= quint64(std::numeric_limits<qint64>::max()) ? Read<qint64>(qint64(number))
					                                                             : Read<qint64>(Issue::Unsupported); });
			}
			Read<MxfRate> rate(int object, const char *name) const
			{
				return get<MxfRate>(object, name, [](QByteArrayView value) -> Read<MxfRate>
									{
					if (value.size() != 8)
						return Issue::Unreadable;
					const MxfRate result{qFromBigEndian<qint32>(value.data()),
					                     qFromBigEndian<qint32>(value.data() + 4)};
					return result.numerator > 0 && result.denominator > 0 ? Read<MxfRate>(result)
					                                                      : Read<MxfRate>(Issue::Unreadable); });
			}
			Read<QString> text(int object, const char *name) const
			{
				return get<QString>(object, name, [](QByteArrayView value)
									{ return decodeUtf16(value); });
			}
			Read<int> resolve(QByteArrayView id) const
			{
				if (id.size() != 16 || std::all_of(id.begin(), id.end(), [](char ch)
												   { return !ch; }))
					return Issue::Unreadable;
				const auto found = instances.constFind(id.toByteArray());
				return found != instances.cend() && found->size() == 1 && objects[found->first()].instanceUsable
						   ? Read<int>(found->first())
						   : Read<int>(Issue::Unreadable);
			}
			Read<QVector<int>> refs(int object, const char *name) const
			{
				const auto def = std::find_if(std::begin(definitions), std::end(definitions),
											  [&](const auto &item)
											  { return std::strcmp(item.name, name) == 0; });
				if (def == std::end(definitions))
					return Issue::Unsupported;
				return get<QVector<int>>(object, name, [&](QByteArrayView value) -> Read<QVector<int>>
										 {
					qsizetype at = 0;
					quint32 count = 1;
					if (def->wire == Wire::References)
					{
						if (value.size() < 8 || qFromBigEndian<quint32>(value.data() + 4) != 16)
							return Issue::Unreadable;
						count = qFromBigEndian<quint32>(value.data());
						at = 8;
						if (quint64(count) * 16 != quint64(value.size() - 8))
							return Issue::Unreadable;
					}
					else if (value.size() != 16)
						return Issue::Unreadable;
					QVector<int> result;
					for (quint32 i = 0; i < count; ++i, at += 16)
					{
						const auto target = resolve(value.sliced(at, 16));
						if (!valueOf(target))
							return std::get<Issue>(target);
						result.append(*valueOf(target));
					}
					return result; });
			}
			Read<int> target(int object, const char *name) const
			{
				const auto values = refs(object, name);
				if (!valueOf(values))
					return std::get<Issue>(values);
				return valueOf(values)->size() == 1 ? Read<int>(valueOf(values)->first())
													: Read<int>(Issue::Unreadable);
			}
		};

		struct Packet
		{
			QByteArray key;
			qint64 value;
			qint64 length;
		};

		Read<quint64> readBer(Input &input, qint64 &position, qint64 end)
		{
			if (position >= end)
			{
				input.fail(MxfReadResult::Malformed,
						   QStringLiteral("MXF length is missing inside its enclosing range."));
				return Issue::Unreadable;
			}
			const auto first = input.read(position++, 1);
			if (!valueOf(first))
				return Issue::Unreadable;
			const quint8 octet = quint8(valueOf(first)->at(0));
			if (octet < 0x80)
				return quint64(octet);
			const int width = octet & 0x7f;
			if (!width || width > 8 || width > end - position)
			{
				input.fail(MxfReadResult::Malformed,
						   QStringLiteral("MXF requires a bounded definite BER length of at most eight bytes."));
				return Issue::Unreadable;
			}
			const auto bytes = input.read(position, width);
			if (!valueOf(bytes))
				return Issue::Unreadable;
			position += width;
			quint64 result = 0;
			for (const char byte : *valueOf(bytes))
				result = (result << 8) | quint8(byte);
			return result;
		}

		Read<Packet> packetAt(Input &input, qint64 position, qint64 end)
		{
			if (position < 0 || position > end || end - position < 17)
			{
				input.fail(MxfReadResult::Malformed,
						   QStringLiteral("MXF packet framing is incomplete inside the selected region."));
				return Issue::Unreadable;
			}
			const auto key = input.read(position, 16);
			if (!valueOf(key))
				return Issue::Unreadable;
			position += 16;
			const auto length = readBer(input, position, end);
			if (!valueOf(length))
				return Issue::Unreadable;
			if (*valueOf(length) > quint64(end - position))
			{
				input.fail(MxfReadResult::Malformed,
						   QStringLiteral("MXF packet crosses its enclosing file or header boundary."));
				return Issue::Unreadable;
			}
			return Packet{*valueOf(key), position, qint64(*valueOf(length))};
		}

		bool localSetKey(QByteArrayView key)
		{
			return key.size() == 16 && key.first(5) == QByteArrayView(hex("060e2b3402")) && (quint8(key[5]) & 7) == 3 &&
				   key[6] == 1;
		}
		bool matchesKey(const QByteArray &left, const char *right)
		{
			return vocabularyKey(left) == vocabularyKey(hex(right));
		}

		bool readLocalSet(Image &image, const Packet &packet, const QHash<quint16, QVector<QByteArray>> &primer,
						  const QHash<QByteArray, const Definition *> &properties)
		{
			const bool privateRoot = packet.key == hex("8053080036210804b3b398a51c9011d4");
			const quint8 coding = privateRoot ? 0x53 : quint8(packet.key[5]);
			Object object;
			object.key = packet.key;
			QByteArray classKey = packet.key;
			if (!privateRoot)
				classKey[5] = 0x53;
			const auto cls = std::find_if(std::begin(classes), std::end(classes),
										  [&](const auto &item)
										  { return matchesKey(classKey, item.key); });
			if (cls != std::end(classes))
				object.definition = cls;
			if (coding != 0x13 && coding != 0x53)
			{
				// Preserve its possible role; unsupported coding cannot disappear
				// from a later ownership-candidate check.
				object.mapped = false;
				image.objects.append(std::move(object));
				return true;
			}
			const qint64 end = packet.value + packet.length;
			qint64 position = packet.value;
			while (position < end)
			{
				if (end - position < (coding == 0x53 ? 4 : 3))
					return image.input.fail(MxfReadResult::Malformed,
											QStringLiteral("MXF local item framing is incomplete."));
				const auto framing = image.input.read(position, coding == 0x53 ? 4 : 2);
				if (!valueOf(framing))
					return false;
				const quint16 tag = qFromBigEndian<quint16>(valueOf(framing)->constData());
				position += valueOf(framing)->size();
				quint64 length = 0;
				if (coding == 0x53)
					length = qFromBigEndian<quint16>(valueOf(framing)->constData() + 2);
				else
				{
					const auto value = readBer(image.input, position, end);
					if (!valueOf(value))
						return false;
					length = *valueOf(value);
				}
				if (length > quint64(end - position))
					return image.input.fail(MxfReadResult::Malformed,
											QStringLiteral("MXF local value crosses its set boundary."));
				const auto mapping = primer.constFind(tag);
				if (mapping == primer.cend())
					object.mapped = false;
				else
				{
					for (const auto &key : *mapping)
					{
						const auto definition = properties.constFind(vocabularyKey(key));
						if (definition == properties.cend())
							continue; // Safely skip an unneeded payload.
						if (mapping->size() != 1)
							object.unusableMappings.insert((*definition)->name);
						object.properties[(*definition)->name].append({position, qint64(length)});
					}
				}
				position += qint64(length);
			}
			image.objects.append(std::move(object));
			return true;
		}

		bool readHeader(Image &image)
		{
			auto &input = image.input;
			if (input.extent < 16)
				return input.fail(MxfReadResult::Malformed, QStringLiteral("MXF initial partition key is incomplete."));
			const auto initialKey = input.read(0, 16);
			if (!valueOf(initialKey))
				return false;
			if (!matchesKey(*valueOf(initialKey), "060e2b34020501010d01020101020400"))
				return input.fail(
					MxfReadResult::Unsupported,
					QStringLiteral("MXF requires a closed, complete initial Header Partition at byte zero."));
			qint64 packValue = 16;
			const auto packLength = readBer(input, packValue, input.extent);
			if (!valueOf(packLength))
				return false;
			if (*valueOf(packLength) > quint64(input.extent - packValue))
				return input.fail(MxfReadResult::Malformed,
								  QStringLiteral("MXF Header Partition crosses the physical file."));
			const Packet partition{*valueOf(initialKey), packValue, qint64(*valueOf(packLength))};
			if (partition.length < 88)
				return input.fail(MxfReadResult::Malformed,
								  QStringLiteral("MXF Header Partition lacks its fixed fields."));
			const auto fixed = input.read(partition.value, 88);
			if (!valueOf(fixed))
				return false;
			const char *pack = valueOf(fixed)->constData();
			if (qFromBigEndian<quint16>(pack) != 1)
				return input.fail(MxfReadResult::Unsupported,
								  QStringLiteral("MXF Header Partition major version is unsupported."));
			if (qFromBigEndian<quint64>(pack + 8) != 0 || qFromBigEndian<quint64>(pack + 16) != 0)
				return input.fail(
					MxfReadResult::Malformed,
					QStringLiteral("Initial MXF partition location or previous-partition value is inconsistent."));
			const QByteArray op(pack + 64, 16);
			if (op.first(7) != hex("060e2b34040101") || op.mid(8, 4) != hex("0d010201") || quint8(op[12]) != 0x10 ||
				quint8(op[13]) > 3 || op[14] != 0 || op[15] != 0)
				return input.fail(
					MxfReadResult::Unsupported,
					QStringLiteral("MXF operational pattern is outside the supported Avid-compatible OP-Atom family."));
			const quint64 headerBytes = qFromBigEndian<quint64>(pack + 32);
			const qint64 afterPack = partition.value + partition.length;
			// A Primer needs a 16-byte key, at least one length byte and its
			// eight-byte batch header. Reject impossible declarations before
			// looking ahead for optional Fill or metadata packet framing.
			if (headerBytes < 25 || headerBytes > quint64(input.extent - afterPack))
				return input.fail(MxfReadResult::Malformed,
								  QStringLiteral("MXF HeaderByteCount cannot contain a complete Primer within the file."));
			image.bodySid = qFromBigEndian<quint32>(pack + 60);
			const quint32 containers = qFromBigEndian<quint32>(pack + 80);
			if (qFromBigEndian<quint32>(pack + 84) != 16 || quint64(containers) * 16 > quint64(partition.length - 88))
				return input.fail(MxfReadResult::Malformed,
								  QStringLiteral("MXF partition container batch has an invalid stride or extent."));
			for (quint32 i = 0; i < containers; ++i)
			{
				const auto label = input.read(partition.value + 88 + qint64(i) * 16, 16);
				if (!valueOf(label))
					return false;
				image.partitionContainers.append(*valueOf(label));
			}
			qint64 position = afterPack;
			auto primerPacket = packetAt(input, position, input.extent);
			if (!valueOf(primerPacket))
				return false;
			if (matchesKey(valueOf(primerPacket)->key, "060e2b34010101020301021001000000"))
			{
				position = valueOf(primerPacket)->value + valueOf(primerPacket)->length;
				primerPacket = packetAt(input, position, input.extent);
				if (!valueOf(primerPacket))
					return false;
			}
			if (!matchesKey(valueOf(primerPacket)->key, "060e2b34020501010d01020101050100"))
				return input.fail(MxfReadResult::Malformed,
								  QStringLiteral("MXF Header Metadata does not begin with its Primer."));
			if (!headerBytes || headerBytes > quint64(input.extent - position))
				return input.fail(MxfReadResult::Malformed,
								  QStringLiteral("MXF HeaderByteCount is empty or crosses the physical file."));
			const qint64 end = position + qint64(headerBytes);
			const auto &primer = *valueOf(primerPacket);
			if (primer.length < 8 || primer.value + primer.length > end)
				return input.fail(MxfReadResult::Malformed,
								  QStringLiteral("MXF Primer crosses its header boundary or lacks its batch fields."));
			const auto batch = input.read(primer.value, 8);
			if (!valueOf(batch))
				return false;
			const quint32 entries = qFromBigEndian<quint32>(valueOf(batch)->constData());
			if (qFromBigEndian<quint32>(valueOf(batch)->constData() + 4) != 18 ||
				quint64(entries) * 18 != quint64(primer.length - 8))
				return input.fail(MxfReadResult::Malformed,
								  QStringLiteral("MXF Primer batch has an invalid stride or extent."));
			QHash<quint16, QVector<QByteArray>> mappings;
			for (quint32 i = 0; i < entries; ++i)
			{
				const auto entry = input.read(primer.value + 8 + qint64(i) * 18, 18);
				if (!valueOf(entry))
					return false;
				const quint16 tag = qFromBigEndian<quint16>(valueOf(entry)->constData());
				const QByteArray key = vocabularyKey(valueOf(entry)->sliced(2));
				if (!mappings[tag].contains(key))
					mappings[tag].append(key);
			}
			QHash<QByteArray, const Definition *> definitionsByKey;
			for (const auto &definition : definitions)
				definitionsByKey.insert(vocabularyKey(hex(definition.key)), &definition);
			position = primer.value + primer.length;
			while (position < end)
			{
				const auto current = packetAt(input, position, end);
				if (!valueOf(current))
					return false;
				const auto &packet = *valueOf(current);
				QByteArray semanticSetKey = packet.key;
				if (localSetKey(semanticSetKey))
					semanticSetKey[5] = 0x53;
				if (matchesKey(packet.key, "060e2b34020501010d01020101050100"))
					return input.fail(MxfReadResult::Malformed,
									  QStringLiteral("Selected MXF header contains more than one Primer."));
				if ((packet.key.first(5) == hex("060e2b3402") && packet.key.mid(8, 4) == hex("0d010301")) ||
					packet.key.first(12) == hex("060e2b34010201010d010301") ||
					packet.key.first(12) == hex("060e2b34010201010e040301") ||
					matchesKey(semanticSetKey, "060e2b34025301010d01020101100100") ||
					matchesKey(packet.key, "060e2b34020501010d01020101110100") ||
					(vocabularyKey(packet.key).first(13) ==
						 vocabularyKey(hex("060e2b34020501010d01020101000000")).first(13) &&
					 quint8(packet.key[13]) >= 2 && quint8(packet.key[13]) <= 4 && packet.key[15] == 0))
					return input.fail(MxfReadResult::Malformed,
									  QStringLiteral("MXF HeaderByteCount includes recording, index or partition data; "
													 "its payload was not read."));
				if (localSetKey(packet.key) || packet.key == hex("8053080036210804b3b398a51c9011d4"))
				{
					if (!readLocalSet(image, packet, mappings, definitionsByKey))
						return false;
				}
				position = packet.value + packet.length;
			}
			for (int i = 0; i < image.objects.size(); ++i)
			{
				const auto instance = image.bytes(i, "InterchangeObject.InstanceUID");
				auto &object = image.objects[i];
				object.instanceUsable =
					valueOf(instance) && valueOf(instance)->size() == 16 &&
					!std::all_of(valueOf(instance)->begin(), valueOf(instance)->end(), [](char ch)
								 { return !ch; });
				// Conflicting/ambiguous UID claims still obstruct a healthy target
				// with the same UID; they must not vanish from the candidate index.
				for (const auto &span : object.properties.value("InterchangeObject.InstanceUID"))
				{
					if (span.length != 16)
						continue;
					const auto claim =
						span.cache ? Read<QByteArray>(*span.cache) : input.read(span.offset, span.length);
					if (!valueOf(claim))
						return false;
					if (!image.instances[*valueOf(claim)].contains(i))
						image.instances[*valueOf(claim)].append(i);
				}
			}
			return input.failure == MxfReadResult::NotRead;
		}
		// Fresh technical extraction from the approved MXF value contract.
		// Insert in mxfparser.cpp's anonymous namespace after Image and Read helpers.

		bool techClassKey(const QByteArray &actual, const char *hex)
		{
			const auto expected = QByteArray::fromHex(hex);
			if (actual.size() != 16 || (quint8(actual[5]) != 0x13 && quint8(actual[5]) != 0x53))
				return false;
			// Both admitted local-set codings describe the same semantic class. The
			// registry-version byte may vary; retain the complete original key separately.
			for (qsizetype i = 0; i != 16; ++i)
				if (i != 5 && i != 7 && actual[i] != expected[i])
					return false;
			return true;
		}

		MxfKind techKind(const QByteArray &key)
		{
			static const std::array<const char *, 3> sound = {{
				"060e2b34025301010d01010101014200", // GenericSound
				"060e2b34025301010d01010101014800", // WaveAudio
				"060e2b34025301010d01010101014700"	// AES3Audio
			}};
			static const std::array<const char *, 4> picture = {{
				"060e2b34025301010d01010101012700", // GenericPicture
				"060e2b34025301010d01010101012800", // CDCI
				"060e2b34025301010d01010101015100", // MPEGVideo
				"060e2b34025301010d01010101012900"	// RGBA
			}};
			for (const auto *known : sound)
				if (techClassKey(key, known))
					return MxfKind::Audio;
			for (const auto *known : picture)
				if (techClassKey(key, known))
					return MxfKind::Video;
			return MxfKind::Unknown;
		}

		template <typename T>
		void techReadProblem(const Image &image, int owner, const char *name, const Read<T> &read)
		{
			if (!valueOf(read) && !missing(read))
				image.problem(owner, name,
							  QStringLiteral("The technical value is unreadable, conflicting or unsupported."));
		}

		std::optional<qint64> techInteger(const Image &image, int owner, const char *name,
										  std::optional<qint64> absent = std::nullopt)
		{
			const auto read = image.integer(owner, name);
			techReadProblem(image, owner, name, read);
			if (const auto *value = valueOf(read))
				return *value;
			return missing(read) ? absent : std::nullopt;
		}

		std::optional<quint32> techUint(const Image &image, int owner, const char *name)
		{
			const auto value = techInteger(image, owner, name);
			if (!value)
				return std::nullopt;
			if (*value < 0 || quint64(*value) > std::numeric_limits<quint32>::max())
			{
				image.problem(owner, name, QStringLiteral("The value is outside its unsigned 32-bit range."));
				return std::nullopt;
			}
			return quint32(*value);
		}

		std::optional<MxfRate> techRate(const Image &image, int owner, const char *name)
		{
			const auto read = image.rate(owner, name);
			techReadProblem(image, owner, name, read);
			if (const auto *value = valueOf(read))
			{
				if (value->numerator > 0 && value->denominator > 0)
					return *value;
				image.problem(
					owner, name,
					QStringLiteral("A usable explicit MXF rational requires positive numerator and denominator."));
			}
			return std::nullopt;
		}

		struct TechRectangle
		{
			std::optional<qint64> width, height, x, y;
		};

		bool techRectangleValid(const TechRectangle &r)
		{
			return r.width && r.height && r.x && r.y && *r.width > 0 && *r.height > 0 && *r.x >= 0 && *r.y >= 0;
		}

		bool techContained(const TechRectangle &child, const TechRectangle &parent)
		{
			if (!techRectangleValid(child) || !techRectangleValid(parent))
				return false;
			// Subtraction after comparison avoids overflowing an offset + extent.
			return *child.width <= *parent.width && *child.height <= *parent.height &&
				   *child.x <= *parent.width - *child.width && *child.y <= *parent.height - *child.height;
		}

		TechRectangle techRectangle(const Image &image, int descriptor, const char *prefix,
									const TechRectangle &fallback)
		{
			const QByteArray name = QByteArray("GenericPictureEssenceDescriptor.") + prefix;
			return {techInteger(image, descriptor, (name + "Width").constData(), fallback.width),
					techInteger(image, descriptor, (name + "Height").constData(), fallback.height),
					techInteger(image, descriptor, (name + "XOffset").constData(), 0),
					techInteger(image, descriptor, (name + "YOffset").constData(), 0)};
		}

		bool techRectangleRecorded(const Image &image, int descriptor, const char *prefix)
		{
			const QByteArray name = QByteArray("GenericPictureEssenceDescriptor.") + prefix;
			return image.has(descriptor, (name + "Width").constData()) ||
				   image.has(descriptor, (name + "Height").constData()) ||
				   image.has(descriptor, (name + "XOffset").constData()) ||
				   image.has(descriptor, (name + "YOffset").constData());
		}

		std::optional<MxfDimensions> techFrameDimensions(const TechRectangle &r, quint8 layout)
		{
			if (!techRectangleValid(r) || layout > 4)
				return std::nullopt;
			const qint64 factor = layout == 1 ? 2 : 1;
			if (*r.height > std::numeric_limits<qint64>::max() / factor)
				return std::nullopt;
			return MxfDimensions{*r.width, *r.height * factor};
		}

		bool techCodingIs(const MxfCompression &compression, const char *hex)
		{
			if (compression.codingState != MxfCodingState::Present)
				return false;
			return QByteArrayView(reinterpret_cast<const char *>(compression.codingLabel.data()), 16) ==
				   QByteArray::fromHex(hex);
		}

		bool techProxy(const Image &image, int descriptor, const MxfCompression &compression, quint8 layout,
					   const TechRectangle &stored, const TechRectangle &sampled, const TechRectangle &display)
		{
			if (!techCodingIs(compression, "060e2b340401010d0401020201311101") || !techRectangleValid(stored) ||
				!techRectangleValid(sampled) || !techRectangleValid(display) || *sampled.x != 0 || *sampled.y != 0 ||
				*display.x != 0 || *display.y != 0)
				return false;
			const auto id = techInteger(image, descriptor, "GenericPictureEssenceDescriptor.ResolutionID");
			if (!id)
				return false;
			struct Proxy
			{
				qint64 id, width, height, activeWidth, activeHeight;
				quint8 layout;
			};
			// Canonical, individually verified source configurations; no approximate match.
			static constexpr std::array<Proxy, 7> profiles = {{{3472, 480, 270, 1920, 540, 2},
															   {3484, 480, 270, 1920, 1080, 0},
															   {3487, 480, 270, 1920, 1080, 0},
															   {3488, 320, 180, 1280, 720, 0},
															   {3470, 352, 240, 720, 240, 2},
															   {3491, 352, 240, 720, 240, 2},
															   {3483, 352, 288, 720, 576, 0}}};
			for (const auto &p : profiles)
				if (*id == p.id && layout == p.layout && *stored.width == p.width && *stored.height == p.height &&
					*sampled.width == p.activeWidth && *sampled.height == p.activeHeight &&
					*display.width == p.activeWidth && *display.height == p.activeHeight)
					return true;
			return false;
		}

		void techGeometry(const Image &image, int descriptor, MxfFileInformation &file)
		{
			const auto layoutValue = techUint(image, descriptor, "GenericPictureEssenceDescriptor.FrameLayout");
			if (!layoutValue || *layoutValue > 4)
			{
				if (layoutValue)
					image.problem(descriptor, "GenericPictureEssenceDescriptor.FrameLayout",
								  QStringLiteral("The picture frame layout is not a supported MXF layout."));
				return;
			}
			const auto layout = quint8(*layoutValue);
			file.compression.frameLayout = layout;
			const TechRectangle stored{techInteger(image, descriptor, "GenericPictureEssenceDescriptor.StoredWidth"),
									   techInteger(image, descriptor, "GenericPictureEssenceDescriptor.StoredHeight"),
									   0, 0};
			const auto sampled = techRectangle(image, descriptor, "Sampled", stored);
			const auto display = techRectangle(image, descriptor, "Display", sampled);

			// Naming uses its independently qualified recorded active dimensions; it is
			// deliberately separate from crop containment and the selected table raster.
			for (const auto *prefix : {"Display", "Sampled", "Stored"})
			{
				const QByteArray name = QByteArray("GenericPictureEssenceDescriptor.") + prefix;
				const TechRectangle naming{techInteger(image, descriptor, (name + "Width").constData()),
										   techInteger(image, descriptor, (name + "Height").constData()), 0, 0};
				if (const auto dimensions = techFrameDimensions(naming, layout))
				{
					file.compression.namingGeometry = *dimensions;
					break;
				}
			}
			const bool contained = techContained(sampled, stored) && techContained(display, sampled);
			if (contained)
			{
				const auto dimensions = techFrameDimensions(display, layout);
				if (dimensions)
				{
					const auto origin = techRectangleRecorded(image, descriptor, "Display")	  ? MxfRasterOrigin::Display
										: techRectangleRecorded(image, descriptor, "Sampled") ? MxfRasterOrigin::Sampled
																							  : MxfRasterOrigin::Stored;
					file.resolution = MxfRaster{*dimensions, origin};
				}
			}
			else if (techProxy(image, descriptor, file.compression, layout, stored, sampled, display))
			{
				if (const auto dimensions = techFrameDimensions(stored, layout))
					file.resolution = MxfRaster{*dimensions, MxfRasterOrigin::VerifiedProxy};
			}
			else
				image.problem(
					descriptor, "GenericPictureEssenceDescriptor.StoredWidth",
					QStringLiteral("The picture rectangles are incomplete, outside their required parents or not a "
								   "verified proxy configuration."));
		}

		struct TechDepth
		{
			std::optional<quint32> bits;
			MxfDepthRepresentation representation = MxfDepthRepresentation::Unknown;
			bool supported = false;
		};

		TechDepth techPictureDepth(quint32 raw, const MxfCompression &compression)
		{
			const bool standard = techCodingIs(compression, "060e2b340401010d0401020203070100");
			const bool fixed = techCodingIs(compression, "060e2b340401010d0401020203070200");
			if (fixed)
			{
				if (raw == 254)
					return {16, MxfDepthRepresentation::S2_14Fixed, true};
				if (raw == 10 || raw == 12)
					return {std::nullopt,
							raw == 10 ? MxfDepthRepresentation::Fixed10_6 : MxfDepthRepresentation::Fixed12_4, true};
				return {};
			}
			if (standard)
			{
				if (raw == 253)
					return {16, MxfDepthRepresentation::HalfFloat, true};
				if (raw == 254)
					return {32, MxfDepthRepresentation::Float, true};
				if (raw == 8 || raw == 10 || raw == 12 || raw == 16)
					return {raw, MxfDepthRepresentation::Integer, true};
				return {};
			}
			if (compression.codingState == MxfCodingState::Unavailable)
				return {}; // Full coding context could change even an ordinary coded width.
			if (raw == 253)
				return {16, MxfDepthRepresentation::HalfFloat, true};
			if (raw == 254)
				return {32, MxfDepthRepresentation::Float, true};
			if (raw == 255)
				return {64, MxfDepthRepresentation::Float, true};
			if (raw > 0 && raw <= 32)
				return {raw, MxfDepthRepresentation::Integer, true};
			return {};
		}

		struct TechRgba
		{
			bool layoutUsable = false;
			bool soleAlpha8 = false;
		};

		TechRgba techRgba(const Image &image, int descriptor, MxfFileInformation &file)
		{
			const auto read = image.bytes(descriptor, "RGBAEssenceDescriptor.PixelLayout");
			techReadProblem(image, descriptor, "RGBAEssenceDescriptor.PixelLayout", read);
			const auto *bytes = valueOf(read);
			if (!bytes)
				return {};
			if (bytes->size() != 16)
			{
				image.problem(descriptor, "RGBAEssenceDescriptor.PixelLayout",
							  QStringLiteral("RGBA PixelLayout must contain exactly eight code/depth pairs."));
				return {};
			}
			std::array<quint8, 16> layout{};
			for (qsizetype i = 0; i != 16; ++i)
				layout[size_t(i)] = quint8((*bytes)[i]);
			file.bitDepth.pixelLayout = layout;
			bool ended = false, structural = true, allSupported = true, sameWidth = true, sameRepresentation = true;
			int meaningful = 0;
			quint8 soleCode = 0, soleDepth = 0;
			std::optional<quint32> common;
			MxfDepthRepresentation representation = MxfDepthRepresentation::Unknown;
			for (size_t i = 0; i != 8; ++i)
			{
				const quint8 code = layout[i * 2], raw = layout[i * 2 + 1];
				const bool padding = (code == 0 || code == '0') && raw == 0;
				if (padding)
				{
					ended = true;
					continue;
				}
				if (ended || code == 0 || code == '0')
				{
					structural = false;
					break;
				}
				if (code == 'F')
					continue; // A fill component contributes no colour/alpha precision.
				++meaningful;
				soleCode = code;
				soleDepth = raw;
				// Lowercase component extensions and palettes need additional semantics;
				// their presence cannot be omitted from a claimed common numeric depth.
				if (!QByteArrayView("RGBAUVWXYZ").contains(char(code)))
				{
					allSupported = false;
					continue;
				}
				const auto value = techPictureDepth(raw, file.compression);
				if (!value.supported || !value.bits)
				{
					allSupported = false;
					continue;
				}
				if (common && *common != *value.bits)
					sameWidth = false;
				if (meaningful == 1)
					representation = value.representation;
				else if (representation != value.representation)
					sameRepresentation = false;
				common = value.bits;
			}
			if (!structural || meaningful == 0)
			{
				image.problem(
					descriptor, "RGBAEssenceDescriptor.PixelLayout",
					QStringLiteral(
						"RGBA requires a meaningful bounded prefix followed only by recognised zero-depth padding."));
				return {};
			}
			if (allSupported && sameWidth && common)
			{
				file.bitDepth.bits = common;
				file.bitDepth.representation = sameRepresentation ? representation : MxfDepthRepresentation::Unknown;
			}
			else
				image.problem(
					descriptor, "RGBAEssenceDescriptor.PixelLayout",
					QStringLiteral("The meaningful RGBA components do not establish one supported common depth."));
			return {true, meaningful == 1 && soleCode == 'A' && soleDepth == 8};
		}

		bool techAlphaContainers(const Image &image, int preface, const QVector<QByteArray> &partitionContainers)
		{
			static const auto alpha = QByteArray::fromHex("060e2b34040101010e04030102080100");
			if (partitionContainers.size() != 1 || partitionContainers.first() != alpha)
				return false;
			const auto read = image.bytes(preface, "Preface.EssenceContainers");
			techReadProblem(image, preface, "Preface.EssenceContainers", read);
			const auto *batch = valueOf(read);
			if (!batch || batch->size() != 24)
				return false;
			return qFromBigEndian<quint32>(batch->constData()) == 1 &&
				   qFromBigEndian<quint32>(batch->constData() + 4) == 16 && batch->mid(8) == alpha;
		}

		void readTechnical(const Image &image, int descriptor, int preface,
						   const QVector<QByteArray> &partitionContainers, MxfFileInformation &file)
		{
			if (descriptor < 0)
				return;
			const auto key = image.classKey(descriptor);
			if (key.size() == 16)
			{
				std::array<quint8, 16> value{};
				for (qsizetype i = 0; i != 16; ++i)
					value[size_t(i)] = quint8(key[i]);
				file.descriptorClass = value;
			}
			file.kind = techKind(key);
			file.sampleRate = techRate(image, descriptor, "FileDescriptor.SampleRate");
			const auto count = techInteger(image, descriptor, "FileDescriptor.ContainerDuration");
			if (count && *count >= 0)
				file.containerDuration = *count;
			else if (count)
				image.problem(
					descriptor, "FileDescriptor.ContainerDuration",
					QStringLiteral("A physical container count must be nonnegative; negative or sentinel values do "
								   "not establish Duration."));

			const bool picture = file.kind == MxfKind::Video;
			const bool audio = file.kind == MxfKind::Audio;
			if (!picture && !audio)
				return;
			const char *codingName = picture ? "GenericPictureEssenceDescriptor.PictureEssenceCoding"
											 : "GenericSoundEssenceDescriptor.SoundEssenceCompression";
			const auto coding = image.bytes(descriptor, codingName);
			techReadProblem(image, descriptor, codingName, coding);
			if (const auto *value = valueOf(coding); value && value->size() == 16)
			{
				file.compression.codingState = MxfCodingState::Present;
				for (qsizetype i = 0; i != 16; ++i)
					file.compression.codingLabel[size_t(i)] = quint8((*value)[i]);
			}
			else if (missing(coding))
				file.compression.codingState = MxfCodingState::Absent;
			else if (valueOf(coding))
				image.problem(descriptor, codingName,
							  QStringLiteral("An essence coding identifier must contain the complete 16-byte value."));

			if (audio)
			{
				if (preface >= 0)
					file.projectEditRate = techRate(image, preface, "Preface.ProjectEditRate");
				file.audioSamplingRate = techRate(image, descriptor, "GenericSoundEssenceDescriptor.AudioSamplingRate");
				const auto bits = techUint(image, descriptor, "GenericSoundEssenceDescriptor.QuantizationBits");
				if (bits && *bits > 0)
					file.bitDepth.bits = *bits;
				else if (bits)
					image.problem(descriptor, "GenericSoundEssenceDescriptor.QuantizationBits",
								  QStringLiteral("Zero does not establish an audio quantization depth."));
				const bool pcmClass = techClassKey(key, "060e2b34025301010d01010101014800") ||
									  techClassKey(key, "060e2b34025301010d01010101014700");
				if (pcmClass && file.compression.codingState == MxfCodingState::Absent)
				{
					file.compression.namingFallback = MxfNamingFallback::Pcm;
					if (file.bitDepth.bits)
						file.bitDepth.representation = MxfDepthRepresentation::Integer;
				}
				return;
			}

			techGeometry(image, descriptor, file);
			const auto horizontal = techUint(image, descriptor, "CDCIEssenceDescriptor.HorizontalSubsampling");
			const auto verticalRead = image.integer(descriptor, "CDCIEssenceDescriptor.VerticalSubsampling");
			file.compression.horizontalSubsampling = horizontal;
			if (missing(verticalRead))
				file.compression.verticalSubsampling = 1;
			else
			{
				techReadProblem(image, descriptor, "CDCIEssenceDescriptor.VerticalSubsampling", verticalRead);
				if (const auto *value = valueOf(verticalRead);
					value && *value >= 0 && quint64(*value) <= std::numeric_limits<quint32>::max())
					file.compression.verticalSubsampling = quint32(*value);
			}

			const bool rgba = techClassKey(key, "060e2b34025301010d01010101012900");
			if (rgba)
			{
				const auto layout = techRgba(image, descriptor, file);
				if (layout.layoutUsable && layout.soleAlpha8 &&
					file.compression.codingState == MxfCodingState::Absent &&
					missing(image.bytes(descriptor, "FileDescriptor.Codec")) && preface >= 0 &&
					techAlphaContainers(image, preface, partitionContainers))
					file.compression.namingFallback = MxfNamingFallback::UncompressedAlpha8;
			}
			else if (techClassKey(key, "060e2b34025301010d01010101012800") ||
					 techClassKey(key, "060e2b34025301010d01010101015100"))
			{
				const auto raw = techUint(image, descriptor, "CDCIEssenceDescriptor.ComponentDepth");
				if (raw)
				{
					file.bitDepth.rawComponentDepth = *raw;
					const auto value = techPictureDepth(*raw, file.compression);
					file.bitDepth.bits = value.bits;
					file.bitDepth.representation = value.representation;
					if (!value.supported)
						image.problem(descriptor, "CDCIEssenceDescriptor.ComponentDepth",
									  QStringLiteral("The coded picture depth has no supported interpretation in its "
													 "complete coding context."));
				}
			}
			// These supported frame-based forms use the explicit own clock directly.
			// The three exact SingleField proxy configurations (3472/3470/3491) also
			// record one count per frame, as checked against their own track clocks
			// and paired MDB counts. Unestablished forms keep the raw
			// count/rate without inventing a field-to-frame conversion or invalid enum.
			file.frameRateQualified = file.sampleRate && file.compression.frameLayout &&
									  (*file.compression.frameLayout != 2 ||
									   (file.resolution && file.resolution->origin == MxfRasterOrigin::VerifiedProxy));
			if (file.sampleRate && !file.frameRateQualified)
				image.problem(
					descriptor, "FileDescriptor.SampleRate",
					QStringLiteral("The raw rate is retained, but physical video frame units are not established for "
								   "this layout/context."));
		}
		// Private editorial extraction helpers. Integrated into Bella's one MXF reader.

		struct EditorialAttribute
		{
			int handle = -1;
			QString name;
		};

		enum class EditorialDataKind
		{
			Picture,
			Sound,
			Timecode,
			Other,
			Unreadable
		};

		Read<QString> editorialIndirectString(const Image &image, int attribute)
		{
			const auto raw = image.bytes(attribute, "TaggedValue.Value");
			const auto *bytes = valueOf(raw);
			if (!bytes)
				return std::get<Issue>(raw);

			// The AAF String AUID has its own byte-order marker, independently of MXF.
			static const auto bigEndianString = QByteArray::fromHex("420110020000000000060e2b3401040101");
			static const auto littleEndianString = QByteArray::fromHex("4c0002100100000000060e2b3401040101");
			if (bytes->startsWith(bigEndianString))
				return decodeUtf16(QByteArrayView(*bytes).sliced(bigEndianString.size()), false);
			if (bytes->startsWith(littleEndianString))
				return decodeUtf16(QByteArrayView(*bytes).sliced(littleEndianString.size()), true);
			return bytes->size() < bigEndianString.size() ? Issue::Unreadable : Issue::Unsupported;
		}

		Read<QVector<EditorialAttribute>> editorialAttributes(const Image &image, int owner, const char *property)
		{
			const auto references = image.refs(owner, property);
			const auto *handles = valueOf(references);
			if (!handles)
				return missing(references) ? Read<QVector<EditorialAttribute>>(QVector<EditorialAttribute>{})
										   : Read<QVector<EditorialAttribute>>(std::get<Issue>(references));
			QVector<EditorialAttribute> attributes;
			attributes.reserve(handles->size());
			for (const int handle : *handles)
			{
				if (!image.is(handle, "TaggedValue"))
					return Issue::Unreadable;
				const auto name = image.text(handle, "TaggedValue.Name");
				if (const auto *text = valueOf(name))
					attributes.push_back({handle, *text});
				else
					return std::get<Issue>(name) == Issue::Conflict ? Issue::Conflict : Issue::Unreadable;
			}
			return attributes;
		}

		Read<QString> editorialProjectCandidate(const Image &image, const QVector<EditorialAttribute> &attributes,
												const QString &key)
		{
			std::optional<QString> candidate;
			for (const auto &attribute : attributes)
			{
				if (attribute.name != key)
					continue;
				const auto value = editorialIndirectString(image, attribute.handle);
				if (const auto *text = valueOf(value))
				{
					if (text->trimmed().isEmpty())
						continue;
					if (candidate && *candidate != *text)
						return Issue::Conflict;
					candidate = *text;
				}
				else
				{
					if (std::get<Issue>(value) == Issue::Conflict)
						return Issue::Conflict;
					image.problem(attribute.handle, "TaggedValue.Value",
								  QStringLiteral("Project attribute is not a readable supported String; trying the "
												 "next permitted candidate."));
				}
			}
			return candidate ? Read<QString>(*candidate) : Read<QString>(Issue::Missing);
		}

		Read<QVector<int>> editorialObjectList(const Image &image, int attribute)
		{
			const auto value = editorialIndirectString(image, attribute);
			const auto *marker = valueOf(value);
			if (!marker)
				return std::get<Issue>(value);
			if (*marker != QStringLiteral("__AttributeList"))
				return Issue::Unsupported;

			const auto references = image.refs(attribute, "TaggedValue.TaggedValueAttributeList");
			if (const auto *handles = valueOf(references))
			{
				for (const int handle : *handles)
					if (!image.is(handle, "TaggedValue"))
						return Issue::Unreadable;
				return *handles;
			}
			return std::get<Issue>(references);
		}

		Read<QString> editorialUserProject(const Image &image, const QVector<EditorialAttribute> &attributes)
		{
			std::optional<QString> candidate;
			for (const auto &attribute : attributes)
			{
				if (attribute.name != QStringLiteral("_USER"))
					continue;
				const auto list = editorialObjectList(image, attribute.handle);
				if (!valueOf(list))
				{
					if (std::get<Issue>(list) == Issue::Conflict)
						return Issue::Conflict;
					image.problem(attribute.handle, "TaggedValue.TaggedValueAttributeList",
								  QStringLiteral("Recorded _USER is not a readable supported local attribute list."));
					continue;
				}
				const auto nested =
					editorialAttributes(image, attribute.handle, "TaggedValue.TaggedValueAttributeList");
				const auto *nestedAttributes = valueOf(nested);
				if (!nestedAttributes)
				{
					if (std::get<Issue>(nested) == Issue::Conflict)
						return Issue::Conflict;
					image.problem(attribute.handle, "TaggedValue.TaggedValueAttributeList",
								  QStringLiteral("Recorded _USER attributes cannot be safely identified."));
					continue;
				}
				const auto value = editorialProjectCandidate(image, *nestedAttributes, QStringLiteral("_PJ"));
				if (const auto *text = valueOf(value))
				{
					if (candidate && *candidate != *text)
						return Issue::Conflict;
					candidate = *text;
				}
				else if (std::get<Issue>(value) == Issue::Conflict)
					return Issue::Conflict;
			}
			return candidate ? Read<QString>(*candidate) : Read<QString>(Issue::Missing);
		}

		void readProject(const Image &image, int preface, int file, MxfFileInformation &information)
		{
			const auto header = image.text(preface, "Preface.ProjectName");
			if (const auto *text = valueOf(header))
			{
				if (!text->trimmed().isEmpty())
				{
					information.project = *text;
					information.projectSelection = MxfProjectSelection::Preface;
					return;
				}
			}
			else if (std::get<Issue>(header) == Issue::Conflict)
			{
				information.projectSelection = MxfProjectSelection::Conflict;
				image.problem(preface, "Preface.ProjectName",
							  QStringLiteral("Conflicting header Project values leave Project unavailable."));
				return;
			}
			else if (!missing(header))
				image.problem(preface, "Preface.ProjectName",
							  QStringLiteral(
								  "Header Project is unreadable; trying the qualified file's own Project attributes."));

			if (file < 0 || !image.is(file, "SourcePackage"))
				return;
			const auto attributes = editorialAttributes(image, file, "GenericPackage.MobAttributeList");
			const auto *own = valueOf(attributes);
			if (!own)
			{
				if (std::get<Issue>(attributes) == Issue::Conflict)
					information.projectSelection = MxfProjectSelection::Conflict;
				image.problem(file, "GenericPackage.MobAttributeList",
							  QStringLiteral("File-owned Project attributes cannot be safely identified."));
				return;
			}

			const auto select = [&](const Read<QString> &candidate, MxfProjectKey key)
			{
				if (const auto *text = valueOf(candidate))
				{
					information.project = *text;
					information.projectSelection = MxfProjectSelection::OwnFile;
					information.projectKey = key;
					return true;
				}
				if (std::get<Issue>(candidate) == Issue::Conflict)
				{
					information.projectSelection = MxfProjectSelection::Conflict;
					image.problem(
						file, "GenericPackage.MobAttributeList",
						QStringLiteral("Conflicting Project values at the same priority leave Project unavailable."));
					return true;
				}
				return false;
			};
			if (select(editorialProjectCandidate(image, *own, QStringLiteral("_PJ")), MxfProjectKey::Pj))
				return;
			if (select(editorialProjectCandidate(image, *own, QStringLiteral("PROJNAME")),
					   MxfProjectKey::ProjName))
				return;
			select(editorialUserProject(image, *own), MxfProjectKey::UserPj);
		}

		EditorialDataKind editorialKnownDataDefinition(const QByteArray &label)
		{
			// Complete primary SMPTE labels and the supported legacy AAF identifiers.
			if (label == QByteArray::fromHex("060e2b34040101010103020201000000") ||
				label == QByteArray::fromHex("807d006008143e6f6f3c8ce16cef11d2"))
				return EditorialDataKind::Picture;
			if (label == QByteArray::fromHex("060e2b34040101010103020202000000") ||
				label == QByteArray::fromHex("807d006008143e6f78e1ebe16cef11d2"))
				return EditorialDataKind::Sound;
			if (label == QByteArray::fromHex("060e2b34040101010103020101000000") ||
				label == QByteArray::fromHex("807f006008143e6f7f275e8177e511d2"))
				return EditorialDataKind::Timecode;
			return EditorialDataKind::Unreadable;
		}

		EditorialDataKind editorialDataDefinition(const Image &image, int component)
		{
			if (!image.is(component, "StructuralComponent"))
				return EditorialDataKind::Unreadable;
			const auto definition = image.bytes(component, "StructuralComponent.DataDefinition");
			const auto *label = valueOf(definition);
			if (!label || label->size() != 16)
				return EditorialDataKind::Unreadable;
			const auto directKind = editorialKnownDataDefinition(*label);
			if (directKind != EditorialDataKind::Unreadable)
				return directKind;

			const auto target = image.target(component, "StructuralComponent.DataDefinition");
			const auto *handle = valueOf(target);
			if (!handle || !image.is(*handle, "DataDefinition"))
				return EditorialDataKind::Unreadable;
			const auto identification = image.bytes(*handle, "DefinitionObject.Identification");
			const auto *identity = valueOf(identification);
			return identity ? editorialKnownDataDefinition(*identity) : EditorialDataKind::Unreadable;
		}

		MxfType editorialType(const Image &image, int master)
		{
			const auto appCode = image.integer(master, "GenericPackage.AppCode");
			const auto usage = image.bytes(master, "GenericPackage.UsageCode");
			if (missing(appCode) && missing(usage))
				return MxfType::Media;

			const auto *code = valueOf(appCode);
			if (!code)
				return MxfType::Unknown;
			if (missing(usage))
				return *code == 1 ? MxfType::Precompute : *code == 7 ? MxfType::Media
																	 : MxfType::Unknown;
			const auto *label = valueOf(usage);
			if (!label)
				return MxfType::Unknown;
			if (*code == 1 && (*label == QByteArray::fromHex("060e2b34040101010d01010201010800") ||
							   *label == QByteArray::fromHex("0d01010201010800060e2b3404010101")))
				return MxfType::Precompute;
			if (*code == 7 && (*label == QByteArray::fromHex("060e2b34040101010d01010201010600") ||
							   *label == QByteArray::fromHex("0d01010201010600060e2b3404010101")))
				return MxfType::Media;
			return MxfType::Unknown;
		}

		MxfPrecomputeCategory editorialPrecompute(const Image &image, int master)
		{
			const auto attributes = editorialAttributes(image, master, "GenericPackage.MobAttributeList");
			const auto *own = valueOf(attributes);
			if (!own)
				return MxfPrecomputeCategory::Unknown;
			int importAttribute = -1;
			for (const auto &attribute : *own)
			{
				if (attribute.name != QStringLiteral("_IMPORTSETTING"))
					continue;
				if (importAttribute >= 0)
					return MxfPrecomputeCategory::Unknown;
				importAttribute = attribute.handle;
			}
			if (importAttribute < 0)
				return MxfPrecomputeCategory::RenderedEffects;

			const auto value = editorialIndirectString(image, importAttribute);
			const auto *marker = valueOf(value);
			if (!marker || *marker == QStringLiteral("__PortableObject"))
				return MxfPrecomputeCategory::Unknown;
			if (*marker != QStringLiteral("__AttributeList"))
				return MxfPrecomputeCategory::RenderedEffects;
			if (!valueOf(editorialObjectList(image, importAttribute)))
				return MxfPrecomputeCategory::Unknown;

			const auto references = image.refs(master, "GenericPackage.Tracks");
			const auto *tracks = valueOf(references);
			if (!tracks)
				return MxfPrecomputeCategory::Unknown;
			QVector<int> seen;
			int pictureThreshold = 0;
			for (const int track : *tracks)
			{
				if (seen.contains(track) || !image.is(track, "GenericTrack"))
					return MxfPrecomputeCategory::Unknown;
				seen.push_back(track);
				const auto sequence = image.target(track, "GenericTrack.Sequence");
				const auto *segment = valueOf(sequence);
				if (!segment)
					return MxfPrecomputeCategory::Unknown;
				switch (editorialDataDefinition(image, *segment))
				{
				case EditorialDataKind::Picture:
					if (pictureThreshold < 2)
						++pictureThreshold;
					break;
				case EditorialDataKind::Sound:
				case EditorialDataKind::Timecode:
					break;
				case EditorialDataKind::Other:
				case EditorialDataKind::Unreadable:
					return MxfPrecomputeCategory::Unknown;
				}
			}
			return pictureThreshold >= 2 ? MxfPrecomputeCategory::TitlesAndMatteKeys
										 : MxfPrecomputeCategory::RenderedEffects;
		}

		void readClip(const Image &image, int master, bool details, MxfAssociatedClipInformation &information)
		{
			if (master < 0 || !image.is(master, "MaterialPackage"))
				return;
			const auto name = image.text(master, "GenericPackage.Name");
			if (const auto *text = valueOf(name))
				information.clipName = *text;
			else if (!missing(name))
				image.problem(master, "GenericPackage.Name",
							  QStringLiteral("Selected master Clip Name is unreadable or conflicting."));

			information.type = editorialType(image, master);
			if (information.type == MxfType::Unknown)
				image.problem(
					master, "GenericPackage.AppCode",
					QStringLiteral(
						"Selected master AppCode and UsageCode do not establish a supported Media/Precompute Type."));
			if (details && information.type == MxfType::Precompute)
			{
				information.precomputeCategory = editorialPrecompute(image, master);
				if (information.precomputeCategory == MxfPrecomputeCategory::Unknown)
					image.problem(
						master, "GenericPackage.MobAttributeList",
						QStringLiteral("Selected Precompute master's import-setting or immediate-track facts do not "
									   "establish its category."));
			}
		}
		Read<MobId> packageId(const Image &image, int object, const char *name)
		{
			const auto raw = image.bytes(object, name);
			if (!valueOf(raw))
				return std::get<Issue>(raw);
			if (valueOf(raw)->size() != 32 ||
				std::all_of(valueOf(raw)->begin(), valueOf(raw)->end(), [](char byte)
							{ return !byte; }))
				return Issue::Unreadable;
			const auto normalized =
				normalizeMobId(QByteArrayView(*valueOf(raw)), MobIdLayout::Aaf32, ByteOrder::BigEndian);
			if (const auto *id = std::get_if<MobId>(&normalized))
				return *id;
			return Issue::Unreadable;
		}

		struct FileSelection
		{
			int preface = -1;
			int file = -1;
			int descriptor = -1;
			QVector<int> packages;
		};

		int selectedDescriptor(const Image &image, int file)
		{
			const auto descriptor = image.target(file, "SourcePackage.Descriptor");
			if (!valueOf(descriptor) || !image.is(*valueOf(descriptor), "FileDescriptor"))
			{
				image.problem(file, "SourcePackage.Descriptor",
							  QStringLiteral("A unique supported own FileDescriptor could not be established."));
				return -1;
			}
			const int owner = *valueOf(descriptor);
			if (!image.is(owner, "MultipleDescriptor"))
				return owner;
			const auto children = image.refs(owner, "MultipleDescriptor.SubDescriptorUIDs");
			if (valueOf(children))
			{
				QSet<int> distinct;
				for (const int child : *valueOf(children))
					if (image.is(child, "FileDescriptor") && !image.is(child, "MultipleDescriptor"))
						distinct.insert(child);
				if (distinct.size() == 1 &&
					distinct.size() == QSet<int>(valueOf(children)->cbegin(), valueOf(children)->cend()).size())
					return *distinct.cbegin();
			}
			// A multi-descriptor owner establishes the file role but not a Kind
			// or a choice among unrelated children.
			image.problem(owner, "MultipleDescriptor.SubDescriptorUIDs",
						  QStringLiteral("No unique applicable owned child descriptor was established; child-dependent "
										 "facts are unavailable."));
			return owner;
		}

		FileSelection selectFile(Image &image, MxfFileInformation &file)
		{
			FileSelection selected;
			for (int object = 0; object < image.objects.size(); ++object)
				if (image.is(object, "Preface"))
				{
					if (selected.preface >= 0)
					{
						image.input.fail(MxfReadResult::Malformed,
										 QStringLiteral("Selected MXF header has ambiguous Preface ownership."));
						return {};
					}
					selected.preface = object;
				}
			if (selected.preface < 0)
			{
				image.input.fail(MxfReadResult::Malformed,
								 QStringLiteral("Selected MXF header has no supported Preface."));
				return selected;
			}
			const auto storage = image.target(selected.preface, "Preface.ContentStorage");
			if (!valueOf(storage) || !image.is(*valueOf(storage), "ContentStorage"))
			{
				image.problem(selected.preface, "Preface.ContentStorage",
							  QStringLiteral("Active ContentStorage ownership is unavailable; dependent file and "
											 "master facts are withheld."));
				return selected;
			}
			const auto packages = image.refs(*valueOf(storage), "ContentStorage.Packages");
			const auto containers = image.refs(*valueOf(storage), "ContentStorage.EssenceContainerData");
			if (!valueOf(packages) || !valueOf(containers))
			{
				image.problem(*valueOf(storage), "ContentStorage",
							  QStringLiteral("Active package or essence-container membership is unreadable; file "
											 "ownership is unavailable."));
				return selected;
			}
			QSet<int> seen;
			for (const int object : *valueOf(packages))
				if (!seen.contains(object))
				{
					selected.packages.append(object);
					seen.insert(object);
				}
			QVector<MobId> essenceIds;
			bool complete = true;
			for (const int ecd : *valueOf(containers))
			{
				if (!image.is(ecd, "EssenceContainerData"))
				{
					complete = false;
					continue;
				}
				const auto sid = image.integer(ecd, "EssenceContainerData.BodySID");
				if (!valueOf(sid))
				{
					complete = false;
					continue;
				}
				if (*valueOf(sid) == 0 || (image.bodySid && *valueOf(sid) != image.bodySid))
					continue;
				const auto id = packageId(image, ecd, "EssenceContainerData.LinkedPackageUID");
				if (!valueOf(id))
				{
					complete = false;
					continue;
				}
				if (std::find(essenceIds.cbegin(), essenceIds.cend(), *valueOf(id)) == essenceIds.cend())
					essenceIds.append(*valueOf(id));
			}
			if (!complete || essenceIds.size() != 1)
			{
				image.problem(
					*valueOf(storage), "ContentStorage.EssenceContainerData",
					QStringLiteral("One unambiguous internal essence-linked file identity could not be established."));
				return selected;
			}
			for (const int object : selected.packages)
			{
				if (image.is(object, "MaterialPackage"))
					continue; // Its identity is independently qualified below.
				const auto id = packageId(image, object, "GenericPackage.PackageUID");
				if (!valueOf(id))
				{
					complete = false;
					continue;
				}
				if (*valueOf(id) != essenceIds.first())
					continue;
				if (!image.is(object, "SourcePackage") || selected.file >= 0)
				{
					complete = false;
					continue;
				}
				selected.file = object;
			}
			if (!complete || selected.file < 0)
			{
				selected.file = -1;
				image.problem(*valueOf(storage), "ContentStorage.Packages",
							  QStringLiteral(
								  "The essence identity does not resolve to one qualified active file SourcePackage."));
				return selected;
			}
			selected.descriptor = selectedDescriptor(image, selected.file);
			if (selected.descriptor < 0)
			{
				selected.file = -1;
				return selected;
			}
			file.fileMobId = essenceIds.first();
			return selected;
		}

		struct Components
		{
			QVector<int> sourceClips;
			bool complete = true;
		};

		Components mediaComponents(const Image &image, int root, EditorialDataKind kind)
		{
			Components result;
			QVector<QPair<int, bool>> pending{{root, false}};
			QHash<int, quint8> colour;
			while (!pending.isEmpty())
			{
				const auto item = pending.takeLast();
				if (item.second)
				{
					colour[item.first] = 2;
					continue;
				}
				if (colour.value(item.first) == 1)
				{
					result.complete = false;
					continue;
				}
				if (colour.value(item.first) == 2)
					continue;
				colour[item.first] = 1;
				pending.append({item.first, true});
				if (!image.is(item.first, "StructuralComponent") || editorialDataDefinition(image, item.first) != kind)
				{
					result.complete = false;
					continue;
				}
				if (image.is(item.first, "SourceClip"))
					result.sourceClips.append(item.first);
				else if (image.is(item.first, "Sequence") || image.is(item.first, "EssenceGroup"))
				{
					const bool sequence = image.is(item.first, "Sequence");
					const auto children =
						image.refs(item.first, sequence ? "Sequence.StructuralComponents" : "EssenceGroup.Choices");
					if (!valueOf(children))
					{
						result.complete = false;
						continue;
					}
					for (auto it = valueOf(children)->crbegin(); it != valueOf(children)->crend(); ++it)
						pending.append({*it, false});
					if (!sequence)
					{
						const auto still = image.target(item.first, "EssenceGroup.StillFrame");
						if (valueOf(still))
							pending.append({*valueOf(still), false});
						else if (!missing(still))
							result.complete = false;
					}
				}
				else if (!image.is(item.first, "Filler"))
					result.complete = false;
			}
			return result;
		}

		void selectMaster(const Image &image, const FileSelection &selected, const MxfFileInformation &file,
						  bool details, MxfAssociatedClipInformation &clip)
		{
			if (selected.file < 0 || !file.fileMobId)
				return;
			const auto fileTracks = image.refs(selected.file, "GenericPackage.Tracks");
			if (!valueOf(fileTracks))
			{
				image.problem(
					selected.file, "GenericPackage.Tracks",
					QStringLiteral("File track coverage is unavailable; a unique master cannot be established."));
				return;
			}
			QHash<qint64, int> tracksById;
			bool complete = true;
			for (const int track : *valueOf(fileTracks))
			{
				const auto id = image.integer(track, "GenericTrack.TrackID");
				if (!image.is(track, "GenericTrack") || !valueOf(id))
				{
					complete = false;
					continue;
				}
				if (tracksById.contains(*valueOf(id)) && tracksById[*valueOf(id)] != track)
					complete = false;
				tracksById.insert(*valueOf(id), track);
			}
			QVector<int> candidates;
			for (const int master : selected.packages)
			{
				if (image.is(master, "SourcePackage"))
					continue;
				if (!image.is(master, "MaterialPackage"))
				{
					complete = false;
					continue;
				}
				const auto tracks = image.refs(master, "GenericPackage.Tracks");
				if (!valueOf(tracks))
				{
					complete = false;
					continue;
				}
				bool linked = false;
				for (const int track : *valueOf(tracks))
				{
					const auto root = image.target(track, "GenericTrack.Sequence");
					if (!image.is(track, "GenericTrack") || !valueOf(root))
					{
						complete = false;
						continue;
					}
					const auto kind = editorialDataDefinition(image, *valueOf(root));
					if (kind == EditorialDataKind::Timecode || kind == EditorialDataKind::Other)
						continue;
					if (kind == EditorialDataKind::Unreadable)
					{
						complete = false;
						continue;
					}
					const auto components = mediaComponents(image, *valueOf(root), kind);
					complete = complete && components.complete;
					for (const int component : components.sourceClips)
					{
						const auto raw = image.bytes(component, "SourceClip.SourcePackageID");
						if (valueOf(raw) && valueOf(raw)->size() == 32 &&
							std::all_of(valueOf(raw)->cbegin(), valueOf(raw)->cend(), [](char ch)
										{ return !ch; }))
							continue;
						const auto id = packageId(image, component, "SourceClip.SourcePackageID");
						if (!valueOf(id))
						{
							complete = false;
							continue;
						}
						if (*valueOf(id) != *file.fileMobId)
							continue;
						const auto target = image.integer(component, "SourceClip.SourceTrackID");
						if (!valueOf(target) || !tracksById.contains(*valueOf(target)))
						{
							complete = false;
							continue;
						}
						const auto fileRoot = image.target(tracksById[*valueOf(target)], "GenericTrack.Sequence");
						if (!valueOf(fileRoot) || editorialDataDefinition(image, *valueOf(fileRoot)) != kind)
						{
							complete = false;
							continue;
						}
						linked = true;
					}
				}
				if (linked)
					candidates.append(master);
			}
			if (!complete || candidates.size() != 1)
			{
				image.problem(selected.file, "master association",
							  QStringLiteral("No unique fully qualified master association was established; "
											 "independent file facts are retained."));
				return;
			}
			const auto id = packageId(image, candidates.first(), "GenericPackage.PackageUID");
			if (!valueOf(id))
			{
				image.problem(candidates.first(), "GenericPackage.PackageUID",
							  QStringLiteral("The associated master's complete identity is unavailable."));
				return;
			}
			clip.masterMobId = *valueOf(id);
			readClip(image, candidates.first(), details, clip);
		}

		// Private exact numbering qualification; it does not format Duration.

		struct NumberingFraction
		{
			quint64 numerator = 0;
			quint64 denominator = 1;
		};

		std::optional<NumberingFraction> numberingProduct(std::array<quint64, 3> numerators,
														  std::array<quint64, 3> denominators)
		{
			for (const auto denominator : denominators)
				if (denominator == 0)
					return {};
			for (auto &numerator : numerators)
				for (auto &denominator : denominators)
				{
					const auto common = std::gcd(numerator, denominator);
					numerator /= common;
					denominator /= common;
				}
			NumberingFraction value{1, 1};
			for (const auto numerator : numerators)
			{
				if (numerator != 0 && value.numerator > std::numeric_limits<quint64>::max() / numerator)
					return {};
				value.numerator *= numerator;
			}
			for (const auto denominator : denominators)
			{
				if (value.denominator > std::numeric_limits<quint64>::max() / denominator)
					return {};
				value.denominator *= denominator;
			}
			return value;
		}

		bool numberingPositive(const MxfRate &rate)
		{
			return rate.numerator > 0 && rate.denominator > 0;
		}

		bool numberingSameClock(const MxfRate &left, const MxfRate &right)
		{
			return numberingPositive(left) && numberingPositive(right) &&
				   qint64(left.numerator) * right.denominator == qint64(right.numerator) * left.denominator;
		}

		std::optional<NumberingFraction> numberingScale(NumberingFraction value, const MxfRate &from, const MxfRate &to)
		{
			if (!numberingPositive(from) || !numberingPositive(to))
				return {};
			return numberingProduct({value.numerator, quint64(from.denominator), quint64(to.numerator)},
									{value.denominator, quint64(from.numerator), quint64(to.denominator)});
		}

		std::optional<NumberingFraction> numberingAdd(NumberingFraction left, NumberingFraction right)
		{
			const auto common = std::gcd(left.denominator, right.denominator);
			const auto leftFactor = right.denominator / common;
			const auto rightFactor = left.denominator / common;
			const auto maximum = std::numeric_limits<quint64>::max();
			if (left.numerator > maximum / leftFactor || right.numerator > maximum / rightFactor ||
				left.denominator > maximum / leftFactor)
				return {};
			const auto first = left.numerator * leftFactor;
			const auto second = right.numerator * rightFactor;
			if (first > maximum - second)
				return {};
			const auto numerator = first + second;
			const auto denominator = left.denominator * leftFactor;
			const auto reduction = std::gcd(numerator, denominator);
			return NumberingFraction{numerator / reduction, denominator / reduction};
		}

		int numberingCompare(NumberingFraction left, NumberingFraction right)
		{
			// Continued fractions compare exact U64 values without cross multiplication.
			bool inverted = false;
			for (;;)
			{
				const auto leftWhole = left.numerator / left.denominator;
				const auto rightWhole = right.numerator / right.denominator;
				if (leftWhole != rightWhole)
					return (leftWhole < rightWhole) != inverted ? -1 : 1;
				const auto leftRemainder = left.numerator % left.denominator;
				const auto rightRemainder = right.numerator % right.denominator;
				if (leftRemainder == 0 || rightRemainder == 0)
				{
					if (leftRemainder == rightRemainder)
						return 0;
					return (leftRemainder == 0) != inverted ? -1 : 1;
				}
				left = {left.denominator, leftRemainder};
				right = {right.denominator, rightRemainder};
				inverted = !inverted;
			}
		}

		struct NumberingTrack
		{
			int handle = -1;
			int component = -1;
			EditorialDataKind kind = EditorialDataKind::Unreadable;
		};

		Read<QVector<NumberingTrack>> numberingTracks(const Image &image, int package)
		{
			const auto references = image.refs(package, "GenericPackage.Tracks");
			const auto *handles = valueOf(references);
			if (!handles)
				return std::get<Issue>(references);
			QVector<NumberingTrack> tracks;
			QVector<int> seen;
			for (const int handle : *handles)
			{
				if (seen.contains(handle) || !image.is(handle, "GenericTrack"))
					return Issue::Unreadable;
				seen.push_back(handle);
				const auto sequence = image.target(handle, "GenericTrack.Sequence");
				const auto *component = valueOf(sequence);
				if (!component)
					return std::get<Issue>(sequence);
				const auto kind = editorialDataDefinition(image, *component);
				if (kind == EditorialDataKind::Unreadable)
					return Issue::Unsupported;
				tracks.push_back({handle, *component, kind});
			}
			return tracks;
		}

		Read<MxfRate> numberingTrackClock(const Image &image, int track)
		{
			if (!image.is(track, "Track"))
				return Issue::Unsupported;
			const auto clock = image.rate(track, "Track.EditRate");
			const auto *value = valueOf(clock);
			if (!value)
				return std::get<Issue>(clock);
			return numberingPositive(*value) ? clock : Read<MxfRate>(Issue::Unreadable);
		}

		bool numberingZeroOrigin(const Image &image, int track)
		{
			const auto origin = image.integer(track, "Track.Origin");
			const auto *value = valueOf(origin);
			return value && *value == 0;
		}

		struct NumberingSpan
		{
			int component = -1;
			quint64 begin = 0;
			quint64 end = 0;
		};

		Read<QVector<NumberingSpan>> numberingSpans(const Image &image, int component, const char *leafClass,
													EditorialDataKind kind)
		{
			const auto duration = image.integer(component, "StructuralComponent.Duration");
			const auto *length = valueOf(duration);
			if (!length || *length < 0)
				return Issue::Unreadable;
			if (image.is(component, leafClass))
				return QVector<NumberingSpan>{{component, 0, quint64(*length)}};
			if (!image.is(component, "Sequence"))
				return Issue::Unsupported;
			const auto references = image.refs(component, "Sequence.StructuralComponents");
			const auto *children = valueOf(references);
			if (!children)
				return std::get<Issue>(references);
			QVector<NumberingSpan> spans;
			quint64 offset = 0;
			for (const int child : *children)
			{
				if (!image.is(child, leafClass) || editorialDataDefinition(image, child) != kind)
					return Issue::Unsupported;
				const auto childDuration = image.integer(child, "StructuralComponent.Duration");
				const auto *childLength = valueOf(childDuration);
				if (!childLength || *childLength < 0 || quint64(*childLength) > quint64(*length) - offset)
					return Issue::Unreadable;
				const auto end = offset + quint64(*childLength);
				spans.push_back({child, offset, end});
				offset = end;
			}
			return offset == quint64(*length) ? Read<QVector<NumberingSpan>>(spans)
											  : Read<QVector<NumberingSpan>>(Issue::Unreadable);
		}

		Read<bool> numberingTimecode(const Image &image, const QVector<NumberingTrack> &tracks,
									 const MxfRate &displayClock, quint64 nominal, NumberingFraction begin,
									 NumberingFraction end)
		{
			std::optional<bool> recorded;
			for (const auto &track : tracks)
			{
				if (track.kind != EditorialDataKind::Timecode)
					continue;
				const auto rate = numberingTrackClock(image, track.handle);
				const auto *clock = valueOf(rate);
				if (!clock)
					return std::get<Issue>(rate);
				if (!numberingSameClock(*clock, displayClock))
					continue;
				if (!numberingZeroOrigin(image, track.handle))
					return Issue::Unsupported;
				const auto components =
					numberingSpans(image, track.component, "TimecodeComponent", EditorialDataKind::Timecode);
				const auto *spans = valueOf(components);
				if (!spans)
					return std::get<Issue>(components);
				const NumberingFraction extent{spans->isEmpty() ? 0 : spans->last().end, 1};
				const bool point = numberingCompare(begin, end) == 0;
				if (numberingCompare(begin, extent) > 0 || (!point && numberingCompare(begin, extent) == 0))
					continue;
				if (numberingCompare(end, extent) > 0)
					return Issue::Unsupported;
				std::optional<bool> trackMode;
				bool missingMode = false;
				bool examined = false;
				for (const auto &span : *spans)
				{
					const auto beginsBeforeEnd = numberingCompare({span.begin, 1}, end);
					const auto endsAfterBegin = numberingCompare({span.end, 1}, begin);
					if (point ? beginsBeforeEnd > 0 || endsAfterBegin < 0 : beginsBeforeEnd >= 0 || endsAfterBegin <= 0)
						continue;
					examined = true;
					const auto base = image.integer(span.component, "TimecodeComponent.RoundedTimecodeBase");
					if (!valueOf(base) || *valueOf(base) != qint64(nominal))
						return Issue::Unreadable;
					const auto drop = image.integer(span.component, "TimecodeComponent.DropFrame");
					if (missing(drop))
					{
						missingMode = true;
						continue;
					}
					const auto *flag = valueOf(drop);
					if (!flag || (*flag != 0 && *flag != 1))
						return Issue::Unsupported;
					const bool mode = *flag == 1;
					if (trackMode && *trackMode != mode)
						return Issue::Conflict;
					trackMode = mode;
				}
				if (!examined || (missingMode && trackMode))
					return Issue::Unsupported;
				if (trackMode)
				{
					if (recorded && *recorded != *trackMode)
						return Issue::Conflict;
					recorded = *trackMode;
				}
			}
			return recorded ? Read<bool>(*recorded) : Read<bool>(Issue::Missing);
		}

		Read<NumberingTrack> numberingEssenceTrack(const Image &image, const QVector<NumberingTrack> &tracks,
												   EditorialDataKind kind, const Read<qint64> &wantedId)
		{
			if (!valueOf(wantedId) && !missing(wantedId))
				return std::get<Issue>(wantedId);
			std::optional<NumberingTrack> candidate;
			for (const auto &track : tracks)
			{
				if (track.kind != kind)
					continue;
				if (const auto *wanted = valueOf(wantedId))
				{
					const auto id = image.integer(track.handle, "GenericTrack.TrackID");
					if (!valueOf(id))
						return Issue::Unreadable;
					if (*valueOf(id) != *wanted)
						continue;
				}
				if (candidate)
					return Issue::Conflict;
				candidate = track;
			}
			return candidate ? Read<NumberingTrack>(*candidate) : Read<NumberingTrack>(Issue::Missing);
		}

		Read<int> numberingSourcePackage(const Image &image, const QVector<int> &packages, const QByteArray &wantedId,
										 int filePackage)
		{
			std::optional<int> candidate;
			for (const int package : packages)
			{
				if (!image.is(package, "GenericPackage"))
					return Issue::Unreadable;
				const auto id = image.bytes(package, "GenericPackage.PackageUID");
				const auto *bytes = valueOf(id);
				if (!bytes || bytes->size() != 32)
					return Issue::Unreadable;
				if (*bytes != wantedId)
					continue;
				if (!image.is(package, "SourcePackage"))
					return Issue::Unsupported;
				if (package == filePackage || candidate)
					return Issue::Conflict;
				candidate = package;
			}
			return candidate ? Read<int>(*candidate) : Read<int>(Issue::Unreadable);
		}

		bool numberingTerminalSource(const Image &image, const NumberingTrack &track, NumberingFraction begin,
									 NumberingFraction end)
		{
			const auto components = numberingSpans(image, track.component, "SourceClip", track.kind);
			const auto *spans = valueOf(components);
			if (!spans)
				return false;
			const bool point = numberingCompare(begin, end) == 0;
			for (const auto &span : *spans)
			{
				const auto beginsBeforeEnd = numberingCompare({span.begin, 1}, end);
				const auto endsAfterBegin = numberingCompare({span.end, 1}, begin);
				if (point ? beginsBeforeEnd > 0 || endsAfterBegin < 0 : beginsBeforeEnd >= 0 || endsAfterBegin <= 0)
					continue;
				const auto source = image.bytes(span.component, "SourceClip.SourcePackageID");
				const auto *id = valueOf(source);
				if (!id || *id != QByteArray(32, '\0'))
					return false;
			}
			return true;
		}

		MxfNumbering readNumbering(const Image &image, int filePackage, int descriptor, const QVector<int> &packages,
								   const MxfFileInformation &information)
		{
			const auto &clockValue =
				information.kind == MxfKind::Audio ? information.projectEditRate : information.sampleRate;
			if (!clockValue || !numberingPositive(*clockValue) ||
				(information.kind != MxfKind::Audio &&
				 (information.kind != MxfKind::Video || !information.frameRateQualified)))
				return MxfNumbering::Unavailable;
			const auto clock = *clockValue;
			quint64 nominal = 0;
			if (numberingSameClock(clock, {30000, 1001}))
				nominal = 30;
			else if (numberingSameClock(clock, {60000, 1001}))
				nominal = 60;
			else
				return MxfNumbering::NotRequired;

			const auto unavailable = [&]()
			{
				image.problem(
					filePackage, "GenericPackage.Tracks",
					QStringLiteral("Required numbering cannot be established from complete supported file/source, "
								   "track, interval, clock and flag relationships."));
				return MxfNumbering::Unavailable;
			};
			if (!information.sampleRate || !numberingPositive(*information.sampleRate) ||
				!information.containerDuration || *information.containerDuration < 0 || filePackage < 0)
				return unavailable();
			const auto duration = quint64(*information.containerDuration);
			const auto ownTracks = numberingTracks(image, filePackage);
			const auto *tracks = valueOf(ownTracks);
			if (!tracks)
				return unavailable();
			const auto end = numberingScale({duration, 1}, *information.sampleRate, clock);
			if (!end)
				return unavailable();
			const auto ownMode = numberingTimecode(image, *tracks, clock, nominal, {0, 1}, *end);
			if (!valueOf(ownMode) && !missing(ownMode))
				return unavailable();

			const auto essenceKind =
				information.kind == MxfKind::Video ? EditorialDataKind::Picture : EditorialDataKind::Sound;
			const auto linkedId = image.integer(descriptor, "FileDescriptor.LinkedTrackID");
			const auto ownEssence = numberingEssenceTrack(image, *tracks, essenceKind, linkedId);
			const auto *essence = valueOf(ownEssence);
			if (!essence)
				return unavailable();
			const auto ownClock = numberingTrackClock(image, essence->handle);
			if (!valueOf(ownClock) || !numberingSameClock(*valueOf(ownClock), *information.sampleRate) ||
				!numberingZeroOrigin(image, essence->handle))
				return unavailable();
			const auto components = numberingSpans(image, essence->component, "SourceClip", essenceKind);
			const auto *spans = valueOf(components);
			if (!spans || (spans->isEmpty() ? duration != 0 : spans->last().end < duration))
				return unavailable();

			std::optional<bool> sourceMode;
			bool missingSourceMode = false;
			for (const auto &span : *spans)
			{
				if (duration == 0 ? span.begin > 0 : span.begin >= duration || span.begin == span.end)
					continue;
				const auto pieceEnd = std::min(span.end, duration);
				const auto sourceId = image.bytes(span.component, "SourceClip.SourcePackageID");
				const auto *id = valueOf(sourceId);
				if (!id || id->size() != 32)
					return unavailable();
				if (*id == QByteArray(32, '\0'))
				{
					missingSourceMode = true;
					continue;
				}
				const auto source = numberingSourcePackage(image, packages, *id, filePackage);
				const auto *sourceHandle = valueOf(source);
				const auto sourceTrackId = image.integer(span.component, "SourceClip.SourceTrackID");
				const auto sourceStart = image.integer(span.component, "SourceClip.StartPosition");
				if (!sourceHandle || !valueOf(sourceTrackId) || *valueOf(sourceTrackId) <= 0 || !valueOf(sourceStart) ||
					*valueOf(sourceStart) < 0)
					return unavailable();
				const auto sourceTracks = numberingTracks(image, *sourceHandle);
				const auto *sourceTrackList = valueOf(sourceTracks);
				if (!sourceTrackList)
					return unavailable();
				const auto sourceEssence = numberingEssenceTrack(image, *sourceTrackList, essenceKind, sourceTrackId);
				const auto *sourceTrack = valueOf(sourceEssence);
				if (!sourceTrack || !numberingZeroOrigin(image, sourceTrack->handle))
					return unavailable();
				const auto sourceClock = numberingTrackClock(image, sourceTrack->handle);
				const auto sourceLength = image.integer(sourceTrack->component, "StructuralComponent.Duration");
				if (!valueOf(sourceClock) || !valueOf(sourceLength) || *valueOf(sourceLength) < 0)
					return unavailable();
				const auto offsetEnd =
					numberingScale({pieceEnd - span.begin, 1}, *valueOf(ownClock), *valueOf(sourceClock));
				if (!offsetEnd)
					return unavailable();
				const NumberingFraction mappedBegin{quint64(*valueOf(sourceStart)), 1};
				const auto mappedEnd = numberingAdd(mappedBegin, *offsetEnd);
				if (!mappedEnd || numberingCompare(*mappedEnd, {quint64(*valueOf(sourceLength)), 1}) > 0)
					return unavailable();
				const auto tcBegin = numberingScale(mappedBegin, *valueOf(sourceClock), clock);
				const auto tcEnd = numberingScale(*mappedEnd, *valueOf(sourceClock), clock);
				if (!tcBegin || !tcEnd)
					return unavailable();
				const auto mode = numberingTimecode(image, *sourceTrackList, clock, nominal, *tcBegin, *tcEnd);
				if (const auto *recorded = valueOf(mode))
				{
					if (sourceMode && *sourceMode != *recorded)
						return unavailable();
					sourceMode = *recorded;
				}
				else if (missing(mode))
				{
					if (!numberingTerminalSource(image, *sourceTrack, mappedBegin, *mappedEnd))
						return unavailable();
					missingSourceMode = true;
				}
				else
					return unavailable();
			}
			if (const auto *recorded = valueOf(ownMode))
			{
				if (sourceMode && *sourceMode != *recorded)
					return unavailable();
				return *recorded ? MxfNumbering::RecordedDf : MxfNumbering::RecordedNdf;
			}
			if (!sourceMode)
				return MxfNumbering::MissingNdfFallback;
			if (missingSourceMode)
				return *sourceMode ? unavailable() : MxfNumbering::MissingNdfFallback;
			return *sourceMode ? MxfNumbering::RecordedDf : MxfNumbering::RecordedNdf;
		}
		struct Stamp
		{
			std::optional<qint64> size;
			QDateTime modified;
		};
		Stamp observe(const QString &path)
		{
			const QFileInfo info(path);
			return info.exists() ? Stamp{info.size() >= 0 ? std::optional<qint64>(info.size()) : std::nullopt,
										 info.lastModified()}
								 : Stamp{};
		}
	} // namespace

	MxfReport parseMxf(const QString &path, const std::atomic_bool &stopRequested, bool includePrecomputeDetails)
	{
		MxfReport report;
		report.path = path;
		const auto cancelled = [&]()
		{
			if (!stopRequested.load(std::memory_order_relaxed))
				return false;
			report.reading = MxfReadResult::Cancelled;
			return true;
		};
		if (cancelled())
			return report;
		const auto before = observe(path);
		QFile source(path);
		if (cancelled())
			return report;
		const bool opened = source.open(QIODevice::ReadOnly);
		if (cancelled())
			return report;
		if (!opened)
		{
			report.reading = MxfReadResult::IoError;
			report.problemNotes.append(QStringLiteral("Cannot open MXF: %1").arg(source.errorString()));
			return report;
		}
		Input input{source, stopRequested};
		if (input.extent < 0 || source.isSequential())
			input.fail(MxfReadResult::IoError,
					   QStringLiteral("MXF requires a readable seekable file with an available extent."));
		{
			Image image(input, report.problemNotes);
			// Stage 1 establishes the complete selected region before publishing
			// facts; property spans fetch only required payloads and cache them.
			if (input.failure == MxfReadResult::NotRead && !input.cancelled() && readHeader(image) && !input.cancelled())
			{
				// Stage 2 resolves ownership and returns the two compact groups.
				const auto selected = selectFile(image, report.fileInformation);
				if (input.failure == MxfReadResult::NotRead)
					readProject(image, selected.preface, selected.file, report.fileInformation);
				if (input.failure == MxfReadResult::NotRead && selected.descriptor >= 0)
					readTechnical(image, selected.descriptor, selected.preface, image.partitionContainers,
								  report.fileInformation);
				if (input.failure == MxfReadResult::NotRead)
					selectMaster(image, selected, report.fileInformation, includePrecomputeDetails,
								 report.associatedClipInformation);
				if (input.failure == MxfReadResult::NotRead && selected.file >= 0)
					report.fileInformation.numbering = readNumbering(image, selected.file, selected.descriptor,
																	 selected.packages, report.fileInformation);
				if (input.failure == MxfReadResult::NotRead)
					input.cancelled();
			}
		} // Release bytes, mappings and object/reference workspace.
		const auto after = observe(path);
		source.close();
		report.reading = input.failure == MxfReadResult::NotRead ? MxfReadResult::Completed : input.failure;
		if (!input.explanation.isEmpty())
			report.problemNotes.append(input.explanation);
		const bool sizes = before.size && after.size;
		const bool dates = before.modified.isValid() && after.modified.isValid();
		if ((sizes && before.size != after.size) || (dates && before.modified != after.modified))
		{
			report.sizeDateCheck = MxfCheckResult::Changed;
			if (report.reading != MxfReadResult::Cancelled)
				report.reading = MxfReadResult::Changed;
			if (sizes && before.size != after.size)
				report.problemNotes.append(
					QStringLiteral(
						"MXF size changed during metadata reading from %1 to %2 bytes; its data is excluded.")
						.arg(*before.size)
						.arg(*after.size));
			if (dates && before.modified != after.modified)
				report.problemNotes.append(
					QStringLiteral(
						"MXF modification time changed during metadata reading from %1 to %2; its data is excluded.")
						.arg(before.modified.toUTC().toString(Qt::ISODateWithMs),
							 after.modified.toUTC().toString(Qt::ISODateWithMs)));
		}
		else
		{
			report.sizeDateCheck = sizes && dates ? MxfCheckResult::Verified : MxfCheckResult::Unverified;
			if (!sizes)
				report.problemNotes.append(QStringLiteral("Size check unavailable before or after MXF reading; "
														  "otherwise usable metadata remains unverified."));
			if (!dates)
				report.problemNotes.append(QStringLiteral("Modification-time check unavailable before or after MXF "
														  "reading; otherwise usable metadata remains unverified."));
		}
		if (report.sizeDateCheck == MxfCheckResult::Changed ||
			(report.reading != MxfReadResult::Completed && report.reading != MxfReadResult::Cancelled))
		{
			report.fileInformation = {};
			report.associatedClipInformation = {};
		}
		return report;
	}
} // namespace Bella
