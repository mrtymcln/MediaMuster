#include "optrash.h"
#include <QFileInfo>
#import <Foundation/Foundation.h>

namespace
{
NSURL *url(const QString &path)
{
	const auto bytes = path.toUtf8();
	return [NSURL fileURLWithPath:[NSString stringWithUTF8String:bytes.constData()]];
}
QString describe(NSError *error)
{
	return error ? QString::fromUtf8([[error localizedDescription] UTF8String]) :
		QStringLiteral("The system returned no Trash result.");
}
}

OpTrash::Result OpTrashPlatform::move(const QString &path, const OpStamp &expected,
	const std::atomic<bool> &cancel)
{
	@autoreleasepool
	{
		if (cancel.load()) return {OpTrash::Outcome::Cancelled, {}, {}, "Cancelled before system Trash.", {}};
		if (!expected.unchanged(OpFile::inspect(path)))
			return {OpTrash::Outcome::Failed, {}, {}, "The source changed before system Trash.", {}};
		NSURL *resultURL = nil;
		NSError *error = nil;
		const BOOL succeeded = [[NSFileManager defaultManager] trashItemAtURL:url(path)
			resultingItemURL:&resultURL error:&error];
		OpTrash::Result result;
		if (resultURL)
		{
			result.path = QString::fromUtf8([[resultURL path] UTF8String]);
			result.landed = OpFile::inspect(result.path);
			result.receipt = receipt("macos", result.path, result.landed);
		}
		if (!succeeded || !resultURL)
		{
			result.error = describe(error);
			// A native failure can be ambiguous. No second deletion/fallback is
			// authorized by this error; the coordinator retains the journal.
			return result;
		}
		if (!expected.unchanged(result.landed) || OpFile::occupied(path))
		{
			result.error = "The system Trash result needs identity or location reconciliation.";
			return result;
		}
		QString syncError;
		if (NativeFile::syncDirectory(QFileInfo(path).absolutePath(), &syncError) != NativeFile::SyncResult::Ok ||
			NativeFile::syncDirectory(QFileInfo(result.path).absolutePath(), &syncError) != NativeFile::SyncResult::Ok)
		{
			result.error = "File is in system Trash, but directory persistence needs confirmation. " + syncError;
			return result;
		}
		result.outcome = OpTrash::Outcome::Succeeded;
		return result;
	}
}

OpTrash::Result OpTrashPlatform::restore(const QJsonObject &saved, const QString &destination,
	const OpStamp &expected, const std::atomic<bool> &cancel)
{
	if (saved["provider"].toString() != "macos")
		return {OpTrash::Outcome::Failed, {}, {}, "This is not a macOS Trash receipt.", {}};
	if (cancel.load()) return {OpTrash::Outcome::Cancelled, {}, {}, "Cancelled before Trash restoration.", {}};
	const QString resolved = saved["resolvedPath"].toString();
	const QString path = resolved.isEmpty() ? saved["path"].toString() : resolved;
	QString error;
	auto item = OpFile::open(path, false, error);
	if (!item || !item->stillAt(path, expected))
		return {OpTrash::Outcome::Failed, path, {}, "The recorded Trash item changed or is missing. " + error, {}};
	if (item->relocate(path, destination, error) != OpFile::Relocation::Moved)
		return {OpTrash::Outcome::Failed, path, {}, error, {}};
	OpTrash::Result result;
	result.path = destination;
	result.landed = item->stamp();
	if (NativeFile::syncDirectory(QFileInfo(path).absolutePath(), &error) != NativeFile::SyncResult::Ok ||
		NativeFile::syncDirectory(QFileInfo(destination).absolutePath(), &error) != NativeFile::SyncResult::Ok)
	{
		result.error = "Restored file needs directory-persistence confirmation. " + error;
		return result;
	}
	result.outcome = OpTrash::Outcome::Succeeded;
	return result;
}
