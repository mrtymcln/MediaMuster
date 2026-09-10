#pragma once

#include "nativefile.h"
#include <QFile>
#include <QJsonObject>
#include <memory>

// A handle, not a pathname, owns an open file. Destruction only closes it.
// Paths are used for presentation and for native no-overwrite relocation.
struct OpStamp
{
	QString fileId;
	QString volumeId;
	qint64 size = -1;
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
	static OpStamp inspect(const QString &path);
	static bool occupied(const QString &path);
	// OkDegraded means created, but the storage does not support directory flush.
	static NativeFile::SyncResult makeDirectory(
		const QString &path, QString &error,
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
	bool preserveMetadataFrom(OpFile &source, QString &error);
	bool canStreamCopy(QString &error) const;
	NativeFile::SyncResult sync();
	Relocation relocate(const QString &from, const QString &to, QString &error);
	// No check-then-unlink fallback. Unsupported removal retains the file.
	bool removeProtected(QString &error);
	QString path() const
	{
		return m_path;
	}

  private:
	OpFile() = default;
	QFile m_file;
	QString m_path;
	bool m_protected = false;
	bool m_created = false;
};
