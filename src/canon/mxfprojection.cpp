// Gives the fresh MXF graph file-owned meaning for the scan model. Package and
// descriptor references establish ownership; file order and a matching name do
// not. Header/footer copies remain separate observations for reconciliation.

#include "projection.h"
#include "dnxnames_p.h"
#include "mxfcatalogue_p.h"
#include "picturegeometry_p.h"
#include "avidusage.h"
#include "mediametadata.h"

#include <QHash>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <limits>

namespace Canon
{
	namespace
	{
		using Property = const RawProperty *;

		Property unique(const AvidObject &object, const char *name)
		{
			Property result = nullptr;
			for (const auto &property : object.properties)
				if (property.locator.name == QLatin1String(name))
				{
					if (property.state != PropertyReadState::Present || !property.decoded.isValid() ||
						(result && result->decoded != property.decoded))
						return nullptr;
					result = &property;
				}
			return result;
		}

		bool has(const AvidObject &object, const char *name)
		{
			return std::any_of(object.properties.cbegin(), object.properties.cend(), [&](const auto &property)
							   { return property.locator.name == QLatin1String(name); });
		}

		bool isClass(const AvidObject &object, const char *name)
		{
			if (!object.mxf)
				return false;
			QString current = object.mxf->name;
			while (!current.isEmpty() && current != QLatin1String("root"))
			{
				if (current == QLatin1String(name))
					return true;
				const auto found = std::find_if(std::begin(Detail::MxfSchema::sets), std::end(Detail::MxfSchema::sets),
												[&](const auto &set)
												{ return current == QLatin1String(set.name); });
				if (found == std::end(Detail::MxfSchema::sets))
					return false;
				current = QString::fromLatin1(found->parent);
			}
			return false;
		}

		std::optional<qint64> integer(Property property)
		{
			if (!property)
				return {};
			bool ok = false;
			const qint64 value = property->decoded.toLongLong(&ok);
			return ok ? std::optional<qint64>(value) : std::nullopt;
		}

		MediaRate rate(Property property)
		{
			return property ? mediaRate(property->decoded) : MediaRate{};
		}

		QString identity(Property property)
		{
			if (!property || property->decoded.metaType().id() != QMetaType::QByteArray)
				return {};
			const auto bytes = property->decoded.toByteArray();
			if (bytes.size() != 32 || std::all_of(bytes.cbegin(), bytes.cend(), [](char byte)
												  { return byte == 0; }))
				return {};
			return canonicalMxfId(bytes);
		}

		bool samePartition(const AvidObject &left, const AvidObject &right)
		{
			return left.mxf && right.mxf && left.mxf->partitionOffset == right.mxf->partitionOffset;
		}

		bool completeSet(const AvidObject &object)
		{
			if (!object.mxf || object.mxf->value.length <= 0)
				return false;
			qint64 position = object.mxf->value.offset;
			for (const auto &property : object.properties)
			{
				if (!property.mxf || property.mxf->mappedAuid.size() != 16 ||
					property.state != PropertyReadState::Present || !property.bytesRetained ||
					property.mxf->framingRanges.isEmpty())
					return false;
				for (const auto &range : property.mxf->framingRanges + property.locator.ranges)
				{
					if (range.offset != position || range.length < 0 ||
						range.length > std::numeric_limits<qint64>::max() - position)
						return false;
					position += range.length;
				}
			}
			return position - object.mxf->value.offset == object.mxf->value.length;
		}

		// The original component codes and depths remain independently available.
		struct NumericFormat
		{
			QString depth;
			QString format;
		};

		NumericFormat numericFormat(const QByteArray &coding, qint64 depth, bool picture = true)
		{
			// SMPTE RDD 50:2019, Tables 5, 7 and 8. The full coding label is
			// essential: a depth of 254 means different things in the two forms.
			static const QByteArray standard = QByteArray::fromHex("060e2b340401010d0401020203070100");
			static const QByteArray fixed = QByteArray::fromHex("060e2b340401010d0401020203070200");
			if (coding == standard)
			{
				if (depth == 253)
					return {QStringLiteral("16-bit"), QStringLiteral("Half float")};
				if (depth == 254)
					return {QStringLiteral("32-bit"), QStringLiteral("Float")};
				if (depth == 8 || depth == 10 || depth == 12 || depth == 16)
					return {QStringLiteral("%1-bit").arg(depth), QStringLiteral("Integer")};
				return {};
			}
			if (coding == fixed)
			{
				if (depth == 254 || depth == 10 || depth == 12)
					return {QStringLiteral("16-bit"), depth == 254 ? QStringLiteral("S2.14 fixed point") : depth == 10 ? QStringLiteral("10.6 fixed point")
																													   : QStringLiteral("12.4 fixed point")};
				return {};
			}
			// ST 377-1:2019 G.2.26/G.2.36 define these picture-component codes.
			// Audio QuantizationBits is a bit count, not this picture enumeration.
			if (picture && depth == 253)
				return {QStringLiteral("16-bit"), QStringLiteral("Half float")};
			if (picture && depth == 254)
				return {QStringLiteral("32-bit"), QStringLiteral("Float")};
			if (picture && depth == 255)
				return {QStringLiteral("64-bit"), QStringLiteral("64-bit float")};
			return {depth > 0 && depth <= (picture ? 32 : 64) ? QStringLiteral("%1-bit").arg(depth) : QString{},
					picture && depth > 0 && depth <= 32 ? QStringLiteral("Integer") : QString{}};
		}

		class Projector
		{
		public:
			Projector(const ParsedSource &source, const Cancellation &cancellation)
				: m_source(source), m_cancellation(cancellation)
			{
				for (const auto &object : source.objects)
				{
					if (cancellation.cancelled())
						break;
					m_objects.insert(object.handle, &object);
				}
				for (const auto &relationship : source.relationships)
				{
					if (cancellation.cancelled())
						break;
					m_links[relationship.origin].append(&relationship);
				}
			}

			Projection run()
			{
				if (m_source.container != ParsedSource::Container::Mxf)
					return {};
				QSet<ObjectHandle> projected;
				QSet<QString> fileIdentities;
				for (const auto &ecd : m_source.objects)
				{
					if (cancelled())
						break;
					if (!isClass(ecd, "EssenceContainerData"))
						continue;
					const auto *link = unique(ecd, "EssenceContainerData.LinkedPackageUID");
					const QString fileId = identity(link);
					if (fileId.isEmpty())
						continue;
					const auto packages = packagesWithId(ecd, fileId);
					if (packages.size() != 1 || !isClass(*packages.first(), "SourcePackage"))
					{
						m_result.diagnostics.append(QStringLiteral("MXF essence package %1 has no unique SourcePackage in partition %2.")
														.arg(fileId)
														.arg(ecd.mxf->partitionOffset));
						continue;
					}
					const auto &package = *packages.first();
					if (projected.contains(package.handle))
						continue;
					projected.insert(package.handle);
					fileIdentities.insert(fileId);
					ProjectedFile file;
					file.fileMobId = fileId;
					remember(file, package);
					remember(file, ecd);
					observe(file, MediaProperty::FileMobId, m_source, ecd, *link, fileId,
							EvidenceBasis::Derived, QStringLiteral("EssenceContainerData explicitly identifies this file's SourcePackage; UMID material bytes converted to the application's identity convention."));
					copy(file, package, "GenericPackage.PackageUID", MediaProperty::FileMobId, fileId);
					projectFile(file, package);
					m_result.files.append(std::move(file));
				}
				if (fileIdentities.size() > 1)
					m_result.diagnostics.append(QStringLiteral("MXF metadata identifies multiple essence packages; candidates are retained separately and no first package was selected."));
				if (m_result.files.isEmpty() && !cancelled())
					m_result.diagnostics.append(QStringLiteral("MXF file ownership was not established by an unambiguous EssenceContainerData/SourcePackage link; unowned source metadata remains available."));
				return std::move(m_result);
			}

		private:
			bool cancelled() const { return m_cancellation.cancelled(); }

			void remember(ProjectedFile &file, const AvidObject &object) const
			{
				file.evidence.registerSource(m_source.snapshot, QStringLiteral("object:%1").arg(object.handle));
				if (std::none_of(file.objects.cbegin(), file.objects.cend(), [&](const auto &reference)
								 { return reference.handle == object.handle; }))
					file.objects.append({m_source.snapshot, object.handle});
			}

			QVector<const AvidObject *> children(const AvidObject &object, const char *name) const
			{
				QVector<const AvidObject *> result;
				for (const auto *link : m_links.value(object.handle))
				{
					if (cancelled())
						break;
					if (link->locator.name != QLatin1String(name) || !link->target)
						continue;
					const auto *target = m_objects.value(link->target);
					if (target && samePartition(object, *target) && !result.contains(target))
						result.append(target);
				}
				return result;
			}

			QVector<const AvidObject *> packagesWithId(const AvidObject &context, const QString &id) const
			{
				QVector<const AvidObject *> result;
				for (const auto &object : m_source.objects)
				{
					if (cancelled())
						break;
					if (samePartition(context, object) && isClass(object, "GenericPackage") &&
						identity(unique(object, "GenericPackage.PackageUID")) == id)
						result.append(&object);
				}
				return result;
			}

			bool completeReferences(const AvidObject &object, const char *name) const
			{
				const auto *property = unique(object, name);
				if (!property)
					return false;
				const qsizetype expected = property->decoded.metaType().id() == QMetaType::QVariantList ? property->decoded.toList().size() : property->decoded.toByteArray().size() == 16 ? 1
																																														   : -1;
				QSet<ObjectHandle> targets;
				qsizetype count = 0;
				for (const auto *link : m_links.value(object.handle))
				{
					if (cancelled())
						return false;
					if (link->locator.name != QLatin1String(name))
						continue;
					const auto *target = m_objects.value(link->target);
					if (!target || !samePartition(object, *target) || targets.contains(link->target))
						return false;
					targets.insert(link->target);
					++count;
				}
				return count == expected;
			}

			void copy(ProjectedFile &file, const AvidObject &object, const char *name, MediaProperty field,
					  const QVariant &converted = {}) const
			{
				recordPropertyCoverage(file, field, m_source, object, {name}, completeSet(object));
				for (const auto &property : object.properties)
					if (property.locator.name == QLatin1String(name))
						observe(file, field, m_source, object, property, converted.isValid() ? converted : property.decoded,
								converted.isValid() ? EvidenceBasis::Derived : EvidenceBasis::Recorded);
			}

			QVector<const AvidObject *> componentGraph(const AvidObject &track) const
			{
				auto pending = children(track, "GenericTrack.Sequence");
				QVector<const AvidObject *> result;
				QSet<ObjectHandle> seen;
				while (!pending.isEmpty() && !cancelled())
				{
					const auto *object = pending.takeLast();
					if (seen.contains(object->handle))
						continue;
					seen.insert(object->handle);
					result.append(object);
					pending += children(*object, "Sequence.StructuralComponents");
					pending += children(*object, "EssenceGroup.Choices");
				}
				return result;
			}

			bool referencesFile(const AvidObject &track, const QString &fileId) const
			{
				for (const auto *component : componentGraph(track))
					if (isClass(*component, "SourceClip") && identity(unique(*component, "SourceClip.SourcePackageID")) == fileId)
						return true;
				return false;
			}

			MediaRate projectRate(const AvidObject &package) const
			{
				MediaRate result;
				for (const auto &object : m_source.objects)
				{
					if (cancelled())
						return {};
					if (!samePartition(package, object) || !isClass(object, "Preface"))
						continue;
					const MediaRate next = rate(unique(object, "Preface.ProjectEditRate"));
					if (next.valid())
					{
						if (result.valid() && !result.sameRate(next))
							return {};
						result = next;
					}
				}
				return result;
			}

			void projectFile(ProjectedFile &file, const AvidObject &package)
			{
				for (const auto &preface : m_source.objects)
				{
					if (cancelled())
						return;
					if (!samePartition(package, preface) || !isClass(preface, "Preface"))
						continue;
					remember(file, preface);
					copy(file, preface, "Preface.ProjectName", MediaProperty::Project);
					const auto *id = unique(preface, "Preface.EssenceFileMobID");
					const auto recordedId = identity(id);
					if (!recordedId.isEmpty())
					{
						observe(file, MediaProperty::FileMobId, m_source, preface, *id, recordedId, EvidenceBasis::Derived);
						if (recordedId != file.fileMobId)
							m_result.diagnostics.append(QStringLiteral("MXF Preface.EssenceFileMobID disagrees with the essence-container package in partition %1; both identities are retained.").arg(preface.mxf->partitionOffset));
					}
				}
				projectAttributes(file, package);
				projectOrigins(file, package);
				auto pending = children(package, "SourcePackage.Descriptor");
				QSet<ObjectHandle> seen;
				while (!pending.isEmpty() && !cancelled())
				{
					const auto *descriptor = pending.takeLast();
					if (seen.contains(descriptor->handle))
						continue;
					seen.insert(descriptor->handle);
					remember(file, *descriptor);
					if (isClass(*descriptor, "MultipleDescriptor"))
						pending += children(*descriptor, "MultipleDescriptor.SubDescriptorUIDs");
					else if (isClass(*descriptor, "FileDescriptor") ||
							 has(*descriptor, "GenericSoundEssenceDescriptor.AudioSamplingRate") ||
							 has(*descriptor, "GenericSoundEssenceDescriptor.SoundEssenceCompression") ||
							 has(*descriptor, "GenericPictureEssenceDescriptor.PictureEssenceCoding"))
						projectDescriptor(file, package, *descriptor);
				}
				projectMasters(file, package);
			}

			void projectAttributes(ProjectedFile &file, const AvidObject &package)
			{
				auto pending = children(package, "GenericPackage.MobAttributeList");
				pending += children(package, "GenericPackage.UserComments");
				QSet<ObjectHandle> seen;
				while (!pending.isEmpty() && !cancelled())
				{
					const auto *attribute = pending.takeLast();
					if (seen.contains(attribute->handle) || !isClass(*attribute, "TaggedValue"))
						continue;
					seen.insert(attribute->handle);
					remember(file, *attribute);
					const auto *name = unique(*attribute, "TaggedValue.Name");
					const auto *value = unique(*attribute, "TaggedValue.Value");
					if (name && value && value->decoded.metaType().id() == QMetaType::QString)
					{
						const auto key = name->decoded.toString();
						const auto text = value->decoded.toString();
						if (key == QLatin1String("_PJ") || key == QLatin1String("PROJNAME"))
							observe(file, MediaProperty::Project, m_source, *attribute, *value, text, EvidenceBasis::Derived,
									QStringLiteral("TaggedValue '%1' belongs to linked package object %2.").arg(key).arg(package.handle));
						else if (key == QLatin1String("UNC Path") && !text.isEmpty())
						{
							observe(file, MediaProperty::SourcePath, m_source, *attribute, *value, text, EvidenceBasis::Derived,
									QStringLiteral("UNC Path is a tagged value in the linked package attribute graph."));
							observe(file, MediaProperty::SourceFilename, m_source, *attribute, *value,
									MediaMetadataUtil::sourceFileBaseName(text), EvidenceBasis::Derived, QStringLiteral("Basename of the recorded import path."));
						}
						else if (key == QLatin1String("Video"))
							observe(file, MediaProperty::SourceContainer, m_source, *attribute, *value, text, EvidenceBasis::Derived,
									QStringLiteral("Avid import-settings Video tagged value; not this MXF file's container."));
						else if (key == QLatin1String("_IMPORTSETTING") && text == QLatin1String("__AttributeList") &&
								 !children(*attribute, "TaggedValue.TaggedValueAttributeList").isEmpty())
							observe(file, MediaProperty::Imported, m_source, *attribute, *value, true, EvidenceBasis::Derived,
									QStringLiteral("Linked package contains the recorded _IMPORTSETTING attribute-list object."));
					}
					pending += children(*attribute, "TaggedValue.TaggedValueAttributeList");
				}
			}

			void projectOrigins(ProjectedFile &file, const AvidObject &package)
			{
				QVector<const AvidObject *> pending{&package};
				QSet<ObjectHandle> seen;
				while (!pending.isEmpty() && !cancelled())
				{
					const auto *origin = pending.takeLast();
					if (seen.contains(origin->handle))
						continue;
					seen.insert(origin->handle);
					for (const auto *descriptor : children(*origin, "SourcePackage.Descriptor"))
						if (isClass(*descriptor, "ImportDescriptor"))
							for (const auto *locator : children(*descriptor, "GenericDescriptor.Locators"))
							{
								remember(file, *origin);
								remember(file, *descriptor);
								remember(file, *locator);
								copy(file, *locator, "NetworkLocator.URLString", MediaProperty::SourcePath);
								if (const auto *path = unique(*locator, "NetworkLocator.URLString"))
									observe(file, MediaProperty::SourceFilename, m_source, *locator, *path,
											QUrl(path->decoded.toString()).fileName(QUrl::FullyDecoded), EvidenceBasis::Derived,
											QStringLiteral("Filename of the explicitly linked import-source URL, with URI escapes decoded once. The recorded URL remains unchanged."));
							}
					for (const auto *track : children(*origin, "GenericPackage.Tracks"))
						for (const auto *component : componentGraph(*track))
						{
							const auto id = identity(unique(*component, "SourceClip.SourcePackageID"));
							if (id.isEmpty())
								continue;
							const auto matches = packagesWithId(*origin, id);
							if (matches.size() == 1 && isClass(*matches.first(), "SourcePackage"))
								pending.append(matches.first());
						}
				}
			}

			void projectDescriptor(ProjectedFile &file, const AvidObject &package, const AvidObject &descriptor)
			{
				// A registered property AUID can establish sound/picture meaning on
				// a newer descriptor class absent from our class catalogue (e.g. MPEG
				// audio). Its unknown class and other raw fields remain unchanged.
				const bool audio = isClass(descriptor, "GenericSoundEssenceDescriptor") ||
								   has(descriptor, "GenericSoundEssenceDescriptor.AudioSamplingRate") ||
								   has(descriptor, "GenericSoundEssenceDescriptor.SoundEssenceCompression");
				const bool video = isClass(descriptor, "GenericPictureEssenceDescriptor") ||
								   has(descriptor, "GenericPictureEssenceDescriptor.PictureEssenceCoding");
				const bool complete = completeSet(descriptor);
				recordPropertyCoverage(file, MediaProperty::Kind, m_source, descriptor,
					{"InterchangeObject.InstanceUID"}, complete);
				if (audio && video)
				{
					m_result.diagnostics.append(QStringLiteral("MXF descriptor object %1 has both sound and picture properties; its technical projection is unresolved.").arg(descriptor.handle));
					return;
				}
				const auto *anchor = unique(descriptor, "InterchangeObject.InstanceUID");
				if (anchor && (audio || video))
					observe(file, MediaProperty::Kind, m_source, descriptor, *anchor, audio ? 1 : 0, EvidenceBasis::Derived,
							QStringLiteral("The owning package references this descriptor; its registered class or Primer-mapped sound/picture property definitions establish Kind."));
				const auto *clock = unique(descriptor, "FileDescriptor.SampleRate");
				const MediaRate unitsRate = rate(clock);
				const MediaRate displayRate = audio ? projectRate(package) : unitsRate;
				if (video && unitsRate.valid())
					observe(file, MediaProperty::FrameRate, m_source, descriptor, *clock, rateValue(unitsRate));
				if (audio)
				{
					const auto *sampling = unique(descriptor, "GenericSoundEssenceDescriptor.AudioSamplingRate");
					if (rate(sampling).valid())
						observe(file, MediaProperty::SampleRate, m_source, descriptor, *sampling, rateValue(rate(sampling)));
					copy(file, descriptor, "GenericSoundEssenceDescriptor.ChannelCount", MediaProperty::Channels);
				}
				copy(file, descriptor, "FileDescriptor.EssenceContainer", MediaProperty::WrappingLabel);
				const char *codingName = audio ? "GenericSoundEssenceDescriptor.SoundEssenceCompression" : "GenericPictureEssenceDescriptor.PictureEssenceCoding";
				// Missing, malformed and not-yet-interpreted inputs have different
				// outcomes. A null unique() result alone cannot establish absence.
				recordPropertyCoverage(file, MediaProperty::Codec, m_source, descriptor, {codingName}, complete);
				recordPropertyCoverage(file, MediaProperty::FileDuration, m_source, descriptor,
					{"FileDescriptor.ContainerDuration", "FileDescriptor.SampleRate"}, complete);
				const char *depthName = audio ? "GenericSoundEssenceDescriptor.QuantizationBits" : "CDCIEssenceDescriptor.ComponentDepth";
				for (const auto field : {MediaProperty::BitDepth, MediaProperty::SampleFormat, MediaProperty::ComponentDepth})
					recordPropertyCoverage(file, field, m_source, descriptor,
						{depthName, "RGBAEssenceDescriptor.PixelLayout"}, complete);
				recordPropertyCoverage(file, MediaProperty::Alpha, m_source, descriptor,
					{"CDCIEssenceDescriptor.AlphaSampleDepth", "RGBAEssenceDescriptor.PixelLayout"}, complete);
				if (video)
				{
					recordPropertyCoverage(file, MediaProperty::FrameRate, m_source, descriptor,
						{"FileDescriptor.SampleRate"}, complete);
					recordPropertyCoverage(file, MediaProperty::Resolution, m_source, descriptor,
						{"GenericPictureEssenceDescriptor.StoredWidth", "GenericPictureEssenceDescriptor.StoredHeight",
						 "GenericPictureEssenceDescriptor.SampledWidth", "GenericPictureEssenceDescriptor.SampledHeight",
						 "GenericPictureEssenceDescriptor.DisplayWidth", "GenericPictureEssenceDescriptor.DisplayHeight",
						 "GenericPictureEssenceDescriptor.FrameLayout"}, complete);
				}
				if (audio)
				{
					recordPropertyCoverage(file, MediaProperty::SampleRate, m_source, descriptor,
						{"GenericSoundEssenceDescriptor.AudioSamplingRate"}, complete);
					file.evidence.recordReadStatus(MediaProperty::Resolution, m_source.snapshot,
						QStringLiteral("object:%1").arg(descriptor.handle),
						{PropertyReadState::NotRead, PropertyReadReason::None, PropertyApplicability::NotApplicable,
						 QStringLiteral("This sound descriptor does not describe a picture raster")});
				}
				copy(file, descriptor, codingName, MediaProperty::CompressionLabel);
				const auto *coding = unique(descriptor, codingName);
				const QByteArray label = coding ? coding->decoded.toByteArray() : QByteArray{};
				const auto *depth = unique(descriptor, audio ? "GenericSoundEssenceDescriptor.QuantizationBits" : "CDCIEssenceDescriptor.ComponentDepth");
				const auto bits = integer(depth);
				if (depth && bits)
				{
					observe(file, MediaProperty::ComponentDepth, m_source, descriptor, *depth, depth->decoded);
					const auto format = numericFormat(label, *bits, video);
					if (!format.depth.isEmpty())
						observe(file, MediaProperty::BitDepth, m_source, descriptor, *depth, format.depth, EvidenceBasis::Derived,
								QStringLiteral("Component depth interpreted with this descriptor's coding label; DNxUncompressed sentinels follow RDD 50 Tables 7–8."));
					if (!format.format.isEmpty())
						observe(file, MediaProperty::SampleFormat, m_source, descriptor, *depth, format.format, EvidenceBasis::Derived,
								QStringLiteral("Picture ComponentDepth uses ST 377-1:2019 G.2.26; DNxUncompressed coding variants refine it per RDD 50:2019 Tables 7–8."));
				}
				projectPixelLayout(file, descriptor, label);
				if (const auto *alpha = unique(descriptor, "CDCIEssenceDescriptor.AlphaSampleDepth"))
					if (const auto value = integer(alpha); value && *value >= 0)
						observe(file, MediaProperty::Alpha, m_source, descriptor, *alpha, *value != 0, EvidenceBasis::Derived,
								QStringLiteral("Explicit AlphaSampleDepth: zero records no alpha samples; positive records alpha."));
				const auto *duration = unique(descriptor, "FileDescriptor.ContainerDuration");
				if (const auto units = integer(duration); units && *units >= 0 && unitsRate.valid())
					observe(file, MediaProperty::FileDuration, m_source, descriptor, *duration,
							durationValue({*units, unitsRate, displayRate, MediaDuration::Source::Descriptor}), EvidenceBasis::Derived,
							QStringLiteral("ContainerDuration uses FileDescriptor.SampleRate from this same descriptor; display clock does not change its units."));
				else if (!has(descriptor, "FileDescriptor.ContainerDuration"))
					projectTrackDuration(file, package, descriptor, displayRate);
				projectVisibleGeometry(file, descriptor);
				if (coding && label.size() == 16)
					projectCodec(file, descriptor, *coding, label, unitsRate);
				else if (audio && isClass(descriptor, "WaveAudioDescriptor") && complete && !has(descriptor, codingName) && anchor)
				{
					observe(file, MediaProperty::Codec, m_source, descriptor, *anchor, QString::fromLatin1(kPcmAudioName), EvidenceBasis::Derived,
							QStringLiteral("WaveAudioDescriptor/AES3AudioDescriptor establishes PCM when SoundEssenceCompression is absent."));
					observe(file, MediaProperty::SampleFormat, m_source, descriptor, *anchor, QStringLiteral("Integer"), EvidenceBasis::Derived,
							QStringLiteral("The linked Wave/AES3 descriptor records PCM; no contradictory sound coding property is present."));
				}
			}

			void projectPixelLayout(ProjectedFile &file, const AvidObject &descriptor, const QByteArray &coding) const
			{
				copy(file, descriptor, "RGBAEssenceDescriptor.PixelLayout", MediaProperty::PixelLayout);
				const auto *layout = unique(descriptor, "RGBAEssenceDescriptor.PixelLayout");
				if (!layout || layout->decoded.metaType().id() != QMetaType::QVariantList)
					return;
				QStringList depths, formats;
				bool alpha = false, complete = true, terminated = false, palette = false;
				int activeComponents = 0;
				for (const auto &entry : layout->decoded.toList())
				{
					const auto component = entry.toMap();
					bool codeOk = false, depthOk = false;
					const auto code = component.value(QStringLiteral("Code")).toInt(&codeOk);
					const auto depth = component.value(QStringLiteral("Depth")).toLongLong(&depthOk);
					if (!codeOk || !depthOk || (terminated && (code != 0 || depth != 0)) ||
						(code == 0 && depth != 0) || (code != 0 && depth <= 0))
					{
						complete = false;
						break;
					}
					if (code == 0)
					{
						terminated = true;
						continue;
					}
					if (!QByteArrayLiteral("RGBArgbaFPUVWXYZuvwxyz").contains(char(code)) &&
						code != 0xd8 && code != 0xd9 && code != 0xda)
					{
						complete = false;
						break;
					}
					alpha = alpha || code == 'A' || code == 'a';
					palette = palette || code == 'P';
					if (code == 'F')
						continue; // Filler components do not describe colour/alpha precision.
					++activeComponents;
					const auto interpreted = numericFormat(coding, depth);
					if (!interpreted.depth.isEmpty())
						depths.append(QStringLiteral("%1: %2").arg(QChar(ushort(code))).arg(interpreted.depth));
					if (!interpreted.format.isEmpty())
						formats.append(QStringLiteral("%1: %2").arg(QChar(ushort(code))).arg(interpreted.format));
				}
				if (!complete || activeComponents == 0 || layout->decoded.toList().size() != 8)
					return;
				// A palette index does not describe the palette's colour or alpha.
				// Its separate PaletteLayout remains raw evidence until projected.
				if (palette)
					return;
				observe(file, MediaProperty::Alpha, m_source, descriptor, *layout, alpha, EvidenceBasis::Derived,
						QStringLiteral("The complete eight-slot RGBA PixelLayout explicitly records its component codes; A/a denotes alpha. Palette layouts require a separate interpretation."));
				// Keep different colour and alpha formats distinct instead of taking
				// the first component, even when the table eventually abbreviates it.
				const auto compact = [](const QStringList &components)
				{
					QStringList distinct;
					for (const auto &component : components)
					{
						const auto value = component.mid(component.indexOf(QLatin1String(": ")) + 2);
						if (!distinct.contains(value))
							distinct.append(value);
					}
					return distinct.size() == 1 ? distinct.first() : components.join(QStringLiteral("; "));
				};
				if (!depths.isEmpty())
					observe(file, MediaProperty::BitDepth, m_source, descriptor, *layout, compact(depths), EvidenceBasis::Derived,
							QStringLiteral("All interpreted PixelLayout component depths; differing component values remain labelled."));
				if (!formats.isEmpty())
					observe(file, MediaProperty::SampleFormat, m_source, descriptor, *layout, compact(formats), EvidenceBasis::Derived,
							QStringLiteral("Each component uses ST 377-1:2019 G.2.36; DNxUncompressed coding variants refine the representation per RDD 50."));
			}

			QPair<qint64, qint64> frameGeometry(const AvidObject &descriptor, const char *prefix) const
			{
				const QByteArray base = QByteArray("GenericPictureEssenceDescriptor.") + prefix;
				const auto width = integer(unique(descriptor, (base + "Width").constData()));
				const auto height = integer(unique(descriptor, (base + "Height").constData()));
				const auto layout = integer(unique(descriptor, "GenericPictureEssenceDescriptor.FrameLayout"));
				if (!width || !height || *width <= 0 || *height <= 0 || !layout || *layout < 0 || *layout > 4)
					return {};
				if (*layout == 1 && *height > std::numeric_limits<qint64>::max() / 2)
					return {};
				return {*width, *layout == 1 ? *height * 2 : *height};
			}

			void projectVisibleGeometry(ProjectedFile &file, const AvidObject &descriptor) const
			{
				const auto layout = integer(unique(descriptor, "GenericPictureEssenceDescriptor.FrameLayout"));
				if (!layout || *layout < 0 || *layout > 4)
					return;
				const bool complete = completeSet(descriptor);
				const auto read = [&](const QByteArray &name, std::optional<qint64> fallback)
				{
					return has(descriptor, name.constData()) ? integer(unique(descriptor, name.constData()))
						   : complete						 ? fallback
															 : std::nullopt;
				};
				const auto rectangle = [&](const char *prefix, const Detail::PictureRectangle &fallback)
				{
					const QByteArray base = QByteArray("GenericPictureEssenceDescriptor.") + prefix;
					return Detail::PictureRectangle{read(base + "Width", fallback.width), read(base + "Height", fallback.height),
													read(base + "XOffset", 0), read(base + "YOffset", 0)};
				};
				const auto recorded = [&](const char *prefix)
				{
					const QByteArray base = QByteArray("GenericPictureEssenceDescriptor.") + prefix;
					return has(descriptor, (base + "Width").constData()) || has(descriptor, (base + "Height").constData()) ||
						   has(descriptor, (base + "XOffset").constData()) || has(descriptor, (base + "YOffset").constData());
				};
				Detail::PictureGeometry geometry;
				geometry.stored = {integer(unique(descriptor, "GenericPictureEssenceDescriptor.StoredWidth")),
								   integer(unique(descriptor, "GenericPictureEssenceDescriptor.StoredHeight")), 0, 0};
				geometry.sampled = rectangle("Sampled", geometry.stored);
				geometry.display = rectangle("Display", geometry.sampled);
				geometry.sampledRecorded = recorded("Sampled");
				geometry.displayRecorded = recorded("Display");
				geometry.displayRelativeToSampled = true;
				geometry.layout = layout;
				geometry.heightMultiplier = *layout == 1 ? 2 : 1;
				if (const auto *coding = unique(descriptor, "GenericPictureEssenceDescriptor.PictureEssenceCoding"))
					geometry.coding = coding->decoded.toByteArray();
				geometry.resolutionId = integer(unique(descriptor, "GenericPictureEssenceDescriptor.ResolutionID"));
				const auto selected = Detail::visibleGeometry(geometry);
				if (selected.origin == Detail::VisibleGeometry::Origin::Unknown)
					return;
				const auto *prefix = selected.origin == Detail::VisibleGeometry::Origin::Display   ? "Display"
									 : selected.origin == Detail::VisibleGeometry::Origin::Sampled ? "Sampled"
																								   : "Stored";
				const QByteArray base = QByteArray("GenericPictureEssenceDescriptor.") + prefix;
				const auto *anchor = unique(descriptor, (base + "Width").constData());
				if (!anchor)
					anchor = unique(descriptor, "GenericPictureEssenceDescriptor.StoredWidth");
				const QString reason = selected.origin == Detail::VisibleGeometry::Origin::VerifiedProxy
										   ? QStringLiteral("Verified Avid H.264 descriptor configuration (ResolutionID %1, coding label, all three rasters, layout and zero offsets) selects this file's stored proxy raster. Matching specimens were independently checked with ffprobe; this is a qualified inference, not a universal proxy flag.").arg(*geometry.resolutionId)
										   : QStringLiteral("Visible raster: Sampled is validated within Stored, and Display within Sampled, including offsets, under ST 377-1 Annex G. Absent optional properties use that format's defaults; unreadable/conflicting properties do not. FrameLayout supplies field-height handling; original geometry remains retained.");
				observe(file, MediaProperty::Resolution, m_source, descriptor, *anchor,
						QStringLiteral("%1x%2").arg(selected.width).arg(selected.height), EvidenceBasis::Derived, reason);
			}

			QPair<qint64, qint64> dnxNamingGeometry(const AvidObject &descriptor) const
			{
				// DNx operating-point names use the recorded active raster. Keep
				// that existing rule independent of table-resolution selection.
				for (const auto *prefix : {"Display", "Sampled", "Stored"})
				{
					const auto geometry = frameGeometry(descriptor, prefix);
					if (geometry.first > 0)
						return geometry;
				}
				return {};
			}

			void projectCodec(ProjectedFile &file, const AvidObject &descriptor, const RawProperty &coding,
							  const QByteArray &label, MediaRate editRate) const
			{
				QString codec = MediaMetadataUtil::codecFromCompressionLabel(label, {});
				for (const auto &profile : Detail::dnxProfiles)
				{
					if (label != QByteArray::fromHex(profile.label))
						continue;
					const auto level = QString::fromLatin1(profile.level);
					const QString newDnx = QStringLiteral("Avid DNx %1").arg(level);
					const QString oldDnx = QStringLiteral("DNx%1 %2").arg(profile.hr ? QLatin1String("HR") : QLatin1String("HD"), level);
					observe(file, MediaProperty::NewDnx, m_source, descriptor, coding, newDnx, EvidenceBasis::Derived,
							QStringLiteral("Exact registered coding-label profile mapped to Avid's unified DNx branding."));
					observe(file, MediaProperty::OldDnx, m_source, descriptor, coding, oldDnx, EvidenceBasis::Derived,
							QStringLiteral("Historical HD/HR profile identity comes from the coding label, not image dimensions."));
					codec = newDnx;
					const auto layout = integer(unique(descriptor, "GenericPictureEssenceDescriptor.FrameLayout"));
					const auto depth = integer(unique(descriptor, "CDCIEssenceDescriptor.ComponentDepth"));
					const auto horizontal = integer(unique(descriptor, "CDCIEssenceDescriptor.HorizontalSubsampling"));
					const auto recordedVertical = integer(unique(descriptor, "CDCIEssenceDescriptor.VerticalSubsampling"));
					const auto vertical = recordedVertical ? recordedVertical : !has(descriptor, "CDCIEssenceDescriptor.VerticalSubsampling") && completeSet(descriptor) ? std::optional<qint64>(1)
																																										 : std::nullopt; // ST 377-1:2019 G.2.28 default.
					const auto reallyOld = Detail::reallyOldDnx(profile, dnxNamingGeometry(descriptor), editRate, layout, depth, horizontal, vertical);
					if (!reallyOld.isEmpty())
					{
						observe(file, MediaProperty::ReallyOldDnx, m_source, descriptor, coding, reallyOld, EvidenceBasis::Derived,
								QStringLiteral("Exact HD profile, raster, frame layout, sampling, depth and rational rate; Avid 2012 white paper pp. 9–10. No nearest-rate match or thin-raster substitution."));
						codec += QStringLiteral(" [%1]").arg(reallyOld);
					}
					break;
				}
				if (label == QByteArray::fromHex("060e2b340401010d0401020203070100") ||
					label == QByteArray::fromHex("060e2b340401010d0401020203070200"))
				{
					codec = QStringLiteral("Avid DNxUncompressed");
					const auto descriptorValue = [&](MediaProperty field)
					{
						QString result;
						for (const auto &item : file.evidence.observations(field))
							if (item.objectIdentity == QStringLiteral("object:%1").arg(descriptor.handle) &&
								item.readState == PropertyReadState::Present)
							{
								if (!result.isEmpty() && result != item.value.toString())
									return QString{};
								result = item.value.toString();
							}
						return result;
					};
					const auto depth = descriptorValue(MediaProperty::BitDepth);
					const auto format = descriptorValue(MediaProperty::SampleFormat);
					if (!depth.isEmpty() && !format.isEmpty())
						codec += QStringLiteral(" — %1 %2").arg(depth, format);
				}
				observe(file, MediaProperty::Codec, m_source, descriptor, coding, codec, EvidenceBasis::Derived,
						QStringLiteral("Coding-label lookup; any historical DNx numbered alias requires the exact checked operating point. The original label remains separately available."));
			}

			void projectTrackDuration(ProjectedFile &file, const AvidObject &package,
									  const AvidObject &descriptor, MediaRate displayRate) const
			{
				const auto linkedTrack = integer(unique(descriptor, "FileDescriptor.LinkedTrackID"));
				const auto tracks = children(package, "GenericPackage.Tracks");
				if (!linkedTrack && tracks.size() != 1)
					return;
				for (const auto *track : tracks)
				{
					if (cancelled())
						return;
					if (linkedTrack && integer(unique(*track, "GenericTrack.TrackID")) != linkedTrack)
						continue;
					const auto clock = rate(unique(*track, "Track.EditRate"));
					for (const auto *sequence : children(*track, "GenericTrack.Sequence"))
					{
						const auto *property = unique(*sequence, "StructuralComponent.Duration");
						const auto length = integer(property);
						if (length && *length >= 0 && clock.valid())
							observe(file, MediaProperty::FileDuration, m_source, *sequence, *property,
									durationValue({*length, clock, displayRate, MediaDuration::Source::FileTrack}), EvidenceBasis::Derived,
									QStringLiteral("Descriptor duration is absent; linked file-track length and its own edit rate provide the fallback. No master-track minimum or audio-rate guess."));
					}
				}
			}

			QVector<const AvidObject *> timecodes(const AvidObject &package) const
			{
				QVector<const AvidObject *> pending, result;
				for (const auto *track : children(package, "GenericPackage.Tracks"))
					pending += children(*track, "GenericTrack.Sequence");
				QSet<ObjectHandle> visited;
				while (!pending.isEmpty() && !cancelled())
				{
					const auto *component = pending.takeLast();
					if (visited.contains(component->handle))
						continue;
					visited.insert(component->handle);
					if (isClass(*component, "TimecodeComponent"))
						result.append(component);
					else if (isClass(*component, "Sequence"))
						pending += children(*component, "Sequence.StructuralComponents");
				}
				return result;
			}

			QVariant projectDropFrame(ProjectedFile &file, const AvidObject &package) const
			{
				QVariant selected;
				bool conflict = false;
				for (const auto *timecode : timecodes(package))
					if (const auto *drop = unique(*timecode, "TimecodeComponent.DropFrame");
						drop && drop->decoded.metaType().id() == QMetaType::Bool)
					{
						remember(file, *timecode);
						observe(file, MediaProperty::DropFrame, m_source, *timecode, *drop, drop->decoded,
								EvidenceBasis::Recorded, QStringLiteral("Drop-frame flag from a timecode component belonging to this file or an explicitly linked material package; never inferred from frame rate."));
						conflict |= selected.isValid() && selected != drop->decoded;
						selected = drop->decoded;
					}
				return conflict ? QVariant{} : selected;
			}

			void projectMasters(ProjectedFile &file, const AvidObject &package)
			{
				QVariantList durations;
				projectDropFrame(file, package);
				const auto displayRate = projectRate(package);
				for (const auto &master : m_source.objects)
				{
					if (cancelled())
						return;
					if (!samePartition(package, master) || !isClass(master, "MaterialPackage"))
						continue;
					QVector<const AvidObject *> referencingTracks;
					for (const auto *track : children(master, "GenericPackage.Tracks"))
						if (referencesFile(*track, file.fileMobId))
							referencingTracks.append(track);
					if (referencingTracks.isEmpty())
						continue;
					const auto *id = unique(master, "GenericPackage.PackageUID");
					const auto masterId = identity(id);
					if (masterId.isEmpty() || packagesWithId(master, masterId).size() != 1)
						continue;
					remember(file, master);
					if (!file.masterMobIds.contains(masterId))
						file.masterMobIds.append(masterId);
					observe(file, MediaProperty::MasterMobId, m_source, master, *id, masterId, EvidenceBasis::Derived,
							QStringLiteral("This MaterialPackage's referenced SourceClip explicitly names the file SourcePackage."));
					copy(file, master, "GenericPackage.Name", MediaProperty::ClipName);
					projectAttributes(file, master);
					projectClassification(file, master, *id);
					const auto dropFrame = projectDropFrame(file, master);
					for (const auto *track : referencingTracks)
					{
						remember(file, *track);
						const auto trackId = integer(unique(*track, "GenericTrack.TrackID"));
						const auto clock = rate(unique(*track, "Track.EditRate"));
						if (!trackId || *trackId < 0 || *trackId > std::numeric_limits<quint32>::max() || !clock.valid())
							continue;
						for (const auto *sequence : children(*track, "GenericTrack.Sequence"))
						{
							remember(file, *sequence);
							const auto *length = unique(*sequence, "StructuralComponent.Duration");
							const auto count = integer(length);
							if (!count || *count < 0)
								continue;
							const auto display = displayRate.valid() ? displayRate : clock;
							durations.append(QVariantMap{{QStringLiteral("MasterMobId"), masterId},
														 {QStringLiteral("TrackId"), quint32(*trackId)},
														 {QStringLiteral("DropFrame"), dropFrame},
														 {QStringLiteral("DurationObject"), sequence->handle},
														 {QStringLiteral("DurationProperty"), length->locator.name},
														 {QStringLiteral("Duration"), durationValue({*count, clock, display, MediaDuration::Source::ClipReference})}});
						}
					}
				}
				for (const auto &preface : m_source.objects)
				{
					if (cancelled())
						return;
					if (!samePartition(package, preface) || !isClass(preface, "Preface"))
						continue;
					const auto *property = unique(preface, "Preface.MasterMobID");
					const auto masterId = identity(property);
					if (masterId.isEmpty())
						continue;
					auto evidence = observation(m_source, preface, *property, masterId, EvidenceBasis::Derived,
												QStringLiteral("Preface master identity corroborates a track-to-file association only when that graph relationship is established."));
					evidence.eligible = file.masterMobIds.contains(masterId);
					file.evidence.observe(MediaProperty::MasterMobId, std::move(evidence));
					if (!file.masterMobIds.isEmpty() && !file.masterMobIds.contains(masterId))
						m_result.diagnostics.append(QStringLiteral("MXF Preface.MasterMobID is not among the file's established material-package associations; the observation remains ineligible and retained."));
				}
				// One list preserves independent tracks without making different track
				// durations appear to be competing answers to one file-duration field.
				if (!durations.isEmpty())
					if (const auto *id = unique(package, "GenericPackage.PackageUID"))
						observe(file, MediaProperty::ClipDuration, m_source, package, *id, durations, EvidenceBasis::Derived,
								QStringLiteral("Independent linked-master track lengths; each entry retains master ID, TrackID, original clock and display clock. Unrecorded drop-frame status is omitted."));
			}

			void projectClassification(ProjectedFile &file, const AvidObject &master, const RawProperty &anchor) const
			{
				qint32 appCode = AvidUsage::kMissing;
				if (has(master, "GenericPackage.AppCode"))
				{
					const auto value = integer(unique(master, "GenericPackage.AppCode"));
					appCode = value && *value >= 0 && *value <= std::numeric_limits<qint32>::max() ? qint32(*value) : AvidUsage::kInvalidOrConflicting;
				}
				AvidUsage::StandardUsage standard = AvidUsage::StandardUsage::Absent;
				bool sawUsage = false;
				static const QByteArray usageKey = QByteArray::fromHex("060e2b34010101070501010800000000");
				for (const auto &property : master.properties)
				{
					if (!property.mxf)
						continue;
					if (property.mxf->mappedAuid.isEmpty())
						return; // An unmapped property might be usage; absence is unproved.
					if (property.mxf->mappedAuid != usageKey)
						continue;
					const auto next = property.state == PropertyReadState::Present ? AvidUsage::standardUsage(property.encoding) : AvidUsage::StandardUsage::Unknown;
					standard = sawUsage && standard != next ? AvidUsage::StandardUsage::Unknown : next;
					sawUsage = true;
				}
				if ((appCode == AvidUsage::kMissing || !sawUsage) && !completeSet(master))
					return;
				const auto classification = AvidUsage::materialClassification(appCode, standard);
				if (classification != AvidUsage::Classification::Unknown)
					observe(file, MediaProperty::Type, m_source, master, anchor,
							classification == AvidUsage::Classification::Precompute ? 1 : 0, EvidenceBasis::Derived,
							QStringLiteral("Established MaterialPackage and Avid application usage plus standard UsageCode; absent usage defaults to ordinary media only for a completed, fully mapped read."));
				if (classification == AvidUsage::Classification::Precompute)
					projectPrecompute(file, master, anchor);
			}

			void projectPrecompute(ProjectedFile &file, const AvidObject &master, const RawProperty &anchor) const
			{
				// MC 26.8 brDisplayable tests the direct master's object attribute,
				// then its immediate track kinds. A nested occurrence of the same
				// name or an unrelated picture descriptor cannot establish either.
				AvidPrecompute::Evidence evidence;
				using Import = AvidPrecompute::ImportAttribute;
				if (!has(master, "GenericPackage.MobAttributeList"))
					evidence.importAttribute = completeSet(master) ? Import::Absent : Import::Unknown;
				else if (completeReferences(master, "GenericPackage.MobAttributeList"))
				{
					evidence.importAttribute = Import::Absent;
					bool seen = false;
					for (const auto *attribute : children(master, "GenericPackage.MobAttributeList"))
					{
						const auto *name = unique(*attribute, "TaggedValue.Name");
						if (!isClass(*attribute, "TaggedValue") || !name)
						{
							evidence.importAttribute = Import::Unknown;
							break;
						}
						if (name->decoded.toString() != QLatin1String("_IMPORTSETTING"))
							continue;
						if (seen)
						{
							evidence.importAttribute = Import::Conflicting;
							break;
						}
						seen = true;
						const auto *value = unique(*attribute, "TaggedValue.Value");
						if (!value || value->decoded.metaType().id() != QMetaType::QString ||
							value->decoded.toString() == QLatin1String("__PortableObject"))
						{
							evidence.importAttribute = Import::Unknown;
							break;
						}
						if (value->decoded.toString() != QLatin1String("__AttributeList"))
							continue;
						if (!completeReferences(*attribute, "TaggedValue.TaggedValueAttributeList"))
						{
							evidence.importAttribute = Import::Unknown;
							break;
						}
						evidence.importAttribute = Import::Present;
					}
				}
				if (evidence.importAttribute == Import::Present && completeReferences(master, "GenericPackage.Tracks"))
				{
					int videos = 0;
					bool complete = true;
					for (const auto *track : children(master, "GenericPackage.Tracks"))
					{
						const auto segments = children(*track, "GenericTrack.Sequence");
						if (!isClass(*track, "GenericTrack") || !completeReferences(*track, "GenericTrack.Sequence") || segments.size() != 1)
						{
							complete = false;
							break;
						}
						const auto *definition = unique(*segments.first(), "StructuralComponent.DataDefinition");
						QByteArray label = definition ? definition->decoded.toByteArray() : QByteArray{};
						const auto definitions = children(*segments.first(), "StructuralComponent.DataDefinition");
						if (definitions.size() == 1)
							if (const auto *id = unique(*definitions.first(), "DefinitionObject.Identification"))
								label = id->decoded.toByteArray();
						if (label == QByteArray::fromHex("060e2b34040101010103020201000000") ||
							label == QByteArray::fromHex("807d006008143e6f6f3c8ce16cef11d2"))
							++videos;
						else if (label != QByteArray::fromHex("060e2b34040101010103020202000000") &&
								 label != QByteArray::fromHex("807d006008143e6f78e1ebe16cef11d2") &&
								 label != QByteArray::fromHex("060e2b34040101010103020101000000") &&
								 label != QByteArray::fromHex("807f006008143e6f7f275e8177e511d2"))
							complete = false;
					}
					if (complete)
						evidence.videoTrackCount = videos;
				}
				const auto category = AvidPrecompute::classify(evidence);
				if (category != AvidPrecompute::Category::Unknown)
					observe(file, MediaProperty::PrecomputeCategory, m_source, master, anchor, int(category), EvidenceBasis::Derived,
							QStringLiteral("Verified precompute master: direct _IMPORTSETTING object presence and direct picture-track count follow MC 26.8 brDisplayable; incomplete lists do not prove absence."));
			}

			const ParsedSource &m_source;
			const Cancellation &m_cancellation;
			QHash<ObjectHandle, const AvidObject *> m_objects;
			QHash<ObjectHandle, QVector<const Relationship *>> m_links;
			Projection m_result;
		};
	}

	Projection projectMxf(const ParsedSource &source, const Cancellation &cancellation)
	{
		return Projector(source, cancellation).run();
	}
}
