#pragma once

#include "mdbparser.h"
#include "mediafile.h"
#include "mxfparser.h"
#include "pmrparser.h"
#include <QElapsedTimer>
#include <QFuture>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QtConcurrent>
#include <atomic>

class ScannerWorker;

// LogMsg lives at file scope because MediaScanner's scanLogBatch signal takes
// a QVector<LogMsg> — moc needs the full type visible at signal-declaration time.
struct LogMsg
{
	int level;
	QString module;
	QString message;
};

class MediaScanner : public QObject
{
	Q_OBJECT
public:
	struct Options
	{
		QStringList drivePaths;
		bool parseMxfHeaders = true;
		bool scanUnmanaged = true;
		bool followSymlinks = false;
		int maxDepth = 10;
	};

	explicit MediaScanner(QObject *parent = nullptr);
	~MediaScanner();

	void startScan(const Options &options);
	void cancelScan();
	bool isRunning() const { return m_running.load(); }

signals:
	void scanProgress(const QString &phase, int current, int total,
					  const QString &currentPath);

	// Coalesced log signal. Emits up to ~50 lines per batch or every
	// ~100 ms, whichever comes first.
	void scanLogBatch(const QVector<LogMsg> &batch);
	void scanFinished(const QVector<MediaFile> &results);

private:
	QThread *m_thread = nullptr;
	// QPointer auto-clears when the worker is destroyed via deleteLater
	// on the worker thread, so cancelScan from the UI thread can safely
	// race with the worker's natural shutdown.
	QPointer<ScannerWorker> m_worker;
	std::atomic<bool> m_running{false};
};

struct ScanTask
{
	QString folderPath;
	QString folderNumber;
	QString driveName;
	QString drivePath;
};

struct FolderResult
{
	QVector<MediaFile> files;
	QVector<LogMsg> logs;
};

class ScannerWorker : public QObject
{
	Q_OBJECT
public:
	explicit ScannerWorker(const MediaScanner::Options &options);
	void cancel();

public slots:
	void process();

signals:
	void progress(const QString &phase, int current, int total,
				  const QString &currentPath);

	// Coalesced log signal: emitted in batches of up to ~50 entries or
	// every ~100 ms (whichever comes first) to keep the console clean.
	void logBatch(const QVector<LogMsg> &batch);
	void finished(const QVector<MediaFile> &results);

private:
	void doScan();

	// Thread-safe; the mutex lets pool threads append without racing the worker.
	void emitLog(int level, const QString &module, const QString &msg);
	void flushLogs();

	QVector<MediaFile> scanDrive(const QString &drivePath,
								 const QString &driveName);
	QVector<MediaFile> scanMxfRoot(const QString &mxfRootPath,
								   const QString &driveName,
								   const QString &drivePath);

	FolderResult processFolderTask(const ScanTask &task);

	MediaFile buildMediaFile(const QString &filePath, const QString &driveName,
							 const QString &drivePath,
							 const QString &folderNumber,
							 const PmrParser::ProjectMaps &pmrMaps,
							 const MdbParser::RecordMap &mdbMap);

	// Stage 2 — runs in parallel across every collected MXF file once the
	// folder walk is done. Pulls all idle cores into the slow part of the
	// scan instead of leaving them per-folder bound.
	void parseMxfHeadersConcurrently(QVector<MediaFile> &files);

	bool isNonPortableFilename(const QString &name) const;
	static bool canReadPath(const QString &path);

	MediaScanner::Options m_options;
	std::atomic<bool> m_cancel{false};

	// Log batching: guarded by m_logMutex. Both the worker thread and
	// QtConcurrent pool threads append to m_pendingLogs via emitLog().
	QMutex m_logMutex;
	QVector<LogMsg> m_pendingLogs;

	// Flush clock; scan-scoped (member, not thread_local) so it dies with
	// the scan rather than persisting in QtConcurrent pool threads across
	// rescans. Started at the top of process(); accessed under m_logMutex.
	QElapsedTimer m_flushTimer;
	qint64 m_lastFlushElapsed = 0;
};