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

// MARK: - Forward declarations

class ScannerWorker;

// MARK: - LogMsg

/// One coalesced log line. At file scope because
/// MediaScanner::scanLogBatch takes a QVector<LogMsg> and moc needs
/// the full type visible at signal-declaration time.
///
/// `level`: 0=info, 1=warn, 2=error, 3=success. `module` is the short
/// tag rendered in the console ("scanner", "mxf", "pmr", "mdb").
struct LogMsg
{
	int level = 0;
	QString module;
	QString message;
};

// MARK: - MediaScanner

/// Walks volumes, locates `Avid MediaFiles/MXF` roots, reads per-folder
/// `msmFMID.pmr` / `msmMMOB.mdb` databases, and parses MXF headers to
/// assemble a MediaFile per essence file.
///
/// Scan runs on a worker QThread we own. Kept the explicit-thread
/// pattern over BackgroundJob because the scanner needs per-folder
/// parallelism via QtConcurrent::mapped, which the simpler one-shot
/// job manager doesn't expose.
///
/// Cancellation is cooperative: cancelScan() flips a flag the worker
/// checks at folder and file boundaries.
class MediaScanner : public QObject
{
	Q_OBJECT
public:
	struct Options
	{
		QStringList volumePaths;
		bool parseMxfHeaders = true; ///< Off skips the slow KLV walk (Stage 2).
	};

	explicit MediaScanner(QObject *parent = nullptr);
	~MediaScanner();

	// MARK: - Public API

	/// No-op if a scan is already running (CAS against `m_running`
	/// guards rapid double-clicks).
	void startScan(const Options &options);

	/// Safe to call from any thread.
	void cancelScan();

	bool isRunning() const
	{
		return m_running.load();
	}

signals:

	// MARK: - Progress signals

	void scanProgress(const QString &phase, int current, int total,
	                  const QString &currentPath);

	/// Up to ~50 lines per batch or every ~100 ms, whichever comes
	/// first. Keeps the console readable and the UI event loop
	/// unburdened during big scans.
	void scanLogBatch(const QVector<LogMsg> &batch);

	void scanFinished(const QVector<MediaFile> &results);

private:
	QThread *m_thread = nullptr;

	/// QPointer auto-clears when the worker self-destructs on its own
	/// thread, so cancelScan from the UI thread can safely race with
	/// the worker's natural shutdown.
	QPointer<ScannerWorker> m_worker;

	std::atomic<bool> m_running{false};
};

// MARK: - ScanTask

/// One unit of folder-level work submitted to QtConcurrent::mapped.
/// Captures everything the per-folder parser needs without reaching
/// back into the worker.
struct ScanTask
{
	QString folderPath;
	QString folderNumber;
	QString volumeName;
	QString volumePath;
};

// MARK: - FolderResult

/// What a per-folder task produces. Logs are buffered here (rather
/// than emitted from the pool thread directly) so the main worker can
/// drain them in input order — keeps the console deterministic.
struct FolderResult
{
	QVector<MediaFile> files;
	QVector<LogMsg> logs;
};

// MARK: - ScannerWorker

/// The actual scanning logic. Lives on its own QThread, owned and
/// managed by MediaScanner.
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

	void logBatch(const QVector<LogMsg> &batch);

	void finished(const QVector<MediaFile> &results);

private:
	void doScan();

	/// Thread-safe log append. Both the worker thread and QtConcurrent
	/// pool threads call this via the buffered fast-path in
	/// processFolderTask.
	void emitLog(int level, const QString &module, const QString &msg);

	/// Drain pending logs as one logBatch emit. Called when the batch
	/// is full, when the age threshold hits, and once at end-of-scan
	/// so nothing gets stranded.
	void flushLogs();

	QVector<MediaFile> scanVolume(const QString &volumePath,
	                              const QString &volumeName);
	QVector<MediaFile> scanMxfRoot(const QString &mxfRootPath,
	                               const QString &volumeName,
	                               const QString &volumePath);

	FolderResult processFolderTask(const ScanTask &task);

	MediaFile buildMediaFile(const QString &filePath, const QString &volumeName,
	                         const QString &volumePath,
	                         const QString &folderNumber,
	                         const PmrParser::ProjectMaps &pmrMaps,
	                         const MdbParser::RecordMap &mdbMap);

	/// Stage 2: parses every collected MXF file in parallel after the
	/// folder walk. Most folders are tiny, so per-folder parallelism
	/// alone starves cores.
	void parseMxfHeadersConcurrently(QVector<MediaFile> &files);

	bool isNonPortableFilename(const QString &name) const;
	static bool canReadPath(const QString &path);

	MediaScanner::Options m_options;
	std::atomic<bool> m_cancel{false};

	// MARK: - Log batching

	/// Guarded by m_logMutex. Both the worker thread and QtConcurrent
	/// pool threads append via emitLog. The swap-then-emit dance in
	/// flushLogs minimises mutex hold time.
	QMutex m_logMutex;
	QVector<LogMsg> m_pendingLogs;

	/// Scan-scoped (member, not thread_local) so it dies with the
	/// scan rather than persisting in pool threads across rescans.
	QElapsedTimer m_flushTimer;
	qint64 m_lastFlushElapsed = 0;

	// MARK: - Over-cap folder tracking

	/// Watch list for folders over 4000 files (close to Avid's 5000
	/// ceiling). Pool threads append under m_overfullMutex. Drained
	/// into one summary line at end-of-scan.
	QMutex m_overfullMutex;
	QVector<QPair<QString, int>> m_overfullFolders;
};