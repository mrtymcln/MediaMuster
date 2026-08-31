#pragma once

#include "backgroundjob.h"
#include "mdbparser.h"
#include "mediafile.h"
#include "pmrparser.h"
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVector>
#include <atomic>

// MARK: - LogMsg

/// One coalesced log line. The ONE scanner type that has to sit at file
/// scope: MediaScanner::scanLogBatch takes a QVector<LogMsg>, and moc
/// needs the complete type when it processes that signal declaration.
/// (ScanTask and FolderResult have no such constraint and are nested
/// inside MediaScanner, where their only users are.)
///
/// `module` is the console tag ('scanner', 'mxf', 'pmr', 'mdb').
struct LogMsg
{
	QtMsgType level = QtInfoMsg;
	QString module;
	QString message;
};

// MARK: - MediaScanner

/// Walks volumes, finds `Avid MediaFiles/MXF` roots, and builds one
/// MediaFile per essence file in two passes:
///
///   Pass 1 — databases. Per folder: list the files, read `msmFMID.pmr`
///            (filename → MOBs, project) and `msmMMOB.mdb` (everything
///            else: clip name, bin, source, codec, dims, rates, duration,
///            bits, channels, type). A row the databases fully describe is
///            finished here and its file is never opened.
///   Pass 2 — headers. Only for the rows pass 1 could not cover — no PMR
///            entry (Interplay keeps records centrally; a file MediaMuster
///            just copied in), an incomplete MDB record, a file changed since
///            Avid indexed it, an unreadable database, or the Debug toggle —
///            read the MXF header as before, then try the MDB once more by
///            the header's own UMID to recover name/bin/source.
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
		/// Debug ▸ Force header scan. The databases are still read — they
		/// supply project, MOB ids, clip name, bin and source exactly as
		/// before — but no row takes its TECHNICAL facts from them, so every
		/// .mxf goes through the header pass: the pre-database-first
		/// behaviour, and the tool for comparing the two.
		bool forceHeaderScan = false;
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

	// MARK: - Per-folder work
	//
	// Nested: nothing outside this class names either type.

	/// One unit of folder-level work submitted to QtConcurrent::mapped.
	/// Self-contained so the parser doesn't reach back into the scanner.
	struct ScanTask
	{
		QString folderPath;
		QString folderNumber;
		QString volumeName;
		QString volumePath;
	};

	/// What a per-folder task produces. Logs are buffered here
	/// so the orchestrator can drain them in input order.
	struct FolderResult
	{
		QVector<MediaFile> files;
		QVector<LogMsg> logs;
	};

	FolderResult processFolderTask(const ScanTask &task);

	/// Per-folder counts for the console: rows the databases described,
	/// rows left for the header pass, and rows whose file changed since Avid
	/// indexed it (the staleness guard sent them to the header pass).
	struct CoverageTally
	{
		int covered = 0;
		int header = 0;
		int stale = 0;
	};

	/// One row from one directory entry (pass 1). `folderStatus` is the
	/// status computed by processFolderTask for any file the folder's PMR
	/// does NOT name: a real miss ("No reference") when the databases were
	/// readable, else the couldn't-check states.
	MediaFile buildMediaFile(const QFileInfo &fi, const QString &volumeName,
							 const QString &volumePath, const QString &folderNumber,
							 const PmrParser::ProjectMap &pmrMap,
							 const MdbDatabase &mdb,
							 MediaFile::DbStatus folderStatus, CoverageTally &tally);

	/// Pass 2. Reads the header of every .mxf row pass 1 left without
	/// technical facts, in parallel, then re-joins each against its folder's
	/// cached clip records by the header's UMID (the file-in-MDB-but-not-PMR
	/// case). Per-folder parallelism alone starves cores on small folders,
	/// so this runs over all rows after the walk.
	void parseMxfHeadersConcurrently(QVector<MediaFile> &files);

	// MARK: - Log batching

	/// Thread-safe log append. Called from the orchestrator thread; pool
	/// threads buffer into FolderResult::logs instead, so console order is
	/// deterministic.
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

	/// Each folder's clip records (MdbDatabase::masters), cached by pass 1
	/// for pass 2's UMID re-join and dropped in concludeScan. Keyed through
	/// PathKey::normalise so pass 1 (QDir::filePath) and pass 2
	/// (QFileInfo::absolutePath) can't drift on the same folder identity.
	/// Written under m_mdbMapsMutex by pool threads during pass 1; read
	/// without it in pass 2, when no writer exists.
	QMutex m_mdbMapsMutex;
	QHash<QString, QHash<QString, MdbMaster>> m_mdbMapsByFolder;
};