#include "operation_request.h"
#include "media/media_file.h"

// MARK: - Media row conversion

OperationItem operationItemFromMediaFile(const MediaFile &mf)
{
	OperationItem it;
	it.src = mf.mediaFilePath;
	it.name = mf.fileName;
	it.mediaFolderName = mf.mediaFolderName;
	it.omfEra = mf.omfEra; // OMF-era: travels with the item, and through the journal
	it.bytes = mf.sizeBytes;
	it.modifiedMs = mf.modified.isValid() ? mf.modified.toMSecsSinceEpoch() : -1;
	// Retain the selected scan claims for replay and reporting. The separate
	// header receipt preserves every identity the opened file must corroborate.
	it.fileMobId = mf.fileMobId;
	it.masterMobId = mf.masterMobId;
	if (!mf.scanStamp.path.isEmpty())
	{
		it.scanPath = mf.scanStamp.path;
		it.scanVolumeIdentifier = mf.scanStamp.volumeIdentifier;
		it.modifiedMs = mf.scanStamp.modified.isValid() ? mf.scanStamp.modified.toMSecsSinceEpoch() : -1;
		it.fileMobId = mf.scanStamp.fileMobId;
		it.masterMobId = mf.scanStamp.masterMobId;
		if (mf.operationScanReceipt)
		{
			it.headerIdentity = mf.operationScanReceipt->headerIdentity;
			it.databaseFileMobIdToVerify = mf.operationScanReceipt->databaseFileMobIdToVerify;
		}
	}
	if (!mf.operationScanReceipt)
		it.headerIdentity = OperationHeaderIdentity{{}, {}, QStringLiteral("Scan safety record unavailable. Rescan before proceeding.")};
	it.clipName = mf.clipName;
	return it;
}
