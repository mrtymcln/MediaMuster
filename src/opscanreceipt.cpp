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

	QString databaseIdentityForSkippedHeader(const MediaFile &file, const OpScanReceiptLookup &lookup)
	{
		if (file.scanStamp.fileMobId.isEmpty() || MobId::isAllZero(file.scanStamp.fileMobId))
			return {};
		const QString scannedPath = originalLocation(file);
		if (!lookup.headerWasSkipped(scannedPath))
			return {};
		for (const auto &observation : file.evidence.observations(MediaProperty::FileMobId))
			if (observation.snapshot && observation.eligible && observation.readState == PropertyReadState::Present &&
				(observation.snapshot->source == MetadataSource::Pmr || observation.snapshot->source == MetadataSource::Mdb) &&
				observation.value.toString() == file.scanStamp.fileMobId)
				return file.scanStamp.fileMobId;
		return {};
	}

	OpHeaderIdentity headerIdentity(const MediaFile &file, const OpScanReceiptLookup &lookup)
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
			identity.fileMobId = fileIds.front();
		else if (fileIds.size() > 1)
			identity.unavailableReason = QStringLiteral("The media header has conflicting file identities. Rescan before proceeding.");
		if (lookup.sourceChanged(originalLocation(file)))
			identity.unavailableReason = QStringLiteral("The media header changed during scanning. Rescan before proceeding.");
		return identity;
	}
}

OpScanReceiptLookup::OpScanReceiptLookup(const MediaEngine::ScanResult &scan)
{
	for (const auto &source : scan.sources)
		if (source.snapshot &&
			(source.snapshot->source == MetadataSource::Mxf || source.snapshot->source == MetadataSource::Omf) &&
			source.outcome == MediaEngine::ParsedSource::Outcome::NotRead)
			m_flags[source.snapshot->path].headerSkipped = true;
	for (const auto &issue : scan.reconciliationIssues)
		if (issue.kind == ScanIssue::Kind::SourceChanged && issue.source)
			m_flags[issue.source->path].sourceChanged = true;
}

bool OpScanReceiptLookup::headerWasSkipped(const QString &path) const
{
	const auto found = m_flags.constFind(path);
	return found != m_flags.cend() && found->headerSkipped;
}

bool OpScanReceiptLookup::sourceChanged(const QString &path) const
{
	const auto found = m_flags.constFind(path);
	return found != m_flags.cend() && found->sourceChanged;
}

void captureOpScanReceipt(MediaFile &file, const OpScanReceiptLookup &lookup)
{
	file.operationScanReceipt = OpScanReceipt{headerIdentity(file, lookup), databaseIdentityForSkippedHeader(file, lookup)};
}

void captureOpScanReceipt(MediaFile &file)
{
	const auto lookup = file.mediaEngineScan ? OpScanReceiptLookup{*file.mediaEngineScan} : OpScanReceiptLookup{};
	captureOpScanReceipt(file, lookup);
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
	it.fileMobId = mf.fileMobId;
	it.masterMobId = mf.masterMobId;
	it.masterMobIds = mf.masterMobIds;
	if (!mf.scanStamp.path.isEmpty())
	{
		it.scanPath = mf.scanStamp.path;
		it.scanVolumeIdentifier = mf.scanStamp.volumeIdentifier;
		it.modifiedMs = mf.scanStamp.modified.isValid() ? mf.scanStamp.modified.toMSecsSinceEpoch() : -1;
		it.fileMobId = mf.scanStamp.fileMobId;
		it.masterMobIds = mf.scanStamp.masterMobIds;
		it.masterMobId = it.masterMobIds.size() == 1 ? it.masterMobIds.front() : QString();
		if (mf.operationScanReceipt)
		{
			it.headerIdentity = mf.operationScanReceipt->headerIdentity;
			it.databaseFileMobIdToVerify = mf.operationScanReceipt->databaseFileMobIdToVerify;
		}
	}
	if (!mf.operationScanReceipt)
		it.headerIdentity = OpHeaderIdentity{{}, {}, QStringLiteral("Scan safety record unavailable. Rescan before proceeding.")};
	it.clipName = mf.clipName;
	return it;
}
