#include "canonadapter.h"
#include "canon/projection.h"
#include "mobid.h"
#include <QFileInfo>
#include <QStorageInfo>
#include <QDir>
#include <QRegularExpression>
#include <algorithm>
#include <tuple>
#include <utility>

bool applyResolvedMetadata(MediaFile &row)
{
	bool changed = false;
	const auto assign = [&](auto &destination, const auto &selected)
	{
		if (destination != selected)
		{
			destination = selected;
			changed = true;
		}
	};
	const auto assignRate = [&](MediaRate &destination, const MediaRate &selected)
	{
		assign(destination.numerator, selected.numerator);
		assign(destination.denominator, selected.denominator);
	};
	const auto durationValues = [](const MediaDuration &duration)
	{
		return std::tie(duration.units, duration.rate.numerator, duration.rate.denominator,
						duration.displayRate.numerator, duration.displayRate.denominator, duration.source);
	};
	const auto value = [&](MediaProperty property)
	{ return row.evidence.selected(property).value; };
	assign(row.fileMobId, value(MediaProperty::FileMobId).toString());
	assign(row.masterMobIds, value(MediaProperty::MasterMobId).toStringList());
	assign(row.masterMobId, row.masterMobIds.size() == 1 ? row.masterMobIds.front() : QString{});
	assign(row.clipName, value(MediaProperty::ClipName).toString());
	auto nameSource = MediaFile::ClipNameSource::None;
	const auto clip = row.evidence.selected(MediaProperty::ClipName);
	const auto &names = row.evidence.observations(MediaProperty::ClipName);
	if (clip.selectedObservation >= 0 && clip.selectedObservation < names.size() && names[clip.selectedObservation].snapshot)
	{
		const auto source = names[clip.selectedObservation].snapshot->source;
		nameSource = source == MetadataSource::Mdb ? MediaFile::ClipNameSource::Mdb : source == MetadataSource::Avb ? MediaFile::ClipNameSource::Avb
																													: MediaFile::ClipNameSource::MaterialPackage;
	}
	assign(row.clipNameSource, nameSource);
	assign(row.project, value(MediaProperty::Project).toString());
	assign(row.originalBin, value(MediaProperty::OriginalBin).toString());
	const auto bin = row.evidence.selected(MediaProperty::OriginalBin);
	const auto &bins = row.evidence.observations(MediaProperty::OriginalBin);
	assign(row.originalBinFromAvb, bin.selectedObservation >= 0 && bin.selectedObservation < bins.size() &&
									   bins[bin.selectedObservation].snapshot && bins[bin.selectedObservation].snapshot->source == MetadataSource::Avb);
	assign(row.compression, value(MediaProperty::Compression).toString());
	assign(row.resolution, value(MediaProperty::Resolution).toString());
	assignRate(row.frameRateRatio, Canon::mediaRate(value(MediaProperty::FrameRate)));
	assign(row.frameRate, row.frameRateRatio.valid()
							  ? QString::number(row.frameRateRatio.value(), 'f', 3).remove(QRegularExpression(QStringLiteral("0+$"))).remove(QRegularExpression(QStringLiteral("\\.$")))
							  : QString{});
	assignRate(row.sampleRateRatio, Canon::mediaRate(value(MediaProperty::SampleRate)));
	assign(row.sampleRate, row.sampleRateRatio.valid() && row.sampleRateRatio.value() <= std::numeric_limits<int>::max()
							   ? qRound(row.sampleRateRatio.value())
							   : 0);
	auto bitDepth = value(MediaProperty::BitDepth).toString();
	if (value(MediaProperty::BitDepth).metaType().id() != QMetaType::QString && value(MediaProperty::BitDepth).toInt() > 0)
		bitDepth = QStringLiteral("%1-bit").arg(value(MediaProperty::BitDepth).toInt());
	assign(row.bitDepth, bitDepth);
	assign(row.sampleFormat, value(MediaProperty::SampleFormat).toString());
	assign(row.channels, value(MediaProperty::Channels).toInt());
	const auto duration = Canon::mediaDuration(value(MediaProperty::FileDuration));
	if (durationValues(row.duration) != durationValues(duration))
	{
		row.duration = duration;
		changed = true;
	}
	assign(row.dropFrame, value(MediaProperty::DropFrame).toBool());
	assign(row.timecodeBase, qRound(row.duration.displayRate.value()));
	QVector<ClipTrackDuration> clipDurations;
	for (const auto &item : value(MediaProperty::ClipDuration).toList())
	{
		const auto track = item.toMap();
		clipDurations.append({track.value(QStringLiteral("TrackId")).toUInt(),
							  Canon::mediaDuration(track.value(QStringLiteral("Duration"))), track.value(QStringLiteral("DropFrame")).toBool()});
	}
	if (row.clipDurations.size() != clipDurations.size() ||
		!std::equal(row.clipDurations.cbegin(), row.clipDurations.cend(), clipDurations.cbegin(), [&](const auto &first, const auto &second)
					{ return first.trackId == second.trackId && first.dropFrame == second.dropFrame && durationValues(first.duration) == durationValues(second.duration); }))
	{
		row.clipDurations = std::move(clipDurations);
		changed = true;
	}
	assign(row.sourceFilePath, value(MediaProperty::SourcePath).toString());
	assign(row.sourceFileName, value(MediaProperty::SourceFilename).toString());
	assign(row.sourceContainer, value(MediaProperty::SourceContainer).toString());
	assign(row.isImported, value(MediaProperty::Imported).toBool());
	assign(row.kind, value(MediaProperty::Kind).isValid() ? MediaFile::Kind(value(MediaProperty::Kind).toInt()) : MediaFile::Kind::Unknown);
	assign(row.type, value(MediaProperty::Type).isValid() ? MediaFile::Type(value(MediaProperty::Type).toInt()) : MediaFile::Type::Unknown);
	assign(row.precomputeCategory, value(MediaProperty::PrecomputeCategory).isValid() ? MediaFile::PrecomputeCategory(value(MediaProperty::PrecomputeCategory).toInt()) : MediaFile::PrecomputeCategory::Unknown);
	assign(row.dbStatus, value(MediaProperty::DatabaseStatus).isValid() ? MediaFile::DbStatus(value(MediaProperty::DatabaseStatus).toInt()) : MediaFile::DbStatus::DbUnreadable);
	bool invalidUmid = MobId::isAllZero(row.fileMobId);
	for (const auto &master : row.masterMobIds)
		invalidUmid |= MobId::isAllZero(master);
	assign(row.isInvalidUmid, invalidUmid);
	assign(row.effect, value(MediaProperty::Effect).toString());
	assign(row.effectCategory, value(MediaProperty::EffectCategory).toString());
	assign(row.effectSequence, value(MediaProperty::EffectSequence).toString());
	return changed;
}

MediaFile canonMediaFile(const Canon::MediaFile &file, const QSharedPointer<const Canon::ScanResult> &scan,
						const QString &volumePath, const QString &volumeName)
{
	MediaFile row;
	row.canonScan = scan;
	row.kelpieId = file.kelpieId;
	row.evidence = file.evidence;
	row.scanStamp = file.stamp;
	row.mediaFilePath = file.path;
	const QFileInfo info(file.path);
	row.fileName = info.fileName();
	row.mediaFolderName = info.dir().dirName();
	// Live scans already know the display volume. Reuse it rather than query
	// network storage once per row, including rows retained after Cancel.
	if (!volumePath.isEmpty())
	{
		row.volumePath = volumePath;
		row.volumeName = volumeName;
	}
	else if (!scan || !scan->cancelled)
	{
		const QStorageInfo volume(file.path);
		row.volumePath = volume.rootPath();
		row.volumeName = volume.displayName();
	}
	if (row.volumeName.isEmpty() && !row.volumePath.isEmpty())
		row.volumeName = QDir(row.volumePath).dirName();
	row.sizeBytes = file.sizeBytes;
	row.created = file.created;
	row.modified = file.modified;
	row.omfEra = file.omfScan;
	row.isQuarantined = file.quarantined;
	applyResolvedMetadata(row);
	const QString allowed = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._- ,()[]+='~@#%&");
	for (const QChar character : row.fileName)
		row.isNonPortable |= !allowed.contains(character);
	return row;
}
