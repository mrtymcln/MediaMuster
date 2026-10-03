#pragma once

#include "mediafile.h"
#include "mediametadata.h"
#include <numeric>

/// Bridge from existing decoded aggregates. Locators explicitly describe the
/// aggregate when the reader does not yet expose an exact object/property offset.
/// Unknown aggregate fields are NotRead, never fabricated format-level absence.
namespace MediaObservations
{
	inline void qualifyTechnical(MediaFile &file, const SourceSnapshotRef &source, bool eligible,
		SourceFreshness freshness = SourceFreshness::Unknown)
	{
		for (MediaProperty property : {MediaProperty::Codec, MediaProperty::Resolution, MediaProperty::FrameRate,
			MediaProperty::SampleRate, MediaProperty::BitDepth, MediaProperty::SampleFormat, MediaProperty::Kind,
			MediaProperty::FileDuration, MediaProperty::Channels})
			file.evidence.qualify(property, source, eligible, freshness);
	}
	inline void add(MediaFile &file, MediaProperty field, const SourceSnapshotRef &source,
		const QString &property, const QVariant &value, EvidenceBasis basis = EvidenceBasis::Recorded,
		const QVariant &raw = {}, const QString &owner = {})
	{
		MetadataObservation observation;
		observation.snapshot = source;
		observation.property = property;
		observation.objectIdentity = owner;
		observation.value = value;
		observation.rawValue = raw;
		observation.basis = basis;
		observation.eligible = source->readState != SourceReadState::Unreadable;
		const bool known = value.isValid() && !(value.metaType().id() == QMetaType::QString && value.toString().isEmpty());
		observation.readState = known ? PropertyReadState::Present : PropertyReadState::NotRead;
		if (!known && source->readState == SourceReadState::Unreadable)
			observation.readState = PropertyReadState::Unreadable;
		if (basis == EvidenceBasis::Derived)
			observation.explanation = QStringLiteral("Derived by the current format reader from the named inputs; raw inputs retained when available");
		file.evidence.observe(field, std::move(observation));
	}
	inline QVariant rate(MediaRate value)
	{
		if (!value.valid())
			return {};
		const qint32 divisor = std::gcd(value.numerator, value.denominator);
		return QVariantList{value.numerator / divisor, value.denominator / divisor};
	}
	inline void metadata(MediaFile &file, const MediaMetadata &meta, const SourceSnapshotRef &source)
	{
		const QString owner = meta.fileMobId;
		add(file, MediaProperty::Codec, source, QStringLiteral("selected descriptor coding / codec lookup"), meta.codec,
			EvidenceBasis::Derived, meta.compressionLabel, owner);
		add(file, MediaProperty::CompressionLabel, source, QStringLiteral("selected descriptor essence coding"),
			meta.compressionLabel.isEmpty() ? QVariant{} : QVariant(meta.compressionLabel), EvidenceBasis::Recorded, meta.compressionLabel, owner);
		add(file, MediaProperty::WrappingLabel, source, QStringLiteral("selected descriptor essence container"),
			meta.wrappingLabel.isEmpty() ? QVariant{} : QVariant(meta.wrappingLabel), EvidenceBasis::Recorded, meta.wrappingLabel, owner);
		add(file, MediaProperty::Resolution, source, QStringLiteral("selected descriptor raster / reader geometry rule"),
			meta.resolution, EvidenceBasis::Derived, QVariantList{meta.width, meta.height, meta.frameLayout}, owner);
		add(file, MediaProperty::FrameRate, source, QStringLiteral("selected descriptor video rate"), rate(meta.frameRateRatio),
			EvidenceBasis::Recorded, QVariantList{meta.frameRateRatio.numerator, meta.frameRateRatio.denominator, meta.frameRate}, owner);
		add(file, MediaProperty::SampleRate, source, QStringLiteral("selected audio descriptor sampling rate"), rate(meta.sampleRateRatio),
			EvidenceBasis::Recorded, QVariantList{meta.sampleRateRatio.numerator, meta.sampleRateRatio.denominator, meta.sampleRateEncoding, meta.sampleRate}, owner);
		add(file, MediaProperty::BitDepth, source, QStringLiteral("selected descriptor component/sample depth"), meta.bitDepth,
			EvidenceBasis::Derived, meta.componentDepth >= 0 ? QVariant(meta.componentDepth) : QVariant{}, owner);
		add(file, MediaProperty::SampleFormat, source, QStringLiteral("coding variant and component/sample depth"), meta.sampleFormat,
			EvidenceBasis::Derived, meta.componentDepth >= 0 ? QVariant(meta.componentDepth) : QVariant{}, owner);
		add(file, MediaProperty::Channels, source, QStringLiteral("selected audio descriptor channel count"),
			meta.channels > 0 ? QVariant(meta.channels) : QVariant{}, EvidenceBasis::Recorded, {}, owner);
		add(file, MediaProperty::Kind, source, QStringLiteral("selected descriptor class / essence label"),
			meta.valid ? QVariant(meta.isAudio ? QStringLiteral("Audio") : QStringLiteral("Video")) : QVariant{}, EvidenceBasis::Derived, {}, owner);
		add(file, MediaProperty::FileDuration, source, QStringLiteral("selected file duration and clock"),
			meta.duration.known() ? QVariant(QVariantList{meta.duration.units, rate(meta.duration.rate)}) : QVariant{},
			meta.duration.source == MediaDuration::Source::Descriptor ? EvidenceBasis::Recorded : EvidenceBasis::Derived,
			QVariantList{meta.duration.units, meta.duration.rate.numerator, meta.duration.rate.denominator,
				meta.duration.displayRate.numerator, meta.duration.displayRate.denominator, static_cast<int>(meta.duration.source), meta.timecodeBase, meta.dropFrame}, owner);
		if (meta.clipNameFromMaterial)
			add(file, MediaProperty::ClipName, source, QStringLiteral("selected material/master name"), meta.clipName, EvidenceBasis::Recorded, {}, meta.umid);
		add(file, MediaProperty::Project, source, QStringLiteral("selected object project attribute"), meta.projectName, EvidenceBasis::Recorded, {}, owner);
		add(file, MediaProperty::SourcePath, source, QStringLiteral("selected import/source path"), meta.sourceFilePath, EvidenceBasis::Recorded, {}, owner);
		add(file, MediaProperty::SourceContainer, source, QStringLiteral("selected import container attribute"), meta.sourceContainer, EvidenceBasis::Recorded, {}, owner);
	}

	inline int technicalRank(MetadataSource source)
	{
		return source == MetadataSource::Mxf || source == MetadataSource::Omf ? 3 : source == MetadataSource::Mdb ? 2 : 0;
	}
	inline int nameRank(MetadataSource source)
	{
		return source == MetadataSource::Mxf || source == MetadataSource::Omf ? 3 : source == MetadataSource::Mdb ? 2 : source == MetadataSource::Avb ? 1 : 0;
	}
	inline ResolvedField select(MediaFile &file, MediaProperty field, MediaEvidence::Rank rank, const QString &rule)
	{
		const auto result = file.evidence.resolve(field, rank, rule);
		file.evidence.select(field, result);
		return result;
	}
	inline void resolveTechnical(MediaFile &file)
	{
		const auto text = [&](MediaProperty field, QString &target)
		{
			const auto result = select(file, field, technicalRank, QStringLiteral("ValidatedHeaderThenMatchingMdb"));
			if (!file.evidence.observations(field).isEmpty())
				target = result.value.toString();
		};
		text(MediaProperty::Codec, file.codec);
		text(MediaProperty::Resolution, file.resolution);
		text(MediaProperty::BitDepth, file.bitDepth);
		text(MediaProperty::SampleFormat, file.sampleFormat);
		const auto selectedRaw = [&](MediaProperty field, const ResolvedField &selection)
		{
			return selection.selectedObservation >= 0 ? file.evidence.observations(field)[selection.selectedObservation].rawValue.toList() : QVariantList{};
		};
		const auto frame = select(file, MediaProperty::FrameRate, technicalRank, QStringLiteral("ValidatedHeaderThenMatchingMdb"));
		if (!file.evidence.observations(MediaProperty::FrameRate).isEmpty())
		{
			const auto raw = selectedRaw(MediaProperty::FrameRate, frame);
			file.frameRateRatio = raw.size() >= 2 ? MediaRate{raw[0].toInt(), raw[1].toInt()} : MediaRate{};
			file.frameRate = raw.size() >= 3 ? raw[2].toString() : QString{};
		}
		const auto sample = select(file, MediaProperty::SampleRate, technicalRank, QStringLiteral("ValidatedHeaderThenMatchingMdb"));
		if (!file.evidence.observations(MediaProperty::SampleRate).isEmpty())
		{
			const auto raw = selectedRaw(MediaProperty::SampleRate, sample);
			file.sampleRateRatio = raw.size() >= 2 ? MediaRate{raw[0].toInt(), raw[1].toInt()} : MediaRate{};
			file.sampleRateEncoding = raw.size() >= 3 ? raw[2].toByteArray() : QByteArray{};
			file.sampleRate = raw.size() >= 4 ? raw[3].toInt() : 0;
		}
		const auto duration = select(file, MediaProperty::FileDuration, technicalRank, QStringLiteral("RelevantFileDuration"));
		if (!file.evidence.observations(MediaProperty::FileDuration).isEmpty())
		{
			const auto raw = selectedRaw(MediaProperty::FileDuration, duration);
			file.duration = raw.size() >= 6 ? MediaDuration{raw[0].toLongLong(), {raw[1].toInt(), raw[2].toInt()},
				{raw[3].toInt(), raw[4].toInt()}, static_cast<MediaDuration::Source>(raw[5].toInt())} : MediaDuration{};
			file.timecodeBase = raw.size() >= 8 ? raw[6].toInt() : 0;
			file.dropFrame = raw.size() >= 8 && raw[7].toBool();
		}
		const auto kind = select(file, MediaProperty::Kind, technicalRank, QStringLiteral("DescriptorKind"));
		if (!file.evidence.observations(MediaProperty::Kind).isEmpty())
			file.kind = kind.value.toString() == QStringLiteral("Audio") ? MediaFile::Kind::Audio :
				kind.value.toString() == QStringLiteral("Video") ? MediaFile::Kind::Video : MediaFile::Kind::Unknown;
		const auto channels = select(file, MediaProperty::Channels, technicalRank, QStringLiteral("RecordedFileChannels"));
		if (!file.evidence.observations(MediaProperty::Channels).isEmpty())
			file.channels = channels.value.toInt();
		const auto name = select(file, MediaProperty::ClipName, nameRank, QStringLiteral("MaterialNameThenMdbThenAvb"));
		if (!file.evidence.observations(MediaProperty::ClipName).isEmpty())
		{
			file.clipName = name.value.toString();
			file.clipNameSource = MediaFile::ClipNameSource::None;
			if (name.selectedObservation >= 0)
			{
				const auto source = file.evidence.observations(MediaProperty::ClipName)[name.selectedObservation].snapshot->source;
				file.clipNameSource = source == MetadataSource::Mdb ? MediaFile::ClipNameSource::Mdb :
					source == MetadataSource::Avb ? MediaFile::ClipNameSource::Avb : MediaFile::ClipNameSource::MaterialPackage;
			}
		}
	}
}
