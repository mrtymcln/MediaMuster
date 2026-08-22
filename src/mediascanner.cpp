#include "mediascanner.h"
#include "effectcatalogue.h"
#include "avidlayout.h"
#include "avidlimits.h"
#include "debugslowdown.h"
#include "logging.h"
#include "mobid.h"
#include "mxfparser.h"
#include "pathkey.h"
#include "pmrkey.h"
#include "progressthrottle.h"
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QMutexLocker>
#include <QtConcurrent>
#include <array>

#ifdef Q_OS_MAC
#include <unistd.h>
#endif

// MARK: - MediaScanner construction

MediaScanner::MediaScanner(QObject *parent)
	: QObject(parent)
{
}

// MARK: - Scan lifecycle

void MediaScanner::startScan(const Options &options)
{
	// CAS against rapid double-clicks on Scan.
	bool expected = false;
	if (!m_running.compare_exchange_strong(expected, true))
		return;

	m_options = options;

	// Locked so leftover threads from a prior scan can't race us.
	{
		QMutexLocker lock(&m_logMutex);
		m_pendingLogs.clear();
	}
	// Belt to concludeScan's brace: even if a future exit path forgets
	// the closing-up routine, stale per-scan state can't cross scans.
	{
		QMutexLocker lock(&m_overfullMutex);
		m_overfullFolders.clear();
	}
	{
		QMutexLocker lock(&m_mdbMapsMutex);
		m_mdbMapsByFolder.clear();
	}
	m_flushTimer.start();
	m_lastFlushElapsed = 0;

	// RAII guard resets m_running on any exit path from the lambda.
	m_job.start(
		[this]
		{
			struct ResetRunning
			{
				std::atomic<bool> &flag;
				~ResetRunning() { flag.store(false); }
			} guard{m_running};
			doScan();
		});
}

void MediaScanner::cancelScan()
{
	// Safe from any thread. Noticed at folder/file boundaries.
	m_job.cancel();
}

// MARK: - Per-MediaFile derivations

namespace
{
	constexpr int kLogBatchMaxSize = 50;
	constexpr qint64 kLogBatchMaxAgeMs = 100;

	// MARK: - Media vs Precompute
	//
	// Decided by ONE fact Avid records in the file: the MaterialPackage's
	// UsageCode (local tag 0x4408). See MxfParser::isPrecomputeUsage.
	//
	// This replaced four name-shape rules on 2026-08-14, all now deleted.
	// Measured over 823 distinct real files, they were a poor stand-in:
	//   - the `P##.`/`W...##.` filename prefix matched 0 files
	//   - the Boris `,S_` test matched 0 files
	//   - the effect-keyword list could never be completed — the real render
	//     names here are 3D_Page_Fold, AniMatte, 14_9_Letterbox, 3D_Ball,
	//     1.85_Mask, 10101010, and not one was in the list
	//   - and it was wrong in both directions on ordinary names: a clip called
	//     "Resize test", "Flip Book" or "Untitled" classified as Precompute,
	//     while the one real Video Mixdown classified as Precompute only
	//     because "title" happens to sit inside "Untitled" — a mixdown is a
	//     master clip, not a render (Avid community thread 90606).
	// UsageCode gets all 823 right, including the mixdown, which carries no
	// UsageCode and so needs no special case.
	//
	// The databases carry the same verdict as an integer — OMFI:MOBJ:UsageCode
	// 1 on a precompute's MASTER mob, 7 on a master clip's (MdbParser reads
	// it; 1,155 corpus joins plus 54 real-drive renders agree with the
	// header's 0x4408) — so a row the database covers is classified without a
	// header read. The file mob's code (9 on most renders, 0 on some) is not
	// part of the rule.
	//
	// Consequence worth knowing: a file whose MXF header cannot be read, and
	// that no database describes, gets no verdict and stays Media. That is
	// the honest answer — nothing about the file says otherwise — where the
	// old rules would guess from a name.

	/// The one place a clip name is ever assigned. Takes only when the new
	/// name comes from a STRICTLY better source, so the rungs of the ladder
	/// can arrive in any order — which they do: Stage 1 reads the MDB, Stage 2
	/// the MXF header, Stage 3 the MDB again. Strictly-better also means the
	/// first of two equal-ranked sources wins, so Stage 1's file-MOB record
	/// keeps precedence over the master-MOB record read moments later.
	void setClipName(MediaFile &mf, const QString &name, MediaFile::ClipNameSource src)
	{
		if (name.isEmpty() || int(src) <= int(mf.clipNameSource))
			return;
		mf.clipName = name;
		mf.clipNameSource = src;
	}

	// Assign only when src is non-empty and dst is empty. Keeps MDB
	// from thrashing values an earlier pass (PMR, MXF) set.
	template <typename T>
	void assignIfMissing(T &dst, const T &src)
	{
		if (!src.isEmpty() && dst.isEmpty())
			dst = src;
	}

	// Copy non-empty MDB fields onto the MediaFile. Shared by Stage 1
	// (file-MOB + master-MOB lookups) and Stage 3 (UMID lookup
	// after endian swap). assignIfMissing means call order doesn't
	// matter; first non-empty wins.
	//
	// The clip name is the MDB rung of the ladder (see
	// MediaFile::ClipNameSource): setClipName ranks it below a
	// MaterialPackage name, so it only ever shows for files whose MXF
	// header can't be read.
	//
	// The bin has no other source in the app: PmrEntry carries a project but
	// no bin, and AvbParser yields only MOB IDs for the Bin Filter. Unknown
	// therefore means blank, never a guess.
	void applyMdbRecord(MediaFile &mf, const MdbMaster &rec)
	{
		setClipName(mf, rec.clipName, MediaFile::ClipNameSource::Mdb);
		assignIfMissing(mf.originalBin, rec.bin);
		assignIfMissing(mf.sourceFilePath, rec.sourceFilePath);
		assignIfMissing(mf.sourceFileName, rec.sourceFileName);
		assignIfMissing(mf.sourceContainer, rec.sourceContainer);
		if (rec.isImported)
			mf.isImported = true;
	}

	/// Technical facts → the row. Shared by both producers: pass 1 hands in
	/// what msmMMOB.mdb says about the file, pass 2 what the file's own header
	/// says. Every value has already been through MxfParser::finalise, so the
	/// two cannot disagree on a derived field.
	void applyMetadata(MediaFile &mf, const MxfMetadata &mxf)
	{
		if (mxf.valid)
		{
			// Only a MaterialPackage name is a rung of the ladder; a
			// SourcePackage name never is (see MediaFile::ClipNameSource).
			if (mxf.clipNameFromMaterial)
				setClipName(mf, mxf.clipName, MediaFile::ClipNameSource::MaterialPackage);
			if (!mxf.codec.isEmpty())
				mf.codec = mxf.codec;
			if (!mxf.essenceContainerLabel.isEmpty())
				mf.codecHex = mxf.essenceContainerLabel.toHex().toUpper();
			if (!mxf.resolution.isEmpty())
				mf.resolution = mxf.resolution;
			if (!mxf.fps.isEmpty())
				mf.fps = mxf.fps;
			if (!mxf.bitDepth.isEmpty())
				mf.bitDepth = mxf.bitDepth;
			if (mxf.sampleRate > 0)
				mf.sampleRate = mxf.sampleRate;
			if (mxf.channels > 0)
				mf.channels = mxf.channels;
			if (mxf.durationFrames > 0)
				mf.durationFrames = mxf.durationFrames;
			if (mxf.timecodeBase > 0)
				mf.timecodeBase = mxf.timecodeBase;
			if (mxf.dropFrame)
				mf.dropFrame = true;
			// The producer owns audio-ness end to end (descriptor sets or the
			// essence label's own bytes in a header; the descriptor class in
			// the MDB). No display-name comparisons here.
			if (mxf.isAudio)
				mf.kind = MediaFile::Kind::Audio;
		}

		// Import facts a header carries as TaggedValues (UNC Path, Video,
		// _IMPORTSETTING). The MDB usually supplied them in pass 1; this is
		// what gives a row WITHOUT a database — Interplay — the same columns.
		assignIfMissing(mf.sourceFilePath, mxf.sourceFilePath);
		assignIfMissing(mf.sourceContainer, mxf.sourceContainer);
		if (mf.sourceFileName.isEmpty() && !mf.sourceFilePath.isEmpty())
			mf.sourceFileName = mf.sourceFilePath.section(QLatin1Char('/'), -1);
		if (mxf.hasImportSetting)
			mf.isImported = true;

		// The one place a file is classified. A parsed header or the
		// database's usage codes can say, and either says both ways: a render
		// is marked, and everything else is positively not one.
		mf.type = mxf.isPrecompute ? MediaFile::Type::Precompute : MediaFile::Type::Media;
	}
} // namespace

// MARK: - Log buffering

void MediaScanner::emitLog(QtMsgType level, const QString &module, const QString &msg)
{
	bool shouldFlush = false;
	{
		QMutexLocker lock(&m_logMutex);
		m_pendingLogs.append({level, module, msg});

		const qint64 nowMs = m_flushTimer.elapsed();
		shouldFlush = m_pendingLogs.size() >= kLogBatchMaxSize ||
					  (nowMs - m_lastFlushElapsed) >= kLogBatchMaxAgeMs;
		if (shouldFlush)
			m_lastFlushElapsed = nowMs;
	}
	if (shouldFlush)
		flushLogs();
}

void MediaScanner::flushLogs()
{
	// Swap-and-emit: the mutex isn't held across the queued signal.
	QVector<LogMsg> batch;
	{
		QMutexLocker lock(&m_logMutex);
		if (m_pendingLogs.isEmpty())
			return;
		batch.swap(m_pendingLogs);
	}
	emit scanLogBatch(batch);
}

// MARK: - Path readability

bool MediaScanner::canReadPath(const QString &path)
{
#ifdef Q_OS_MAC
	// access(2) R_OK skips the full stat that QFileInfo::isReadable does.
	return access(QFile::encodeName(path).constData(), R_OK) == 0;
#else
	return QFileInfo(path).isReadable();
#endif
}

// MARK: - Scan orchestration

void MediaScanner::doScan()
{
	QVector<MediaFile> allFiles;

	emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("Scanning %1 location(s)...").arg(m_options.volumePaths.size()));

	QElapsedTimer stageTimer;
	stageTimer.start();
	qCDebug(lcScanner) << "scan start:" << m_options.volumePaths.size() << "location(s)";

	// MARK: Pass 1 — per-volume folder walk + databases

	for (const QString &volumePath : m_options.volumePaths)
	{
		if (m_job.isCancelled())
			break;

		QDir volumeDir(volumePath);
		QString volumeName = volumeDir.dirName();
		if (volumeName.isEmpty())
			volumeName = volumePath;

		if (!canReadPath(volumePath))
		{
			emitLog(QtCriticalMsg, QStringLiteral("scanner"), QStringLiteral("Permission denied: %1").arg(volumePath));
			emitLog(QtWarningMsg, QStringLiteral("scanner"),
					"Grant Full Disk Access in System Preferences > Privacy & "
					"Security");
			continue;
		}

		emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("Scanning: %1 (%2)").arg(volumeName, volumePath));
		emit scanProgress(0, 0, volumePath);

		auto volumeFiles = scanVolume(volumePath, volumeName);
		allFiles.append(volumeFiles);

		if (!volumeFiles.isEmpty())
		{
			emitLog(QtInfoMsg, QStringLiteral("scanner"),
					QStringLiteral("  %1: %2 media files found").arg(volumeName).arg(volumeFiles.size()));
		}
	}

	qCDebug(lcScanner) << "pass 1 (walk + databases):" << allFiles.size() << "files in" << stageTimer.restart()
					   << "ms";

	if (m_job.isCancelled())
	{
		concludeScan(allFiles, /*cancelled=*/true);
		return;
	}

	// MARK: Pass 2 — headers for the rows the databases didn't cover

	parseMxfHeadersConcurrently(allFiles);
	qCDebug(lcScanner) << "pass 2 (headers):" << stageTimer.restart() << "ms";

	if (m_job.isCancelled())
	{
		concludeScan(allFiles, /*cancelled=*/true);
		return;
	}

	// Both passes are done and the bar has hit 100%. Tell the UI to show an
	// indeterminate "Finalising..." for the tally below so a slow finish on a
	// big share can't look like a frozen 100%.
	emit scanFinalising();

	// MARK: Summary — name the renders, tally, cleanup

	// Only rows the usage code already proved to be renders are looked up in
	// the effect catalogue — the name labels, it never decides.
	for (MediaFile &f : allFiles)
	{
		if (f.type != MediaFile::Type::Precompute)
			continue;
		const EffectCatalogue::Hit hit = EffectCatalogue::lookup(f.clipName);
		f.effect = hit.name;
		f.effectCategory = hit.category;
		f.effectSequence = hit.sequence;
		f.effectInstance = hit.instance;
	}

	int noReference = 0, noDatabase = 0, invalidUmid = 0, noProject = 0, nonPortable = 0;
	for (const auto &f : allFiles)
	{
		if (f.isNoReference)
			++noReference;
		if (f.isNoDatabase())
			++noDatabase;
		if (f.isInvalidUmid)
			++invalidUmid;
		if (f.isNoProject)
			++noProject;
		if (f.isNonPortable)
			++nonPortable;
	}

	qCDebug(lcScanner) << "scan tally:" << allFiles.size() << "files —" << noReference
					   << "no reference," << noDatabase << "no database," << invalidUmid << "invalid umid,"
					   << noProject << "no project," << nonPortable << "non-portable";

	if (allFiles.isEmpty())
	{
		emitLog(QtWarningMsg, QStringLiteral("scanner"), "No media files found.");
	}
	else
	{
		emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("Scan complete: %1 files found").arg(allFiles.size()));
	}

	if (noReference > 0)
		emitLog(QtWarningMsg, QStringLiteral("scanner"),
				QStringLiteral("%1 file%2 with no local database reference")
					.arg(noReference)
					.arg(noReference == 1 ? "" : "s"));
	if (noDatabase > 0)
		emitLog(QtWarningMsg, QStringLiteral("scanner"),
				QStringLiteral("%1 file%2 in folders with no readable database")
					.arg(noDatabase)
					.arg(noDatabase == 1 ? "" : "s"));
	if (invalidUmid > 0)
		emitLog(QtWarningMsg, QStringLiteral("scanner"),
				QStringLiteral("%1 file%2 with an invalid (all-zero) UMID").arg(invalidUmid).arg(invalidUmid == 1 ? "" : "s"));
	if (noProject > 0)
		emitLog(QtWarningMsg, QStringLiteral("scanner"),
				QStringLiteral("%1 file%2 with no project").arg(noProject).arg(noProject == 1 ? "" : "s"));
	if (nonPortable > 0)
		emitLog(QtWarningMsg, QStringLiteral("scanner"),
				QStringLiteral("%1 non-portable filename%2")
					.arg(nonPortable)
					.arg(nonPortable == 1 ? "" : "s"));

	concludeScan(allFiles, /*cancelled=*/false);
}

// MARK: - Scan conclusion

// The one closing-up routine: three cancel doors and the normal finish
// all leave through here, so per-scan state can't survive into the next
// scan whichever door fires. (It used to: only the normal exit cleared,
// so a cancelled scan's cached MDB maps could attribute the NEXT scan's
// files from a database that no longer existed on disk, and its over-cap
// entries duplicated the next summary.) startScan() clears the same
// state defensively — the second lock on the same door.
void MediaScanner::concludeScan(const QVector<MediaFile> &files, bool cancelled)
{
	if (cancelled)
		emitLog(QtWarningMsg, QStringLiteral("scanner"), "Scan cancelled by user");

	// MARK: Aggregate over-cap folder summary

	{
		QMutexLocker lock(&m_overfullMutex);
		if (!cancelled && !m_overfullFolders.isEmpty())
		{
			QString msg = QStringLiteral("%1 folder(s) over %2 files "
										 "(Avid recommends staying under %3):")
							  .arg(m_overfullFolders.size())
							  .arg(AvidLimits::kFolderWarn)
							  .arg(AvidLimits::kFolderMax);
			for (const auto &p : m_overfullFolders)
				msg += QStringLiteral("\n  %1 — %2 files").arg(p.first).arg(p.second);
			emitLog(QtWarningMsg, QStringLiteral("scanner"), msg);
		}
		m_overfullFolders.clear();
	}

	// Drop the cached clip records (only the masters were kept — a few
	// strings per clip), but staleness is the real reason to clear.
	{
		QMutexLocker lock(&m_mdbMapsMutex);
		m_mdbMapsByFolder.clear();
	}

	// Drain the log buffer first so the last batch doesn't land
	// after scanFinished.
	flushLogs();
	emit scanFinished(files);
}

// MARK: - Per-volume: locate Avid MediaFiles roots

QVector<MediaFile> MediaScanner::scanVolume(const QString &volumePath, const QString &volumeName)
{
	QVector<MediaFile> files;
	QDir dir(volumePath);
	const QString dirName = dir.dirName();

	// MARK: Case 1 — Volume root contains Avid MediaFiles/MXF

	const QString mxfViaRoot = AvidLayout::mxfRootUnder(volumePath);
	if (QDir(mxfViaRoot).exists())
	{
		emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("  Found Avid MediaFiles/MXF"));
		return scanMxfRoot(mxfViaRoot, volumeName, volumePath);
	}

	// MARK: Case 2 — Path is somewhere inside an Avid MediaFiles directory

	const int avidIdx = volumePath.indexOf(AvidLayout::kAvidMediaFilesDir, 0, Qt::CaseInsensitive);
	if (avidIdx >= 0)
	{
		const QString avidPart = volumePath.left(avidIdx + AvidLayout::kAvidMediaFilesDir.size());
		const QString mxfInside = avidPart + "/MXF";
		if (QDir(mxfInside).exists())
		{
			emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("  Found MXF folder at %1").arg(mxfInside));
			return scanMxfRoot(mxfInside, volumeName, avidPart);
		}

		if (AvidLayout::isMxfRootName(dirName))
		{
			emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("  Pointed directly at MXF folder"));
			return scanMxfRoot(volumePath, volumeName, QFileInfo(volumePath).absolutePath());
		}
	}

	// MARK: Case 3 — Path itself is an MXF or OMF root

	if (AvidLayout::isMxfRootName(dirName) || AvidLayout::isOmfRootName(dirName))
	{
		const QStringList subs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
		if (!subs.isEmpty())
		{
			emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("  MXF folder with %1 subfolders").arg(subs.size()));
			return scanMxfRoot(volumePath, volumeName, QFileInfo(volumePath).absolutePath());
		}
	}

	// MARK: Case 4 — Single media folder with per-folder databases

	if (QFile::exists(volumePath + "/msmMMOB.mdb") || QFile::exists(volumePath + "/msmFMID.pmr"))
	{
		emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("  Found database files in folder"));

		ScanTask t;
		t.folderPath = volumePath;
		t.folderNumber = dir.dirName();
		t.volumeName = volumeName;
		t.volumePath = QFileInfo(volumePath).absolutePath();

		auto result = processFolderTask(t);
		for (const auto &msg : result.logs)
			emitLog(msg.level, msg.module, msg.message);
		return result.files;
	}

	// MARK: Case 5 — Deep search (two levels)

	// Two levels covers the usual `~/Documents/Project/Avid MediaFiles`
	// layout without scanning the entire volume.
	emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("  Searching for Avid MediaFiles in %1...").arg(volumeName));

	QStringList searchDirs = {volumePath};
	for (const QString &sub1 : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
	{
		if (m_job.isCancelled())
			break;
		QString path1 = volumePath + "/" + sub1;
		if (!canReadPath(path1))
			continue;
		searchDirs.append(path1);

		QDir d1(path1);
		for (const QString &sub2 : d1.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
		{
			if (m_job.isCancelled())
				break;
			QString path2 = path1 + "/" + sub2;
			if (!canReadPath(path2))
				continue;
			searchDirs.append(path2);
		}
	}

	for (const QString &searchDir : searchDirs)
	{
		if (m_job.isCancelled())
			break;
		QString candidate = AvidLayout::mxfRootUnder(searchDir);
		if (QDir(candidate).exists())
		{
			emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("  Found Avid media at %1").arg(candidate));
			auto subFiles = scanMxfRoot(candidate, volumeName, searchDir);
			files.append(subFiles);
		}
	}

	if (files.isEmpty())
	{
		emitLog(QtWarningMsg, QStringLiteral("scanner"), QStringLiteral("  No Avid MediaFiles found in %1").arg(volumeName));
	}

	return files;
}

// MARK: - MXF root: parallel per-folder scan

QVector<MediaFile> MediaScanner::scanMxfRoot(const QString &mxfRootPath, const QString &volumeName,
											 const QString &volumePath)
{
	QVector<MediaFile> files;
	QDir mxfDir(mxfRootPath);

	if (!canReadPath(mxfRootPath))
	{
		emitLog(QtCriticalMsg, QStringLiteral("scanner"), QStringLiteral("  Permission denied: %1").arg(mxfRootPath));
		return files;
	}

	QStringList subFolders = mxfDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

	QList<ScanTask> tasks;
	for (const QString &folder : subFolders)
	{
		if (m_job.isCancelled())
			break;

		QString folderPath = mxfDir.filePath(folder);

		if (!canReadPath(folderPath))
		{
			emitLog(QtWarningMsg, QStringLiteral("scanner"), QStringLiteral("  Permission denied: %1").arg(folder));
			continue;
		}

		ScanTask t;
		t.folderPath = folderPath;
		t.folderNumber = folder;
		t.volumeName = volumeName;
		t.volumePath = volumePath;
		tasks.append(t);
	}

	emitLog(QtInfoMsg, QStringLiteral("scanner"),
			QStringLiteral("  %1 subfolders queued for concurrent scanning").arg(tasks.size()));

	std::atomic<int> completedFolders{0};
	const int totalFolders = tasks.size();

	// ~30 Hz emit cap when folders finish quickly.
	ProgressThrottle throttle;

	// QtConcurrent::mapped preserves input order, so buffered logs
	// replay in scan order.
	QFuture<FolderResult> future =
		QtConcurrent::mapped(tasks,
							 [this, &completedFolders, totalFolders, &throttle](const ScanTask &t)
							 {
								 auto res = this->processFolderTask(t);
								 int done = ++completedFolders;

								 // No-op when slow mode is off.
								 DebugSlowdown::pauseForMs(80);

								 // Always emit the last folder so the bar hits 100%;
								 // gate the rest.
								 if (done == totalFolders || throttle.shouldEmit())
								 {
									 emit scanProgress(done, totalFolders, t.folderPath);
								 }
								 return res;
							 });

	future.waitForFinished();

	// Drain per-task logs in input order; results() returns by
	// index no matter what order the pool threads finished.
	for (const auto &result : future.results())
	{
		for (const auto &msg : result.logs)
		{
			emitLog(msg.level, msg.module, msg.message);
		}
		files.append(result.files);
	}

	return files;
}

// MARK: - Per-folder task

FolderResult MediaScanner::processFolderTask(const ScanTask &task)
{
	FolderResult result;

	if (m_job.isCancelled())
		return result;

	// Buffer logs in the result instead of emitting from pool
	// threads. The orchestrator replays them in input order so
	// the console stays deterministic.
	auto bufLog = [&result](QtMsgType level, const QString &module, const QString &msg)
	{ result.logs.append({level, module, msg}); };

	// MARK: Parse the PMR

	// Missing PMR/MDB is normal in Interplay environments.
	PmrParser::ProjectMaps pmrMaps;
	bool pmrOk = true; // vacuously fine when the file doesn't exist
	const QString pmrPath = task.folderPath + "/msmFMID.pmr";
	const bool pmrExists = QFile::exists(pmrPath);
	if (pmrExists)
	{
		pmrMaps = PmrParser::buildFileMapWithFallback(pmrPath, &pmrOk);
		if (pmrOk)
			bufLog(QtInfoMsg, "pmr",
				   QStringLiteral("  PMR: %1 file entries in /%2")
					   .arg(pmrMaps.primary.size())
					   .arg(task.folderNumber));
		else
			bufLog(QtWarningMsg, "pmr",
				   QStringLiteral("  msmFMID.pmr in /%1 is unreadable; unmatched files here "
								  "surface as 'No database', not 'No reference'")
					   .arg(task.folderNumber));
	}
	else
	{
		bufLog(QtInfoMsg, "pmr", QStringLiteral("  No msmFMID.pmr in /%1").arg(task.folderNumber));
	}

	// MARK: Parse the MDB

	MdbDatabase mdb;
	bool mdbOk = true; // vacuously fine when the file doesn't exist
	const QString mdbPath = task.folderPath + "/msmMMOB.mdb";
	const bool mdbExists = QFile::exists(mdbPath);
	if (mdbExists)
	{
		mdb = MdbParser::load(mdbPath, &mdbOk);
		if (mdbOk)
			bufLog(QtInfoMsg, QStringLiteral("mdb"),
				   QStringLiteral("  MDB: %1 clips, %2 files in /%3")
					   .arg(mdb.masters.size())
					   .arg(mdb.files.size())
					   .arg(task.folderNumber));
		else
			bufLog(QtWarningMsg, QStringLiteral("mdb"),
				   QStringLiteral("  msmMMOB.mdb in /%1 is unreadable; unmatched files here "
								  "surface as 'No database', not 'No reference'")
					   .arg(task.folderNumber));
	}
	else
	{
		bufLog(QtInfoMsg, QStringLiteral("mdb"), QStringLiteral("  No msmMMOB.mdb in /%1").arg(task.folderNumber));
	}

	// MARK: Folder database status

	// What the databases can vouch for. An absent database can't contain any
	// file (vacuously checkable), but an unreadable one could contain
	// anything, so nothing unmatched in this folder can be verified. Both
	// have to be absent before "never indexed" applies.
	MediaFile::DbIssue folderDbIssue = MediaFile::DbIssue::None;
	if ((pmrExists && !pmrOk) || (mdbExists && !mdbOk))
		folderDbIssue = MediaFile::DbIssue::Unreadable;
	else if (!pmrExists && !mdbExists)
		folderDbIssue = MediaFile::DbIssue::NeverIndexed;

	// MARK: Enumerate files in this folder

	QFileInfoList entries;
	QDir folder(task.folderPath);

	// Avid's own name for the folder it moves unreadable media into. Decided
	// once here; every row from this folder is stamped isQuarantined below,
	// and the table's Quarantined filter reads that flag.
	const bool isQuarantineFolder =
		task.folderNumber.compare("Quarantined Files", Qt::CaseInsensitive) == 0;

	if (isQuarantineFolder)
	{
		QDirIterator it(task.folderPath, QDir::Files | QDir::NoDotAndDotDot,
						QDirIterator::Subdirectories);
		int mxfCount = 0;
		while (it.hasNext())
		{
			it.next();
			const QFileInfo fi = it.fileInfo();
			if (AvidLayout::countsAsEssenceName(fi.fileName()))
				++mxfCount;
			entries.append(fi);
		}

		if (entries.isEmpty())
			bufLog(QtInfoMsg, QStringLiteral("scanner"),
				   QStringLiteral("  Quarantined Files folder on %1 is empty").arg(task.volumeName));
		else if (mxfCount > 0)
			bufLog(QtWarningMsg, QStringLiteral("scanner"),
				   QStringLiteral("⚠️ Avid Quarantined Files folder on %1 contains %2 MXF file(s)!")
					   .arg(task.volumeName)
					   .arg(mxfCount));
		else
			bufLog(QtInfoMsg, QStringLiteral("scanner"),
				   QStringLiteral("  Quarantined Files folder on %1 contains %2 non-MXF file(s)")
					   .arg(task.volumeName)
					   .arg(entries.size()));
	}
	else
	{
		// Normal folders are flat; no recursion beneath `<n>/`.
		entries = folder.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
	}

	// MARK: Build a MediaFile for each entry

	CoverageTally tally;
	for (const QFileInfo &entry : entries)
	{
		if (m_job.isCancelled())
			break;

		QString fileName = entry.fileName();
		// Only Avid media appears in the table: .mxf/.omf plus the OMF
		// era's .aif/.wav audio. Everything else — the msm databases, OS
		// junk, stray exports, AppleDouble "._clip.mxf" twins — is
		// invisible to the table, the counts, and every media operation.
		if (!AvidLayout::isAvidMediaName(fileName))
			continue;

		MediaFile mf = buildMediaFile(entry, task.volumeName, task.volumePath, task.folderNumber, pmrMaps, mdb,
									  folderDbIssue, tally);
		mf.isQuarantined = isQuarantineFolder;

		result.files.append(mf);
	}

	// What the databases did for this folder. The header count is the work
	// pass 2 inherits; the stale count is files whose bytes changed since
	// Avid indexed them (sent to pass 2 rather than trusted).
	if (tally.covered + tally.header > 0)
	{
		QString line = QStringLiteral("  /%1: %2 media file(s), %3 described by the databases, %4 need a header read")
						   .arg(task.folderNumber)
						   .arg(result.files.size())
						   .arg(tally.covered)
						   .arg(tally.header);
		if (tally.stale > 0)
			line += QStringLiteral(" (%1 changed since Avid indexed them)").arg(tally.stale);
		bufLog(QtInfoMsg, QStringLiteral("scanner"), line);
	}

	if (result.files.size() > AvidLimits::kFolderWarn)
	{
		// Don't warn per-folder; N pool threads firing would bury
		// the progress logs. Stash the (folder, count) and let
		// doScan emit one summary at the end.
		QMutexLocker lock(&m_overfullMutex);
		m_overfullFolders.append(
			{task.volumeName + QLatin1Char('/') + task.folderNumber, int(result.files.size())});
	}

	// Cache the clip records for Stage 3 (UMID recovery) — only the masters;
	// the per-file essence is consumed above and dropped. Move because this
	// task is done with it. Skip empties as nothing to join.
	// PathKey::normalise keeps Stages 1 and 3 agreeing on folder
	// identity across different Qt path APIs.
	if (!mdb.masters.isEmpty())
	{
		QMutexLocker lock(&m_mdbMapsMutex);
		m_mdbMapsByFolder.insert(PathKey::normalise(task.folderPath), std::move(mdb.masters));
	}

	return result;
}

// MARK: - MediaFile assembly (Stage 1)

MediaFile MediaScanner::buildMediaFile(const QFileInfo &fi, const QString &volumeName,
									   const QString &volumePath, const QString &folderNumber,
									   const PmrParser::ProjectMaps &pmrMaps,
									   const MdbDatabase &mdb,
									   MediaFile::DbIssue folderDbIssue, CoverageTally &tally)
{
	// `fi` is the directory listing's own entry — its size and times are
	// already known, so nothing here stats the file again.
	MediaFile mf;
	mf.filePath = fi.filePath();
	mf.fileName = fi.fileName();
	mf.extension = "." + fi.suffix().toLower();
	mf.volumeName = volumeName;
	mf.volumePath = volumePath;
	mf.mxfFolder = folderNumber;

	// MARK: File-level metadata

	mf.sizeBytes = fi.size();
	// birthTime() is invalid on filesystems that don't record creation
	// (some network shares, ext4). The column shows blank there — an
	// unknown must never be silently coerced to a different fact (the
	// modified-time fallback used to do exactly that).
	mf.created = fi.birthTime();
	mf.modified = fi.lastModified();
	// No clip-name seed: the filename is not a name Avid gave the clip, and
	// seeding it here meant an unknown arrived at the table looking like an
	// answer. The name is filled in by setClipName from the MDB (below) or
	// the MXF header (Stage 2), and stays empty when neither knows.
	mf.isNonPortable = isNonPortableFilename(mf.fileName);

	// MARK: PMR lookup (primary key + fallback)

	const QString primaryKey = PmrKey::primary(mf.fileName);

	const PmrEntry *pmrHit = nullptr;
	auto applyPmrHit = [&mf, &pmrHit](const PmrEntry &pmr)
	{
		pmrHit = &pmr;
		mf.project = pmr.project;
		mf.mobId = pmr.mobId;
		mf.masterMobId = pmr.masterMobId;
	};

	auto pmrIt = pmrMaps.primary.find(primaryKey);
	if (pmrIt != pmrMaps.primary.end() && !pmrIt->isEmpty())
	{
		applyPmrHit(pmrIt->first());
	}
	else
	{
		// Fallback key: last extension stripped, remaining dots turned to
		// underscores (see PmrKey::fallback). Bridges the on-disk name to the
		// PMR's dotted-vs-undotted spelling of the same clip.
		pmrIt = pmrMaps.fallback.find(PmrKey::fallback(primaryKey));
		if (pmrIt != pmrMaps.fallback.end() && !pmrIt->isEmpty())
			applyPmrHit(pmrIt->first());
	}

	// MARK: MDB lookup (the master clip's record)

	// The clip-level facts — name, bin, source, import flag — live on the
	// MASTER mob, the one the PMR's MASTER record names. The file mob's own
	// record is not consulted: its CPNT:Name is usually the source filename,
	// and reading it first used to put "Avid DNx SQ.mov" in the Clip Name
	// column on 67 of 795 corpus rows whenever the header went unread.
	// applyMdbRecord is the file-scope helper above; Stage 1 and Stage 3 both
	// call it so the merge rules can't drift.
	if (!mf.masterMobId.isEmpty())
	{
		auto mdbIt = mdb.masters.find(mf.masterMobId);
		if (mdbIt != mdb.masters.end())
			applyMdbRecord(mf, mdbIt.value());
	}

	// MARK: Technical facts from the database — or leave them for pass 2

	// The database may describe this very file: the PMR names it, the MDB
	// holds its file mob with a complete essence record AND its master mob,
	// and the file on disk is still the one Avid indexed — its modified time
	// matches the PMR's trailer in either of the two spellings Avid has used
	// (PmrParser::trailerMatchesModified). Then the row takes every
	// technical fact from the database and the file is never opened.
	// Anything less — no PMR entry, an incomplete record (MPEG audio has no
	// codec label in the MDB), a file changed since it was indexed, a
	// database that didn't read, or Debug ▸ Force header scan — leaves the
	// row for the header pass, which is exactly the pre-database-first path.
	// A row the database covered has a codec (finalise always names one);
	// pass 2 picks up the .mxf rows that don't.
	const bool isMxf = AvidLayout::hasMxfExtension(mf.extension) && mf.sizeBytes > 1024;
	if (isMxf && pmrHit && !m_options.forceHeaderScan && !mdb.isEmpty())
	{
		const auto fileIt = mdb.files.constFind(mf.mobId);
		const auto masterIt = mdb.masters.constFind(mf.masterMobId);
		const bool described = fileIt != mdb.files.constEnd() && fileIt->essenceComplete &&
							   masterIt != mdb.masters.constEnd();
		bool current = described;
		if (described && pmrHit->fileModifiedSecs != 0)
		{
			current = PmrParser::trailerMatchesModified(pmrHit->fileModifiedSecs, fi.lastModified());
			if (!current)
				++tally.stale;
		}
		if (current)
		{
			MxfMetadata essence = fileIt->essence;
			// The master mob's usage code is the verdict, as the MaterialPackage's
			// is in the header: 1 = precompute. (The file mob usually says 9 too,
			// but real folders hold renders whose file mob says 0 — 54 on one
			// drive — so the file code is not required.)
			essence.isPrecompute = masterIt->usageCode == 1;
			applyMetadata(mf, essence);
			++tally.covered;
		}
		else
		{
			++tally.header;
		}
	}
	else if (isMxf)
	{
		++tally.header;
	}

	// An all-zero MOB ID means Avid never wrote a real identity for the file
	// or its clip; the media can't be tracked or relinked reliably.
	mf.isInvalidUmid = MobId::isAllZero(mf.mobId) || MobId::isAllZero(mf.masterMobId);

	// MARK: Local-database classification

	// No PMR project means the local databases never attributed this file.
	// Claim a verified miss ("No reference") only when every database that
	// exists in the folder parsed cleanly; otherwise the honest answer is
	// "couldn't check" ("No database"), with the reason kept for the UI.
	if (mf.project.isEmpty())
	{
		if (folderDbIssue == MediaFile::DbIssue::None)
			mf.markNoReference();
		else
			mf.markNoDatabase(folderDbIssue);
	}

	// A MOB ID plus an MDB in the folder means the file IS indexed — the
	// editor probably deleted the project that owned the essence. 'No
	// project' outranks both miss states (the setter encodes that rule).
	if (!mf.mobId.isEmpty() && (mf.isNoReference || mf.isNoDatabase()) && !mdb.isEmpty())
		mf.markNoProject();

	return mf;
}

// MARK: - Header pass (pass 2)

void MediaScanner::parseMxfHeadersConcurrently(QVector<MediaFile> &files)
{
	// The rows the databases did not describe: an .mxf worth opening that
	// still has no codec (finalise always names one for essence the MDB
	// vouched for; a row without a database, or sent here by the staleness
	// guard or the Debug toggle, has none yet). Everything else —
	// .wav/.aif/.omf, sub-1 KB stubs, every database-covered row — is left
	// alone. Each row carries its folder's key so the re-join below can
	// find that folder's clip records; PathKey::normalise is a filesystem
	// round-trip, so it runs once per distinct folder, not once per file.
	struct HeaderRow
	{
		int index;
		QString folderKey;
	};
	QVector<HeaderRow> rows;
	rows.reserve(files.size() / 4);
	QHash<QString, QString> folderKeyCache;
	for (int i = 0; i < files.size(); ++i)
	{
		const MediaFile &f = files[i];
		if (!AvidLayout::hasMxfExtension(f.extension) || f.sizeBytes <= 1024 || !f.codec.isEmpty())
			continue;
		const QString rawFolder = QFileInfo(f.filePath).absolutePath();
		auto cacheIt = folderKeyCache.find(rawFolder);
		if (cacheIt == folderKeyCache.end())
			cacheIt = folderKeyCache.insert(rawFolder, PathKey::normalise(rawFolder));
		rows.append({i, cacheIt.value()});
	}
	if (rows.isEmpty())
		return;

	const int total = rows.size();
	emitLog(QtInfoMsg, QStringLiteral("scanner"),
			QStringLiteral("Reading MXF headers for %1 file(s) the databases don't describe").arg(total));
	emit scanProgress(0, total, {});

	std::atomic<int> done{0};
	std::atomic<int> recovered{0};
	std::atomic<qint64> totalBytesRead{0};
	std::atomic<qint64> maxBytesRead{0};
	ProgressThrottle throttle;

	// Pass 1 has joined, so nobody writes the cache any more: plain
	// concurrent reads below, no lock.
	const QHash<QString, QHash<QString, MdbMaster>> &clipsByFolder = m_mdbMapsByFolder;

	// Detach once before workers touch the vector. QVector::data()
	// fires the CoW detach if shared; pool threads then write to
	// disjoint indices via a raw pointer with no detach race.
	MediaFile *const base = files.data();

	QtConcurrent::blockingMap(
		rows,
		[&, base](const HeaderRow &row)
		{
			if (m_job.isCancelled())
				return;
			MediaFile &mf = base[row.index];
			qint64 bytesRead = 0;
			const MxfMetadata mxf = MxfParser::parseHeader(mf.filePath, &bytesRead);
			applyMetadata(mf, mxf);

			// Re-join by the header's own UMID (its MaterialPackage UID = the
			// master MOB in MXF byte order): a file the PMR doesn't name but
			// the MDB still knows — the Interplay shape, or a PMR that was
			// corrupt while the MDB read fine. Recovers name/bin/source and
			// adopts the MOB ids so the bin filter and Select Relatives see
			// the row like a PMR hit. Media vs Precompute is NOT touched: the
			// header just said.
			const bool unattributed = mf.isNoReference || mf.isNoDatabase();
			if (unattributed && !mxf.umid.isEmpty() && !MobId::isAllZero(mxf.umid))
			{
				const auto mapIt = clipsByFolder.constFind(row.folderKey);
				if (mapIt != clipsByFolder.constEnd() && !mapIt->isEmpty())
				{
					// Direct match is rare: the MXF stores the middle fields
					// little-endian, the MDB big-endian. Try direct (free),
					// then swapped.
					auto recIt = mapIt->constFind(mxf.umid);
					if (recIt == mapIt->constEnd())
					{
						const QString swapped = MobId::toPmrForm(mxf.umid);
						if (!swapped.isEmpty())
							recIt = mapIt->constFind(swapped);
					}
					if (recIt != mapIt->constEnd())
					{
						applyMdbRecord(mf, recIt.value());
						if (!recIt->mobIdHex.isEmpty())
						{
							mf.mobId = recIt->mobIdHex;
							mf.masterMobId = recIt->mobIdHex;
						}
						mf.markNoProject(); // in the MDB, but no project's PMR names it
						++recovered;
					}
				}
			}
			// The header's own identity can be the zero one too.
			mf.isInvalidUmid = MobId::isAllZero(mf.mobId) || MobId::isAllZero(mf.masterMobId) ||
							   (!mxf.umid.isEmpty() && MobId::isAllZero(mxf.umid));

			totalBytesRead.fetch_add(bytesRead, std::memory_order_relaxed);

			// Lock-free max via CAS loop. Every pool thread fights for
			// the same atomic, so retry until we win or someone else
			// sets a bigger value.
			qint64 prev = maxBytesRead.load(std::memory_order_relaxed);
			while (bytesRead > prev &&
				   !maxBytesRead.compare_exchange_weak(prev, bytesRead, std::memory_order_relaxed))
			{
			}

			const int n = ++done;
			if (n == total || throttle.shouldEmit())
				emit scanProgress(n, total, mf.fileName);
		});

	// MARK: Pass 2 summary log

	const qint64 totalBytes = totalBytesRead.load();
	const qint64 maxBytes = maxBytesRead.load();
	const qint64 avgKB = total > 0 ? (totalBytes / total) / 1024 : 0;
	emitLog(QtInfoMsg, QStringLiteral("mxf"),
			QStringLiteral("MXF parse: %1 files, avg %2 KB/file, max %3 KB, total %4 MB read")
				.arg(total)
				.arg(avgKB)
				.arg(maxBytes / 1024)
				.arg(totalBytes / (1024 * 1024)));
	if (recovered > 0)
		emitLog(QtInfoMsg, QStringLiteral("mdb"),
				QStringLiteral("Recovered %1 file(s) via MDB / UMID lookup").arg(recovered.load()));
}

// MARK: - Portable filename test

bool MediaScanner::isNonPortableFilename(const QString &name)
{
	// Allowed set: A-Z a-z 0-9 . _ - space , ( ) [ ] + = ' ~ @ # % &
	static const auto kPortableChars = []
	{
		std::array<bool, 128> t{};
		for (char c : "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
					  "abcdefghijklmnopqrstuvwxyz"
					  "0123456789"
					  "._- ,()[]+='~@#%&")
		{
			if (c != '\0')
				t[static_cast<unsigned char>(c)] = true;
		}
		return t;
	}();

	for (QChar ch : name)
	{
		const ushort u = ch.unicode();
		if (u >= 128 || !kPortableChars[u])
			return true;
	}
	return false;
}