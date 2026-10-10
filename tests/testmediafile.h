#pragma once

// UI fixtures describe selected facts, not another parser or metadata engine.
// Give hand-written rows the same evidence/selection contract as scanned rows.
// Effect names are derived by the real catalogue; display-only labels cannot
// override the recorded clip-name evidence.

#include "mediaengineadapter.h"
#include "mediaengine/projection.h"
#include "mediaengine/scancoordinator.h"
#include <utility>

namespace TestMediaFile
{
	inline QVector<MediaFile> seeded(QVector<MediaFile> files)
	{
		for (auto &file : files)
		{
			if (file.mediaEngineScan)
				continue; // Deliberate engine/evidence fixtures already supply their own facts.
			const auto source = [&](MetadataSource kind)
			{
				return QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
					kind, file.mediaFilePath, {}, SourceReadState::Complete});
			};
			const auto header = source(file.omfEra ? MetadataSource::Omf : MetadataSource::Mxf);
			const auto filesystem = source(MetadataSource::Filesystem);
			const auto add = [&](MediaProperty property, const QVariant &value, const SourceSnapshotRef &snapshot)
			{
				if (!value.isValid() || !file.evidence.observations(property).isEmpty())
					return;
				if (value.metaType().id() == QMetaType::QString && value.toString().isEmpty())
					return;
				MetadataObservation observation;
				observation.snapshot = snapshot;
				observation.property = mediaPropertyName(property);
				observation.objectIdentity = QStringLiteral("UI fixture");
				observation.value = value;
				observation.readState = PropertyReadState::Present;
				file.evidence.observe(property, std::move(observation));
			};
			const auto nameSource = file.clipNameSource == MediaFile::ClipNameSource::Mdb ? source(MetadataSource::Mdb)
								: file.clipNameSource == MediaFile::ClipNameSource::Avb ? source(MetadataSource::Avb)
																						: header;
			add(MediaProperty::ClipName, file.clipName, nameSource);
			add(MediaProperty::OriginalBin, file.originalBin, file.originalBinFromAvb ? source(MetadataSource::Avb) : nameSource);
			add(MediaProperty::FileMobId, file.fileMobId, header);
			const auto masters = file.masterMobIds.isEmpty() ? QStringList{file.masterMobId} : file.masterMobIds;
			add(MediaProperty::MasterMobId, masters, header);
			add(MediaProperty::Project, file.project, header);
			add(MediaProperty::Compression, file.compression, header);
			add(MediaProperty::Resolution, file.resolution, header);
			if (file.kind != MediaFile::Kind::Unknown)
				add(MediaProperty::Kind, int(file.kind), header);
			if (file.type != MediaFile::Type::Unknown)
				add(MediaProperty::Type, int(file.type), header);
			add(MediaProperty::PrecomputeCategory, int(file.precomputeCategory), header);
			MediaRate frameRate = file.frameRateRatio;
			if (!frameRate.valid() && !file.frameRate.isEmpty())
				frameRate = {qRound(file.frameRate.toDouble() * 1000), 1000};
			if (frameRate.valid())
				add(MediaProperty::FrameRate, MediaEngine::rateValue(frameRate), header);
			const MediaRate sampleRate = file.sampleRateRatio.valid() ? file.sampleRateRatio : MediaRate{file.sampleRate, 1};
			if (sampleRate.valid())
				add(MediaProperty::SampleRate, MediaEngine::rateValue(sampleRate), header);
			add(MediaProperty::BitDepth, file.bitDepth, header);
			add(MediaProperty::SampleFormat, file.sampleFormat, header);
			add(MediaProperty::Channels, file.channels, header);
			add(MediaProperty::DropFrame, file.dropFrame, header);
			if (file.duration.known())
				add(MediaProperty::FileDuration, MediaEngine::durationValue(file.duration), header);
			QVariantList tracks;
			for (const auto &track : file.clipDurations)
				tracks.append(QVariantMap{{QStringLiteral("TrackId"), track.trackId},
					{QStringLiteral("Duration"), MediaEngine::durationValue(track.duration)},
					{QStringLiteral("DropFrame"), track.dropFrame}});
			if (!tracks.isEmpty())
				add(MediaProperty::ClipDuration, tracks, header);
			add(MediaProperty::SourcePath, file.sourceFilePath, header);
			add(MediaProperty::SourceFilename, file.sourceFileName, header);
			add(MediaProperty::SourceContainer, file.sourceContainer, header);
			add(MediaProperty::Imported, file.isImported, header);
			add(MediaProperty::DatabaseStatus, int(file.dbStatus), filesystem);
			MediaEngine::selectMetadata(file.evidence);
			applyResolvedMetadata(file);
		}
		return files;
	}
}
