#include "bellaoperationhandoff.h"

#include "bella/bellascanner.h"
#include "mediafile.h"
#include "omfuid.h"

#include <QDir>
#include <QFileInfo>
#include <utility>

void applyBellaOperationHandoff(MediaFile &row, const Bella::ScanEntry &entry)
{
	const QFileInfo file(entry.path);
	const auto &metadata = entry.match.metadata;
	row.mediaFilePath = entry.path;
	row.fileName = file.fileName();
	row.mediaFolderName = file.dir().dirName();
	row.omfEra = entry.omfEra;
	row.sizeBytes = entry.sizeBytes.value_or(-1);
	row.modified = entry.modified;
	row.clipName = metadata.clipName;
	row.fileMobId = metadata.fileMobId ? OmfUid::toIdText(metadata.fileMobId->bytes()) : QString{};
	row.masterMobId = metadata.masterMobId ? OmfUid::toIdText(metadata.masterMobId->bytes()) : QString{};
	row.scanStamp = {entry.path, entry.volumeIdentifier, entry.modified, row.fileMobId, row.masterMobId};

	OpScanReceipt receipt;
	if (const auto *verify = std::get_if<Bella::VerifyFileId>(&entry.operationSafety))
	{
		const auto id = OmfUid::toIdText(verify->id.bytes());
		if (verify->origin == Bella::FileIdOrigin::Database)
			receipt.databaseFileMobIdToVerify = id;
		else
			receipt.headerIdentity.fileMobId = id;
	}
	else if (const auto *reason = std::get_if<Bella::RescanReason>(&entry.operationSafety))
	{
		switch (*reason)
		{
		case Bella::RescanReason::FileChanged:
			receipt.headerIdentity.unavailableReason = QStringLiteral("The media file changed during scanning. Rescan before proceeding.");
			break;
		case Bella::RescanReason::FilePresenceUnconfirmed:
			receipt.headerIdentity.unavailableReason = QStringLiteral("The scan could not confirm that the media file was still present. Rescan before proceeding.");
			break;
		}
	}
	// A physical-only instruction is an explicit empty receipt.
	row.operationScanReceipt = std::move(receipt);
}
