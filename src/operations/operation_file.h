#pragma once

#include "storage/file_sync.h"
#include <QFile>
#include <QJsonObject>
#include <memory>

// Native object identity and metadata captured for later comparison.
// MARK: - File identity stamps

struct OperationStamp
{
	QString fileId;
	QString volumeId;
	qint64 size = -1; // Bytes; -1 when unavailable.
	// Native modification value: POSIX nanoseconds or Windows FILETIME ticks,
	// distinct from OpItem::modifiedMs (milliseconds).
	qint64 modified = 0;
	bool valid() const
	{
		return !fileId.isEmpty() && size >= 0;
	}
	bool sameObject(const OperationStamp &other) const;
	bool unchanged(const OperationStamp &other) const;
	QJsonObject json() const;
	static OperationStamp fromJson(const QJsonObject &value);
};

// Owns an open file handle and closes it on destruction.
// Paths are used for presentation and native no-overwrite relocation.
// MARK: - Protected file access

class OperationFile
{
public:
	enum class Relocation
	{
		Moved,
		Exists,
		CrossVolume,
		Failed
	};
	static std::unique_ptr<OperationFile> open(const QString &path, bool create, QString &error);
	// Opens an existing regular file for read/write without creating or truncating it.
	static std::unique_ptr<OperationFile> openWritableExisting(const QString &path, QString &error);
	static OperationStamp inspect(const QString &path);
	static OperationStamp inspectDirectory(const QString &path);
	static bool occupied(const QString &path);
	// OkDegraded means created, but the storage does not support directory flush.
	static FileSync::SyncResult makeDirectory(
		const QString &path, QString &error,
		const FileSync::DirectorySync &sync = FileSync::syncDirectory);
	// Exclusively create an operation-owned staging/retirement directory. The
	// identity remains available if creation succeeds but persistence fails.
	static FileSync::SyncResult makePrivateDirectory(
		const QString &path, OperationStamp &identity, QString &error,
		const FileSync::DirectorySync &sync = FileSync::syncDirectory);
	// Never recursive. Requires the recorded directory identity, not its name
	// alone; a nonempty, replaced or redirected directory is retained.
	static FileSync::SyncResult removeEmptyPrivateDirectory(
		const QString &path, const OperationStamp &identity, QString &error,
		const FileSync::DirectorySync &sync = FileSync::syncDirectory);
	static bool safePath(const QString &path);

	// MARK: - Opened file access and mutation

	QFile &io()
	{
		return m_file;
	}
	OperationStamp stamp() const;
	bool stillAt(const QString &path, const OperationStamp &expected) const;
	bool protectedFromWriters() const
	{
		return m_protected;
	}
#ifndef Q_OS_WIN
	bool preserveMetadataFrom(OperationFile &source, QString &error);
#endif
	bool checkCopySupport(QString &error) const;
	FileSync::SyncResult sync();
	Relocation relocate(const QString &from, const QString &to, QString &error);
	// No check-then-unlink fallback. Unsupported removal retains the file.
	bool removeProtected(QString &error);
	// Remove only a journalled payload.partial in its recorded private staging
	// directory. expectedFile must reflect its latest confirmed contents.
	bool removePartial(const OperationStamp &expectedFile, const OperationStamp &expectedDirectory,
					   QString &error);
	// Only for journalled original retirement: .mediamuster-retire-<UUID>/payload.retired.
	// The parent must be an exclusively owned 0700 directory on the source volume.
	// Caller records intent before relocation here; this method never removes an
	// original directly from its ordinary pathname. Failure leaves recovery evidence.
	bool removeOriginal(QString &error);
	QString path() const
	{
		return m_path;
	}

// MARK: - File handle state

private:
	friend class OperationCopier;
	static std::unique_ptr<OperationFile> openImpl(const QString &path, bool create, bool writable,
											QString &error);
	OperationFile() = default;
	QFile m_file;
	QString m_path;
	bool m_protected = false;
	bool m_created = false;
};
