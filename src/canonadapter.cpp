#include "canonadapter.h"
#include "canon/projection.h"
#include "mobid.h"
#include <QFileInfo>
#include <QStorageInfo>
#include <QDir>
#include <QRegularExpression>

MediaFile canonMediaFile(const Canon::MediaFile &file, const QSharedPointer<const Canon::ScanResult> &scan)
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
	const QStorageInfo volume(file.path);
	row.volumePath = volume.rootPath();
	row.volumeName = volume.displayName();
	if (row.volumeName.isEmpty())
		row.volumeName = QDir(row.volumePath).dirName();
	row.sizeBytes = file.sizeBytes;
	row.created = file.created;
	row.modified = file.modified;
	row.omfEra = file.omfScan;
	row.isQuarantined = file.quarantined;
	const auto value = [&](MediaProperty property)
	{ return file.evidence.selected(property).value; };
	row.fileMobId = value(MediaProperty::FileMobId).toString();
	row.masterMobIds = value(MediaProperty::MasterMobId).toStringList();
	if (row.masterMobIds.size() == 1)
		row.masterMobId = row.masterMobIds.front();
	row.clipName = value(MediaProperty::ClipName).toString();
	const auto clip = file.evidence.selected(MediaProperty::ClipName);
	const auto &names = file.evidence.observations(MediaProperty::ClipName);
	if (clip.selectedObservation >= 0 && clip.selectedObservation < names.size() && names[clip.selectedObservation].snapshot)
	{
		const auto source = names[clip.selectedObservation].snapshot->source;
		row.clipNameSource = source == MetadataSource::Mdb ? MediaFile::ClipNameSource::Mdb : source == MetadataSource::Avb ? MediaFile::ClipNameSource::Avb
																															: MediaFile::ClipNameSource::MaterialPackage;
	}
	row.project = value(MediaProperty::Project).toString();
	row.originalBin = value(MediaProperty::OriginalBin).toString();
	row.codec = value(MediaProperty::Codec).toString();
	row.resolution = value(MediaProperty::Resolution).toString();
	row.frameRateRatio = Canon::mediaRate(value(MediaProperty::FrameRate));
	if (row.frameRateRatio.valid())
		row.frameRate = QString::number(row.frameRateRatio.value(), 'f', 3).remove(QRegularExpression(QStringLiteral("0+$"))).remove(QRegularExpression(QStringLiteral("\\.$")));
	row.sampleRateRatio = Canon::mediaRate(value(MediaProperty::SampleRate));
	if (row.sampleRateRatio.valid() && row.sampleRateRatio.value() <= std::numeric_limits<int>::max())
		row.sampleRate = qRound(row.sampleRateRatio.value());
	row.bitDepth = value(MediaProperty::BitDepth).toString();
	if (value(MediaProperty::BitDepth).metaType().id() != QMetaType::QString && value(MediaProperty::BitDepth).toInt() > 0)
		row.bitDepth = QStringLiteral("%1-bit").arg(value(MediaProperty::BitDepth).toInt());
	row.sampleFormat = value(MediaProperty::SampleFormat).toString();
	row.channels = value(MediaProperty::Channels).toInt();
	row.duration = Canon::mediaDuration(value(MediaProperty::FileDuration));
	row.dropFrame = value(MediaProperty::DropFrame).toBool();
	row.timecodeBase = qRound(row.duration.displayRate.value());
	for (const auto &item : value(MediaProperty::ClipDuration).toList())
	{
		const auto track = item.toMap();
		row.clipDurations.append({track.value(QStringLiteral("TrackId")).toUInt(),
								  Canon::mediaDuration(track.value(QStringLiteral("Duration"))), track.value(QStringLiteral("DropFrame")).toBool()});
	}
	row.sourceFilePath = value(MediaProperty::SourcePath).toString();
	row.sourceFileName = value(MediaProperty::SourceFilename).toString();
	row.sourceContainer = value(MediaProperty::SourceContainer).toString();
	row.isImported = value(MediaProperty::Imported).toBool();
	if (value(MediaProperty::Kind).isValid())
		row.kind = MediaFile::Kind(value(MediaProperty::Kind).toInt());
	if (value(MediaProperty::Type).isValid())
		row.type = MediaFile::Type(value(MediaProperty::Type).toInt());
	if (value(MediaProperty::PrecomputeCategory).isValid())
		row.precomputeCategory = MediaFile::PrecomputeCategory(value(MediaProperty::PrecomputeCategory).toInt());
	row.dbStatus = value(MediaProperty::DatabaseStatus).isValid() ? MediaFile::DbStatus(value(MediaProperty::DatabaseStatus).toInt()) : MediaFile::DbStatus::DbUnreadable;
	row.isInvalidUmid = MobId::isAllZero(row.fileMobId);
	for (const auto &master : row.masterMobIds)
		row.isInvalidUmid |= MobId::isAllZero(master);
	const QString allowed = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._- ,()[]+='~@#%&");
	for (const QChar character : row.fileName)
		row.isNonPortable |= !allowed.contains(character);
	row.effect = value(MediaProperty::Effect).toString();
	row.effectCategory = value(MediaProperty::EffectCategory).toString();
	row.effectSequence = value(MediaProperty::EffectSequence).toString();
	return row;
}
