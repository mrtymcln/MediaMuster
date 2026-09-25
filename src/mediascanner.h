#pragma once

#include "backgroundjob.h"
#include "avidmedialayout.h"
#include "mdbparser.h"
#include "mediafile.h"
#include "pmrparser.h"
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QVector>
#include <atomic>

// MARK: - LogMsg

/// One buffered console line. Kept at file scope so Qt's signal generator
/// sees the complete type used by scanLogBatch.
struct LogMsg
{
	QtMsgType level = QtInfoMsg;
	QString module;
	QString message;
};

// MARK: - MediaScanner

/// Builds an inventory of recognised Avid media files in two passes:
/// directory listings and PMR/MDB joins, then media reads for incomplete or
/// stale rows. Complete current database metadata can avoid a media read.
/// AvidMediaLayout defines eligible locations; databases alone do not qualify
/// a folder. Cancellation returns the partial inventory gathered so far.
/// Scope and metadata rules: docs/current-behaviour.md.
class MediaScanner : public QObject
{
	Q_OBJECT
public:
	struct Options
	{
		/// Drive roots and the system-drive bases: scanned by
		/// scanVolumeRoot, top level only.
		QStringList volumePaths;
		/// Correctly structured Avid media roots, their media folders, or
		/// a directory directly containing those roots, added by the user.
		QStringList manualPaths;
		/// Session-only opt-in for OMFI roots and legacy OMF/audio essence.
		/// MXF and its PMR/MDB metadata remain available by default.
		bool includeOmf = false;
	};

	explicit MediaScanner(QObject *parent = nullptr);

	/// Uses the same managed-tree resolution as a manually requested scan.
	/// OMF roots qualify here even while their session feature is disabled.
	static bool canScanPath(const QString &path);

	/// Join before caches and mutexes are destroyed: the worker accesses them,
	/// and m_job is declared first, so its own destructor would run too late.
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

	/// Header reads and MDB recovery are complete. Effect naming and tallying
	/// remain; the UI switches to indeterminate progress for this final work.
	void scanFinalising();

	/// Coalesces up to ~50 lines or ~100 ms, whichever hits first.
	/// Keeps the UI smooth under heavy load.
	void scanLogBatch(const QVector<LogMsg> &batch);

	void scanFinished(const QVector<MediaFile> &results);

private:
	// MARK: - Scan stages
	//
	// Everything below runs off-thread (BackgroundJob worker or
	// shared thread pool). Don't call from UI handlers.

	void doScan();

	/// Clear per-scan caches and flush logs on both completion and cancellation,
	/// so later scans cannot inherit stale metadata.
	void concludeScan(const QVector<MediaFile> &files, bool cancelled);

	/// A volume path: probe `<path>/Avid MediaFiles/MXF` and, OMF-era,
	/// `<path>/OMFI MediaFiles`; scan whichever exist; never look deeper.
	QVector<MediaFile> scanVolumeRoot(const QString &volumePath, const QString &volumeName);

	/// Resolve an explicitly added managed media tree, leaf or immediate
	/// container. No recursive archive search or database-only fallback.
	QVector<MediaFile> scanAddedFolder(const QString &folderPath, const QString &volumeName);

	QVector<MediaFile> scanMxfRoot(const QString &mxfRootPath, const QString &volumeName,
								   const QString &volumePath);

	/// Scan the flat OMF root plus one level of legacy workstation folders.
	/// Each folder uses its own databases; staging/reserved folders are skipped.
	QVector<MediaFile> scanOmfRoot(const QString &omfRootPath, const QString &volumeName,
								   const QString &volumePath);

	// MARK: - Per-folder work
	//
	// Nested: nothing outside this class names either type.

	/// One unit of folder-level work submitted to the parallel map.
	/// Self-contained so the parser doesn't reach back into the scanner.
	struct ScanTask
	{
		AvidMediaLayout::Family family = AvidMediaLayout::Family::Mxf;
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

	/// What one folder's databases said, merged across every spelling
	/// present (Conventions::kPmrFileNames / kMdbFileNames). `pmrExists` /
	/// `mdbExists` mean "at least one file of that kind was there";
	/// `pmrOk` / `mdbOk` mean "and the msm* one parsed (or, with no msm*,
	/// some file of that kind did)" — an ama* twin that fails beside a
	/// readable msm* sibling is ignored, so the folder's verdict is exactly
	/// what the msm* pair alone would have given.
	struct FolderDatabases
	{
		PmrIndex pmr;
		MdbDatabase mdb;
		bool pmrExists = false;
		bool pmrOk = true;
		bool mdbExists = false;
		bool mdbOk = true;
	};

	/// Reads and merges the folder's PMR/MDB files, buffering Console notices
	/// in `logs` (see FolderResult). PMR entries append per
	/// filename; MDB records insert only when the key is new, so the msm*
	/// pair — read first — wins over an ama* twin describing the same mob.
	static FolderDatabases readFolderDatabases(const ScanTask &task, QVector<LogMsg> &logs);

	/// One row from one directory entry (pass 1). `folderStatus` is the
	/// status computed by processFolderTask for any file the folder's PMR
	/// does NOT name: a real miss ("No reference") when the databases were
	/// readable, else the couldn't-check states. The accepted layout selects
	/// the media family independently of what the databases contain.
	MediaFile buildMediaFile(const QFileInfo &fi, const QString &volumeName,
							 const QString &volumePath, const QString &folderNumber,
							 AvidMediaLayout::Family family,
							 const PmrIndex &pmrMap,
							 const MdbDatabase &mdb,
							 MediaFile::DbStatus folderStatus);

	/// Read rows marked needsHeaderRead, then rejoin cached MDB records by
	/// recovered master identity. Parallelise across all rows so a scan with
	/// few folders can still use multiple workers.
	void readMediaHeadersConcurrently(QVector<MediaFile> &files);

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

	BackgroundJob m_job;
	std::atomic<bool> m_running{false};

	Options m_options;

	/// Guard pending logs while swapping batches out for emission. Folder
	/// workers return their own buffers for the orchestrator to replay.
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
	/// a canonical, case-preserving path so distinct directories on a
	/// case-sensitive share cannot borrow each other's clip records.
	/// Written under m_mdbMapsMutex by pool threads during pass 1; read
	/// without it in pass 2, when no writer exists.
	QMutex m_mdbMapsMutex;
	QHash<QString, QHash<QString, MdbMasterMob>> m_mdbMapsByFolder;
	/// Guarded by the same mutex; overlapping scan roots enumerate a folder once.
	QSet<QString> m_seenFolders;
};
