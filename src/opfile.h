#pragma once

#include "nativefile.h"
#include <QFile>
#include <QJsonObject>
#include <memory>

// Native object identity and metadata captured for later comparison.
struct OpStamp
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
	bool sameObject(const OpStamp &other) const;
	bool unchanged(const OpStamp &other) const;
	QJsonObject json() const;
	static OpStamp fromJson(const QJsonObject &value);
};

// Owns an open file handle and closes it on destruction.
// Paths are used for presentation and native no-overwrite relocation.
class OpFile
{
public:
	enum class Relocation
	{
		Moved,
		Exists,
		CrossVolume,
		Failed
	};
	static std::unique_ptr<OpFile> open(const QString &path, bool create, QString &error);
	// Opens an existing regular file for read/write without creating or truncating it.
	static std::unique_ptr<OpFile> openWritableExisting(const QString &path, QString &error);
	static OpStamp inspect(const QString &path);
	static OpStamp inspectDirectory(const QString &path);
	static bool occupied(const QString &path);
	// OkDegraded means created, but the storage does not support directory flush.
	static NativeFile::SyncResult makeDirectory(
		const QString &path, QString &error,
		const NativeFile::DirectorySync &sync = NativeFile::syncDirectory);
	// Exclusively create an operation-owned staging/retirement directory. The
	// identity remains available if creation succeeds but persistence fails.
	static NativeFile::SyncResult makePrivateDirectory(
		const QString &path, OpStamp &identity, QString &error,
		const NativeFile::DirectorySync &sync = NativeFile::syncDirectory);
	// Never recursive. Requires the recorded directory identity, not its name
	// alone; a nonempty, replaced or redirected directory is retained.
	static NativeFile::SyncResult removeEmptyPrivateDirectory(
		const QString &path, const OpStamp &identity, QString &error,
		const NativeFile::DirectorySync &sync = NativeFile::syncDirectory);
	static bool safePath(const QString &path);

	QFile &io()
	{
		return m_file;
	}
	OpStamp stamp() const;
	bool stillAt(const QString &path, const OpStamp &expected) const;
	bool protectedFromWriters() const
	{
		return m_protected;
	}
#ifndef Q_OS_WIN
	bool preserveMetadataFrom(OpFile &source, QString &error);
#endif
	bool checkCopySupport(QString &error) const;
	NativeFile::SyncResult sync();
	Relocation relocate(const QString &from, const QString &to, QString &error);
	// No check-then-unlink fallback. Unsupported removal retains the file.
	bool removeProtected(QString &error);
	// Remove only a journalled payload.partial in its recorded private staging
	// directory. expectedFile must reflect its latest confirmed contents.
	bool removePartial(const OpStamp &expectedFile, const OpStamp &expectedDirectory,
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

private:
	friend class OpCopier;
	static std::unique_ptr<OpFile> openImpl(const QString &path, bool create, bool writable,
											QString &error);
	OpFile() = default;
	QFile m_file;
	QString m_path;
	bool m_protected = false;
	bool m_created = false;
};
