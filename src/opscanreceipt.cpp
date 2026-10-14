#include "opscanreceipt.h"
#include "mediafile.h"
#include "mobid.h"
#include "mediaengine/scanmodel.h"
#include <algorithm>

namespace
{
	MediaHeaderReadOutcome headerReadOutcome(MediaEngine::ParsedSource::Outcome outcome)
	{
		using Outcome = MediaEngine::ParsedSource::Outcome;
		switch (outcome)
		{
		case Outcome::NotRead: return MediaHeaderReadOutcome::NotRead;
		case Outcome::Complete: return MediaHeaderReadOutcome::Complete;
		case Outcome::Incomplete: return MediaHeaderReadOutcome::Incomplete;
		case Outcome::Malformed: return MediaHeaderReadOutcome::Malformed;
		case Outcome::Unsupported: return MediaHeaderReadOutcome::Unsupported;
		case Outcome::IoError: return MediaHeaderReadOutcome::IoError;
		case Outcome::Cancelled: return MediaHeaderReadOutcome::Cancelled;
		}
		return MediaHeaderReadOutcome::NoReceipt;
	}

	QString originalLocation(const MediaEvidence &evidence, const MediaScanStamp &stamp)
	{
		// Capture from temporary evidence before publishing the row. The first
		// filesystem location identifies the original header receipt owner.
		for (const auto &observation : evidence.observations(MediaProperty::Location))
			if (observation.snapshot && observation.snapshot->source == MetadataSource::Filesystem &&
				observation.readState == PropertyReadState::Present)
				return observation.value.toString(); // Later locations may belong to another scan receipt.
		return stamp.path;
	}

	QString databaseIdentityForSkippedHeader(const MediaEvidence &evidence, const MediaScanStamp &stamp, const ScanRowLookup &lookup)
	{
		if (stamp.fileMobId.isEmpty() || MobId::isAllZero(stamp.fileMobId))
			return {};
		const QString scannedPath = originalLocation(evidence, stamp);
		if (!lookup.headerWasSkipped(scannedPath))
			return {};
		for (const auto &observation : evidence.observations(MediaProperty::FileMobId))
			if (observation.snapshot && observation.eligible && observation.readState == PropertyReadState::Present &&
				(observation.snapshot->source == MetadataSource::Pmr || observation.snapshot->source == MetadataSource::Mdb) &&
				observation.value.toString() == stamp.fileMobId)
				return stamp.fileMobId;
		return {};
	}

	OpHeaderIdentity headerIdentity(const MediaEvidence &evidence, const MediaScanStamp &stamp, const ScanRowLookup &lookup)
	{
		OpHeaderIdentity identity;
		QStringList fileIds;
		for (const auto property : {MediaProperty::FileMobId, MediaProperty::MasterMobId})
			for (const auto &observation : evidence.observations(property))
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
		if (lookup.sourceChanged(originalLocation(evidence, stamp)))
			identity.unavailableReason = QStringLiteral("The media header changed during scanning. Rescan before proceeding.");
		return identity;
	}
}

ScanRowLookup::ScanRowLookup(const MediaEngine::ScanResult &scan)
{
	for (const auto &source : scan.sources)
		if (source.snapshot &&
			(source.snapshot->source == MetadataSource::Mxf || source.snapshot->source == MetadataSource::Omf))
		{
			auto &flags = m_flags[source.snapshot->path];
			flags.headerSkipped |= source.outcome == MediaEngine::ParsedSource::Outcome::NotRead;
			// An attempted read remains attempted if another receipt records no read.
			if (source.outcome != MediaEngine::ParsedSource::Outcome::NotRead ||
				flags.headerOutcome == MediaHeaderReadOutcome::NoReceipt)
				flags.headerOutcome = headerReadOutcome(source.outcome);
		}
	for (const auto &issue : scan.reconciliationIssues)
		if (issue.kind == ScanIssue::Kind::SourceChanged && issue.source)
			m_flags[issue.source->path].sourceChanged = true;
}

bool ScanRowLookup::headerWasSkipped(const QString &path) const
{
	const auto found = m_flags.constFind(path);
	return found != m_flags.cend() && found->headerSkipped;
}

bool ScanRowLookup::sourceChanged(const QString &path) const
{
	const auto found = m_flags.constFind(path);
	return found != m_flags.cend() && found->sourceChanged;
}

MediaHeaderReadOutcome ScanRowLookup::headerOutcome(const QString &path) const
{
	const auto found = m_flags.constFind(path);
	return found == m_flags.cend() ? MediaHeaderReadOutcome::NoReceipt : found->headerOutcome;
}

OpScanReceipt makeOpScanReceipt(const MediaEvidence &evidence, const MediaScanStamp &stamp,
							   const ScanRowLookup &lookup)
{
	return {headerIdentity(evidence, stamp, lookup), databaseIdentityForSkippedHeader(evidence, stamp, lookup)};
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
