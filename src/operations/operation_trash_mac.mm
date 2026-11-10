#include "operation_trash.h"
#include <QFileInfo>
#include <cerrno>
#import <Foundation/Foundation.h>

// MARK: - macOS Trash helpers

namespace
{
NSURL *url(const QString &path)
{
	const auto bytes = path.toUtf8();
	return [NSURL fileURLWithPath:[NSString stringWithUTF8String:bytes.constData()]];
}
QString describe(NSError *error)
{
	if (!error) return QStringLiteral("The system returned no Trash result.");
	QStringList details;
	for (int depth = 0; error && depth < 4; ++depth)
	{
		details.append(QStringLiteral("%1 (%2, code %3)")
			.arg(QString::fromUtf8([[error localizedDescription] UTF8String]),
				QString::fromUtf8([[error domain] UTF8String]))
			.arg(qlonglong([error code])));
		error = [[error userInfo] objectForKey:NSUnderlyingErrorKey];
	}
	return details.join(QStringLiteral("; "));
}
bool cancelled(NSError *error)
{
	for (int depth = 0; error && depth < 4; ++depth)
	{
		if (([[error domain] isEqualToString:NSCocoaErrorDomain] && [error code] == NSUserCancelledError) ||
			([[error domain] isEqualToString:NSPOSIXErrorDomain] && [error code] == ECANCELED))
			return true;
		error = [[error userInfo] objectForKey:NSUnderlyingErrorKey];
	}
	return false;
}
}

// MARK: - macOS Trash operations

OperationTrash::Result OperationTrashPlatform::move(const QString &path, const OperationStamp &expected,
	const std::atomic<bool> &cancel)
{
	@autoreleasepool
	{
		if (cancel.load()) return {OperationTrash::Outcome::Cancelled, {}, {}, "Cancelled before system Trash.", {}};
		if (!OperationFile::safePath(path) || !expected.unchanged(OperationFile::inspect(path)))
			return {OperationTrash::Outcome::Failed, {}, {}, "The source changed before system Trash.", {}};
		NSURL *resultURL = nil;
		NSError *error = nil;
		const BOOL succeeded = [[NSFileManager defaultManager] trashItemAtURL:url(path)
			resultingItemURL:&resultURL error:&error];
		OperationTrash::Result result;
		if (resultURL)
		{
			result.path = QString::fromUtf8([[resultURL path] UTF8String]);
			result.landed = OperationFile::inspect(result.path);
			result.receipt = receipt("macos", result.path, result.landed);
		}
		if (!succeeded || !resultURL)
		{
			result.error = describe(error);
			// Only a refused operation with no returned location and an unchanged
			// original is eligible for the coordinator's fallback consent dialog.
			// Keep ambiguous native results for reconciliation, even on Cancel.
			if (!succeeded && !resultURL && OperationFile::safePath(path) &&
				expected.unchanged(OperationFile::inspect(path)))
				result.outcome = cancel.load() || cancelled(error) ?
					OperationTrash::Outcome::Cancelled : OperationTrash::Outcome::Unavailable;
			return result;
		}
		if (!OperationFile::safePath(result.path) || !expected.unchanged(result.landed) || OperationFile::occupied(path))
		{
			result.error = "The system Trash result needs identity or location reconciliation.";
			return result;
		}
		QString syncError;
		if (FileSync::syncDirectory(QFileInfo(path).absolutePath(), &syncError) != FileSync::SyncResult::Ok ||
			FileSync::syncDirectory(QFileInfo(result.path).absolutePath(), &syncError) != FileSync::SyncResult::Ok)
		{
			result.error = "File is in system Trash, but directory persistence needs confirmation. " + syncError;
			return result;
		}
		result.outcome = OperationTrash::Outcome::Succeeded;
		return result;
	}
}

OperationTrash::Result OperationTrashPlatform::restore(const QJsonObject &saved, const QString &destination,
	const OperationStamp &expected, const std::atomic<bool> &cancel)
{
	if (saved["provider"].toString() != "macos")
		return {OperationTrash::Outcome::Failed, {}, {}, "This is not a macOS Trash receipt.", {}};
	if (cancel.load()) return {OperationTrash::Outcome::Cancelled, {}, {}, "Cancelled before Trash restoration.", {}};
	const QString resolved = saved["resolvedPath"].toString();
	const QString path = resolved.isEmpty() ? saved["path"].toString() : resolved;
	QString error;
	auto item = OperationFile::open(path, false, error);
	if (!item || !item->stillAt(path, expected))
		return {OperationTrash::Outcome::Failed, path, {}, "The recorded Trash item changed or is missing. " + error, {}};
	if (item->relocate(path, destination, error) != OperationFile::Relocation::Moved)
		return {OperationTrash::Outcome::Failed, path, {}, error, {}};
	OperationTrash::Result result;
	result.path = destination;
	result.landed = item->stamp();
	if (FileSync::syncDirectory(QFileInfo(path).absolutePath(), &error) != FileSync::SyncResult::Ok ||
		FileSync::syncDirectory(QFileInfo(destination).absolutePath(), &error) != FileSync::SyncResult::Ok)
	{
		result.error = "Restored file needs directory-persistence confirmation. " + error;
		return result;
	}
	result.outcome = OperationTrash::Outcome::Succeeded;
	return result;
}
