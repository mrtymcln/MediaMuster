#pragma once

#include "backgroundjob.h"
#include "mdbparser.h"
#include "mediafile.h"
#include "pmrparser.h"
#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVector>
#include <atomic>

// MARK: - LogMsg

/// One coalesced log line. File-scope because
/// MediaScanner::scanLogBatch takes a QVector<LogMsg> and moc needs
/// the full type at signal-declaration time.
///
/// `module` is the console tag ('scanner', 'mxf', 'pmr', 'mdb').
struct LogMsg
{
	QtMsgType level = QtInfoMsg;
	QString module;
	QString message;
};

// MARK: - ScanTask

/// One unit of folder-level work submitted to QtConcurrent::mapped.
/// Self-contained so the parser doesn't reach back into the scanner.
struct ScanTask
{
	QString folderPath;
	QString folderNumber;
	QString volumeName;
	QString volumePath;
};

// MARK: - FolderResult

/// What a per-folder task produces. Logs are buffered here
/// so the orchestrator can drain them in input order.
struct FolderResult
{
	QVector<MediaFile> files;
	QVector<LogMsg> logs;
};

// MARK: - MediaScanner

/// Walks volumes, finds `Avid MediaFiles/MXF` roots, reads per-folder
/// `msmFMID.pmr` / `msmMMOB.mdb`, and parses MXF headers.
/// One MediaFile per essence file.
///
/// Cancellation is cooperative; checked at folder/file boundaries
/// so work in flight isn't left half-done.
class MediaScanner : public QObject
{
	Q_OBJECT
public:
	struct Options
	{
		QStringList volumePaths;
	};

	explicit MediaScanner(QObject *parent = nullptr);

	/// Joins the scan worker before any member unwinds. `m_job` is declared
	/// first (so destroyed last), and the worker touches m_logMutex /
	/// m_mdbMapsByFolder / m_pendingLogs etc. — left to the default dtor it
	/// would run on against members already gone. shutdown() closes that.
	~MediaScanner() override { m_job.shutdown(); }

	// MARK: - Public API

	/// No-op if a scan is already running; rapid double-clicks
	/// don't stack.
	void startScan(const Options &options);

	/// Safe to call from any thread.
	void cancelScan();

signals:

	// MARK: - Progress signals

	void scanProgress(int current, int total, const QString &currentPath);

	/// Enumeration and parse are done; post-walk finalisation (MDB recovery,
	/// tally) is running. The UI shows an indeterminate "Finalising..." so a
	/// slow finalise on a big or networked share can't look like a frozen 100%.
	void scanFinalising();

	/// Coalesces up to ~50 lines or ~100 ms, whichever hits first.
	/// Keeps the UI smooth under heavy load.
	void scanLogBatch(const QVector<LogMsg> &batch);

	void scanFinished(const QVector<MediaFile> &results);

private:
	// MARK: - Scan stages
	//
	// Everything below runs off-thread (BackgroundJob worker or
	// QtConcurrent pool). Don't call from UI handlers.

	void doScan();

	/// The one closing-up routine for every scan exit — cancel doors and
	/// the normal finish alike — so per-scan state (over-cap summary,
	/// cached MDB maps) can't leak into the next scan.
	void concludeScan(const QVector<MediaFile> &files, bool cancelled);

	QVector<MediaFile> scanVolume(const QString &volumePath, const QString &volumeName);
	QVector<MediaFile> scanMxfRoot(const QString &mxfRootPath, const QString &volumeName,
								   const QString &volumePath);

	FolderResult processFolderTask(const ScanTask &task);

	/// `folderDbIssue` is the folder-level database state computed by
	/// processFolderTask; it decides whether an unmatched file is a verified
	/// "No reference" or an unverifiable "No database".
	MediaFile buildMediaFile(const QString &filePath, const QString &volumeName,
							 const QString &volumePath, const QString &folderNumber,
							 const PmrParser::ProjectMaps &pmrMaps,
							 const MdbDatabase &mdb,
							 MediaFile::DbIssue folderDbIssue);

	/// Stage 2. Per-folder parallelism alone starves cores on small
	/// folders, so parse every MXF in parallel after the walk.
	void parseMxfHeadersConcurrently(QVector<MediaFile> &files);

	/// Stage 3. Re-join files the local databases couldn't attribute
	/// ("No reference" and "No database" alike — a readable MDB can still
	/// vouch for a file whose PMR was corrupt) against their folder's
	/// cached MDB via the MXF UMID. Mostly no-op outside Interplay or
	/// database corruption; sparse, so cheap.
	void recoverUnreferencedFromMdb(QVector<MediaFile> &files);

	// MARK: - Log batching

	/// Thread-safe log append. Called from the orchestrator and
	/// from QtConcurrent pool threads.
	void emitLog(QtMsgType level, const QString &module, const QString &msg);

	/// Drain pending logs. Fires when batch fills, age limit hits, or
	/// at scan end.
	void flushLogs();

	static bool isNonPortableFilename(const QString &name);
	static bool canReadPath(const QString &path);

	// MARK: - State

	BackgroundJob m_job{this};
	std::atomic<bool> m_running{false};

	Options m_options;

	/// Guarded by m_logMutex. Orchestrator + QtConcurrent pool threads
	/// all append via emitLog. The swap-then-emit dance in flushLogs
	/// keeps the mutex hold short.
	QMutex m_logMutex;
	QVector<LogMsg> m_pendingLogs;

	/// Scan-scoped (member, not thread_local); dies with the scan
	/// instead of sticking around in pool threads across rescans.
	QElapsedTimer m_flushTimer;
	qint64 m_lastFlushElapsed = 0;

	/// Watch list for folders over kFolderWarn (4,500) files, near Avid's
	/// 5,000 ceiling. Pool threads append under m_overfullMutex. Drained to
	/// one summary line at end of scan.
	QMutex m_overfullMutex;
	QVector<QPair<QString, int>> m_overfullFolders;

	/// Cached for Stage 3's UMID join. Keyed through PathKey::normalise
	/// so Stage 1 (QDir::filePath) and Stage 3 (QFileInfo::absolutePath)
	/// can't drift on the same folder identity.
	QMutex m_mdbMapsMutex;
	QHash<QString, QHash<QString, MdbMaster>> m_mdbMapsByFolder;
};