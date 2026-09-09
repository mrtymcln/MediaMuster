#pragma once

#include "fileidentity.h"
#include "opfile.h"
#include "oprequest.h"
#include <QFile>
#include <QLockFile>
#include <QVector>
#include <optional>

// Each append contains the complete state of ONE item. A valid prefix survives
// a torn last append. Journal errors stop the engine; records are never pruned.
class OpJournal
{
  public:
	enum class Step
	{
		Planned,
		Copying,
		Verified,
		Publishing,
		Published,
		Relocating,
		Done,
		SourceRetained,
		Skipped,
		Cancelled,
		Failed,
		NeedsAttention
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
		QString hash;
		bool copyDurable = false;
		bool metadataComplete = false;
		QString error;
		OpStamp source;
		OpStamp landed;
		Step step = Step::Planned;
		QStringList artifacts;
		bool complete() const;
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
	static QStringList unreadableRecords(const QString &directory = {});
	static std::optional<Record> readOne(const QString &path);
	static bool dismiss(const QString &path, QString &error);
	static QString canonicalPath(const QString &path);
	static QString stepName(Step step);
	static bool resolve(Record &record, QString &error,
						const QVector<VolumeIdentity> &mounted = {});
	static std::unique_ptr<QLockFile> acquire(const QString &directory, QString &error);

  private:
	bool append(const QJsonObject &value);
	QFile m_file;
	Record m_record;
	bool m_healthy = false;
	QString m_error;
};
