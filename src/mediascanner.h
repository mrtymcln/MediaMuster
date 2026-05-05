#pragma once

#include "mdbparser.h"
#include "mediafile.h"
#include "mxfparser.h"
#include "pmrparser.h"
#include <QFuture>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QtConcurrent>
#include <atomic>

// MediaScanner — Controller for the background scanning process

class ScannerWorker; // Forward declaration

// Log entry struct: defined here because
// MediaScanner's scanLogBatch signal takes a QVector<LogMsg> and the moc
// needs the full type visible at signal-declaration time. Used by both
// the controller's batch signal and ScannerWorker's internal buffering.
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
	void scanError(const QString &error);

private:
	QThread *m_thread = nullptr;
	// QPointer auto-clears when the worker is destroyed via deleteLater
	// on the worker thread, so cancelScan from the GUI thread can safely
	// race with the worker's natural shutdown.
	QPointer<ScannerWorker> m_worker;
	std::atomic<bool> m_running{false};
};

// ScannerWorker — The isolated background task
struct ScanTask
{
	QString folderPath;
	QString folderNumber;
	QString driveName;
	QString drivePath;
	bool parseMxf;
};

// Result from a single folder scan: files + buffered logs
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
	// every ~100 ms (whichever comes first) to keep the GUI thread from
	// drowning in QPlainTextEdit::appendPlainText calls during big scans.
	void logBatch(const QVector<LogMsg> &batch);

	void finished(const QVector<MediaFile> &results);

private:
	void doScan();

	// Append a log line to the pending buffer. Thread-safe: can be called
	// from the worker thread or any QtConcurrent pool thread.
	void emitLog(int level, const QString &module, const QString &msg);

	// Drain the pending log buffer into a logBatch signal. Called from
	// the worker thread on a timer (~100 ms) and once at scan end.
	void flushLogs();

	QVector<MediaFile> scanDrive(const QString &drivePath,
								 const QString &driveName);
	QVector<MediaFile> scanMxfRoot(const QString &mxfRootPath,
								   const QString &driveName,
								   const QString &drivePath);

	// The isolated multi-threaded payload function
	FolderResult processFolderTask(const ScanTask &task);

	MediaFile buildMediaFile(const QString &filePath, const QString &driveName,
							 const QString &drivePath,
							 const QString &folderNumber,
							 const PmrParser::ProjectMaps &pmrMaps,
							 const MdbParser::RecordMap &mdbMap, bool parseMxf);

	bool isNonPortableFilename(const QString &name) const;
	static bool canReadPath(const QString &path);

	MediaScanner::Options m_options;
	std::atomic<bool> m_cancel;
	QStringList m_deniedPaths;

	// Log batching: guarded by m_logMutex. Both the worker thread and
	// QtConcurrent pool threads append to m_pendingLogs via emitLog().
	QMutex m_logMutex;
	QVector<LogMsg> m_pendingLogs;
};