#include "file_check.h"

#include "scanner/mxf_parser.h"
#include "scanner/omf_parser.h"
#include "media/conventions.h"
#include "media/mob_id_text.h"
#include "storage/volume_identity.h"

#include <QDir>
#include <QFileInfo>

// MARK: - Source identity helpers

namespace
{
	bool mediaIdentityMatches(const OperationItem &item, OperationFile &source, QString &error)
	{
		if (item.headerIdentity && !item.headerIdentity->unavailableReason.isEmpty())
		{
			error = item.headerIdentity->unavailableReason;
			return false;
		}
		const bool mxf = Conventions::hasMxfExtension(item.src);
		if (!mxf && !Conventions::hasOmfEraExtension(item.src))
			return true;
		const QString fileId = !item.databaseFileMobIdToVerify.isEmpty() ? item.databaseFileMobIdToVerify
							   : item.headerIdentity					 ? item.headerIdentity->fileMobId
																		 : item.fileMobId;
		QStringList expectedMasters = item.headerIdentity						 ? item.headerIdentity->masterMobIds
									  : item.databaseFileMobIdToVerify.isEmpty() ? QStringList{item.masterMobId}
																				 : QStringList{};
		expectedMasters.removeIf([](const QString &id)
								 { return id.isEmpty() || MobId::isAllZero(id); });
		const bool fileKnown = !fileId.isEmpty() && !MobId::isAllZero(fileId);
		if (!fileKnown && expectedMasters.isEmpty())
			return true;
		const bool matches = mxf ? Bella::mxfIdentityMatches(source.io(), fileKnown ? fileId : QString{}, expectedMasters)
								 : Bella::omfIdentityMatches(source.io(), fileKnown ? fileId : QString{}, expectedMasters);
		if (!matches)
		{
			error = item.databaseFileMobIdToVerify.isEmpty()
						? QStringLiteral("The file's Avid identity is missing or differs from the scan. Rescan before proceeding.")
						: QStringLiteral("The opened media header cannot confirm the PMR/MDB FileMobId selected during scanning. The identity is missing, unreadable or different; rescan before proceeding.");
			return false;
		}
		return true;
	}
	bool scanLocationMatches(const OperationItem &item, QString &error)
	{
		if (item.scanPath.isEmpty())
			return true; // Non-scanned inputs and operation-only maintenance have no scan receipt.
		if (QDir::cleanPath(item.scanPath) != QDir::cleanPath(item.src))
			error = QStringLiteral("The selected path differs from its scan receipt. Rescan before proceeding.");
		else if (item.scanVolumeIdentifier.isEmpty())
			error = QStringLiteral("The scan did not establish the source volume identity. Rescan before proceeding.");
		else if (item.modifiedMs < 0)
			error = QStringLiteral("The scan did not establish the source modification time. Rescan before proceeding.");
		else if (VolumeIdentity::capture(item.src).identifier() != item.scanVolumeIdentifier)
			error = QStringLiteral("The source volume identity is unavailable or differs from the scan. Rescan before proceeding.");
		return error.isEmpty();
	}
}

// MARK: - File identity checks

bool FileCheck::unchanged(const OperationStamp &expected, const OperationStamp &observed)
{
	return expected.unchanged(observed);
}

bool FileCheck::unchanged(const OperationStamp &expected, const QString &path)
{
	return unchanged(expected, OperationFile::inspect(path));
}

bool FileCheck::unchanged(const OperationStamp &expected, const OperationFile &opened, const QString &path)
{
	return opened.stillAt(path, expected);
}

bool FileCheck::sameObject(const OperationStamp &expected, const OperationStamp &observed)
{
	return expected.sameObject(observed);
}

bool FileCheck::hasSize(const OperationStamp &observed, qint64 expectedBytes)
{
	return observed.size == expectedBytes;
}

// MARK: - Copy and source validation

bool FileCheck::completedCopyMatches(const QString &path, const OperationStamp &expected, QString &error)
{
	if (!unchanged(expected, path))
	{
		error = "The completed destination is missing or changed: " + path;
		return false;
	}
	return true;
}

bool FileCheck::sourceMatches(const OperationItem &item, OperationFile &source, const OperationStamp &expected,
							  const OperationStamp &current, QString &error)
{
	if (!expected.valid() || !unchanged(expected, current) ||
		(item.bytes >= 0 && item.bytes != current.size) ||
		(item.modifiedMs >= 0 && QFileInfo(item.src).lastModified().toMSecsSinceEpoch() != item.modifiedMs) ||
		!scanLocationMatches(item, error) || !mediaIdentityMatches(item, source, error) ||
		!unchanged(current, source, item.src))
	{
		if (error.isEmpty())
			error = "The file changed since it was selected or journalled; rescan before proceeding.";
		return false;
	}
	return true;
}
