#pragma once

#include "volumeidentity.h"
#include "opfile.h"
#include "oprequest.h"
#include <QDateTime>
#include <QFile>
#include <QLockFile>
#include <QVector>
#include <optional>

// Each append contains the complete state of ONE item. A valid prefix survives
// a torn last append. Journal errors stop the engine; recovery and Undo records
// are protected from pruning.
class OpJournal
{
public:
	enum class Step
	{
		Planned,
		Copying,
		CopyReady,
		Publishing,
		Published,
		Relocating,
		RemovingSource,
		SourceRemoved,
		Done,
		NoEffect,
		SourceRetained,
		Skipped,
		Cancelled,
		Failed,
		TrashFallback,
		RestoringSource,
		SourceRestored,
		NeedsAttention
	};
	struct Cleanup
	{
		QString directory;
		OpStamp directoryStamp;
		QString file;
		OpStamp fileStamp;
		bool removeFile = false;
	};
	struct Entry
	{
		int id = -1;
		OpItem item;
		QString originalSource;
		VolumeIdentity originalVolume; // Never re-anchored; future Undo's original location.
		QString originalRelativePath;
		QString dst;
		QString temp;
		QString mechanism;
		QString retirement;
		QString trashProvider;
		QString trashReceipt;
		bool trashFallbackApproved = false;
		bool explicitSkip = false;
		bool sourceRemoved = false;
		int attempts = 0;
		int undoEntryId = -1;
		QString undoAction;
		bool copyDurable = false;
		bool metadataComplete = false;
		QString error;
		OpStamp source;
		OpStamp landed;
		Step step = Step::Planned;
		QStringList artifacts;
		QVector<Cleanup> cleanup;
		bool complete() const;
		bool needsOriginalRestoration() const;
		QJsonObject json() const;
		static std::optional<Entry> fromJson(const QJsonObject &json);
	};
	struct Record
	{
		QString path;
		QString started;
		OpRequest request;
		QVector<VolumeIdentity> volumes;
		QVector<Entry> entries;
		QStringList changedFolders;
		bool stopped = false;
		bool dismissed = false;
		bool copiesComplete = false;
		QString undoPath;
		bool torn = false;
		bool corrupt = false;
		qint64 validBytes = 0;
	};

	OpJournal() = default;
	bool create(const OpRequest &request, const QString &directory, QString &error);
	bool resume(const Record &record, QString &error);
	bool save(const Entry &entry);
	bool touchFolder(const QString &folder);
	bool finish(bool cancelled);
	bool markCopiesComplete();
	bool claimUndo(const QString &undoPath);
	bool healthy() const
	{
		return m_healthy;
	}
	void stop(const QString &error)
	{
		m_healthy = false;
		m_error = error;
	}
	QString path() const
	{
		return m_file.fileName();
	}
	const Record &record() const
	{
		return m_record;
	}
	QString error() const
	{
		return m_error;
	}
	static QString standardJournalDir();
	static bool standardDirWritable();
	static QVector<Record> scan(const QString &directory = {});
	// Journal-only discovery: disconnected storage must not hide an unfinished job.
	static QVector<Record> interrupted(const QString &directory = {});
	static std::optional<Record> latestUndoable(const QString &directory = {});
	// Acquires the operation lock; removes completed journals last updated over
	// 30 days ago, preserving recovery evidence and Undo dependencies.
	static bool prune(const QString &directory, QString &error,
					  const QDateTime &now = QDateTime::currentDateTimeUtc());
	static std::optional<Record> readOne(const QString &path);
	static bool dismiss(const QString &path, QString &error);
	static QString canonicalPath(const QString &path);
	static QString stepName(Step step);
	static bool resolve(Record &record, QString &error,
						const QVector<VolumeIdentity> &mounted = {});
	// Protective restoration does not depend on the copied destination being
	// present: resolve only the volumes containing originals awaiting return.
	static bool resolveRestoration(Record &record, QString &error,
								   const QVector<VolumeIdentity> &mounted = {});
	static std::unique_ptr<QLockFile> acquire(const QString &directory, QString &error);

private:
	bool append(const QJsonObject &value);
	QFile m_file;
	Record m_record;
	bool m_healthy = false;
	QString m_error;
};
