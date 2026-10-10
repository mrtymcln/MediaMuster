#include "opscanreceipt.h"
#include "mediafile.h"
#include "mobid.h"
#include "mediaengine/scanmodel.h"
#include <algorithm>

namespace
{
	QString originalLocation(const MediaFile &file)
	{
		// Confirmed moves/copies keep the original scan and this row's location
		// history. The header receipt still refers to that original location.
		for (const auto &observation : file.evidence.observations(MediaProperty::Location))
			if (observation.snapshot && observation.snapshot->source == MetadataSource::Filesystem &&
				observation.readState == PropertyReadState::Present)
				return observation.value.toString(); // Later locations may belong to another scan receipt.
		return file.scanStamp.path;
	}

	QString databaseIdentityForSkippedHeader(const MediaFile &file)
	{
		if (!file.mediaEngineScan || file.scanStamp.mobId.isEmpty() || MobId::isAllZero(file.scanStamp.mobId))
			return {};
		const QString scannedPath = originalLocation(file);
		const bool skipped = std::any_of(file.mediaEngineScan->sources.cbegin(), file.mediaEngineScan->sources.cend(),
											 [&](const MediaEngine::StoredSource &source)
										 {
											 return source.snapshot && scannedPath == source.snapshot->path &&
													(source.snapshot->source == MetadataSource::Mxf || source.snapshot->source == MetadataSource::Omf) &&
													source.outcome == MediaEngine::ParsedSource::Outcome::NotRead;
										 });
		if (!skipped)
			return {};
		for (const auto &observation : file.evidence.observations(MediaProperty::FileMobId))
			if (observation.snapshot && observation.eligible && observation.readState == PropertyReadState::Present &&
				(observation.snapshot->source == MetadataSource::Pmr || observation.snapshot->source == MetadataSource::Mdb) &&
				observation.value.toString() == file.scanStamp.mobId)
				return file.scanStamp.mobId;
		return {};
	}

	OpHeaderIdentity headerIdentity(const MediaFile &file)
	{
		OpHeaderIdentity identity;
		QStringList fileIds;
		for (const auto property : {MediaProperty::FileMobId, MediaProperty::MasterMobId})
			for (const auto &observation : file.evidence.observations(property))
			{
				if (!observation.snapshot ||
					(observation.snapshot->source != MetadataSource::Mxf && observation.snapshot->source != MetadataSource::Omf))
					continue;
				if (observation.freshness == SourceFreshness::Changed)
					identity.unavailableReason = QStringLiteral("The media header changed during scanning. Rescan before proceeding.");
				if (!observation.eligible || observation.readState != PropertyReadState::Present)
					continue;
				const auto ids = observation.value.metaType().id() == QMetaType::QStringList
									 ? observation.value.toStringList()
									 : QStringList{observation.value.toString()};
				for (const auto &id : ids)
					if (!id.isEmpty())
						(property == MediaProperty::FileMobId ? fileIds : identity.masterMobIds).append(id);
			}
		fileIds.removeDuplicates();
		identity.masterMobIds.removeDuplicates();
		std::sort(identity.masterMobIds.begin(), identity.masterMobIds.end());
		if (fileIds.size() == 1)
			identity.mobId = fileIds.front();
		else if (fileIds.size() > 1)
			identity.unavailableReason = QStringLiteral("The media header has conflicting file identities. Rescan before proceeding.");
		if (file.mediaEngineScan)
		{
			const QString scannedPath = originalLocation(file);
			for (const auto &issue : file.mediaEngineScan->reconciliationIssues)
				if (issue.kind == ScanIssue::Kind::SourceChanged && issue.source && issue.source->path == scannedPath)
					identity.unavailableReason = QStringLiteral("The media header changed during scanning. Rescan before proceeding.");
		}
		return identity;
	}
}

OpItem opItemFromMediaFile(const MediaFile &mf)
{
	OpItem it;
	it.src = mf.mediaFilePath;
	it.name = mf.fileName;
	it.mediaFolderName = mf.mediaFolderName;
	it.omfEra = mf.omfEra; // OMF-era: travels with the item, and through the journal
	it.bytes = mf.sizeBytes;
	it.modifiedMs = mf.modified.isValid() ? mf.modified.toMSecsSinceEpoch() : -1;
	// Retain all scan claims for replay and reporting. The separate header
	// receipt below identifies which claims the opened file can corroborate.
	it.mobId = mf.fileMobId;
	it.masterMobId = mf.masterMobId;
	it.masterMobIds = mf.masterMobIds;
	if (!mf.scanStamp.path.isEmpty())
	{
		it.scanPath = mf.scanStamp.path;
		it.scanVolumeIdentifier = mf.scanStamp.volumeIdentifier;
		it.modifiedMs = mf.scanStamp.modified.isValid() ? mf.scanStamp.modified.toMSecsSinceEpoch() : -1;
		it.mobId = mf.scanStamp.mobId;
		it.masterMobIds = mf.scanStamp.masterMobIds;
		it.masterMobId = it.masterMobIds.size() == 1 ? it.masterMobIds.front() : QString();
		it.headerIdentity = headerIdentity(mf);
		it.databaseMobIdToVerify = databaseIdentityForSkippedHeader(mf);
	}
	it.clipName = mf.clipName;
	return it;
}
