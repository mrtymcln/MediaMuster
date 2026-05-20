#include "mediascanner.h"
#include "debugslowdown.h"
#include "mobid.h"
#include "pmrkey.h"
#include "progressthrottle.h"
#include "workerthread.h"
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <array>

#ifdef Q_OS_MAC
#include <unistd.h>
#endif

// MARK: - Junk-file set

// Meyers singleton — sidesteps file-scope static init order surprises.
static const QSet<QString> &junkFiles()
{
	static const QSet<QString> s = {
	    ".ds_store", "thumbs.db", ".spotlight-v100", ".fseventsd",
	    ".trashes", "desktop.ini", "._.ds_store"};
	return s;
}

// MARK: - MediaScanner construction / teardown

MediaScanner::MediaScanner(QObject *parent)
    : QObject(parent)
{
}

MediaScanner::~MediaScanner()
{
	cancelScan();

	// Cooperative cancel polls at folder/file boundaries —
	// sub-second normally; only approaches the cap if needed.
	WorkerThread::joinOrTerminate(m_thread, WorkerThread::kWorkerShutdownTimeoutMs);

	// Thread self-deletes on QThread::finished (see startScan).
}

// MARK: - Scan lifecycle

void MediaScanner::startScan(const Options &options)
{
	// CAS against rapid double-clicks on Scan.
	bool expected = false;
	if (!m_running.compare_exchange_strong(expected, true))
		return;

	m_thread = new QThread(this);
	auto *worker = new ScannerWorker(options);
	m_worker = worker;
	worker->moveToThread(m_thread);

	connect(worker, &ScannerWorker::progress, this,
	        &MediaScanner::scanProgress, Qt::QueuedConnection);
	connect(worker, &ScannerWorker::logBatch, this,
	        &MediaScanner::scanLogBatch, Qt::QueuedConnection);
	connect(worker, &ScannerWorker::finished, this, [this](const QVector<MediaFile> &results)
	        {
				m_running.store(false);
				emit scanFinished(results); }, Qt::QueuedConnection);

	connect(m_thread, &QThread::started, worker, &ScannerWorker::process);
	connect(worker, &ScannerWorker::finished, m_thread, &QThread::quit);
	connect(worker, &ScannerWorker::finished, worker, &ScannerWorker::deleteLater);
	connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);
	connect(m_thread, &QThread::finished, this, [this]
	        { m_thread = nullptr; });

	m_thread->start();
}

void MediaScanner::cancelScan()
{
	if (auto *w = m_worker.data())
		w->cancel();
}

// MARK: - ScannerWorker construction

ScannerWorker::ScannerWorker(const MediaScanner::Options &options)
    : m_options(options)
{
}

void ScannerWorker::cancel()
{
	// `release` pairs with `acquire` on the poll sites so the flag
	// becomes visible promptly on ARM as well as x86.
	m_cancel.store(true, std::memory_order_release);
}

void ScannerWorker::process()
{
	// Reset the flush clock at scan-begin.
	m_flushTimer.start();
	m_lastFlushElapsed = 0;
	doScan();
}

// MARK: - Per-MediaFile derivations

namespace
{
constexpr int kLogBatchMaxSize = 50;
constexpr qint64 kLogBatchMaxAgeMs = 100;

// Called from Stage 1 (buildMediaFile) and Stage 2 (after MXF
// merge). Re-derive — either pass can update the inputs.
void recomputeBitrate(MediaFile &mf)
{
	mf.bitRateString.clear();

	if (mf.mediaType == MediaFile::Type::Audio)
	{
		if (mf.sampleRate > 0)
			mf.bitRateString = QString::number(mf.sampleRate / 1000) + " kHz";
		return;
	}

	const float fpsNum = mf.fps.toFloat();
	if (fpsNum <= 0 || mf.durationFrames <= 0 || mf.sizeBytes <= 0)
		return;
	const float seconds = mf.durationFrames / fpsNum;
	if (seconds <= 0)
		return;

	int mbps = qRound((mf.sizeBytes * 8.0) / (seconds * 1000000.0));

	// Snap to nearest common DNxHD tier — ordered hot-first so the
	// typical case hits early and breaks out.
	static constexpr std::array<int, 12> kStandards{
	    36, 145, 220, 115, 120, 175, 185, 45, 50, 100, 350, 440};
	for (int s : kStandards)
	{
		if (qAbs(mbps - s) <= 10)
		{
			mbps = s;
			break;
		}
	}
	mf.bitRateString = QString::number(mbps);
}

void applyMxfMetadata(MediaFile &mf, const MxfMetadata &mxf)
{
	if (mxf.valid)
	{
		const int dot = mf.fileName.lastIndexOf(QLatin1Char('.'));
		const QString fileBase = (dot > 0) ? mf.fileName.left(dot) : mf.fileName;

		if (!mxf.umid.isEmpty())
			mf.umid = mxf.umid;
		// Overwrite clipName from MXF only if Stage 1 left the bare
		// filename — MDB/PMR values take precedence.
		if (mf.clipName == fileBase && !mxf.clipName.isEmpty())
			mf.clipName = mxf.clipName;
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
		if (mxf.isAudio || mf.codec == "PCM Audio")
			mf.mediaType = MediaFile::Type::Audio;
	}

	recomputeBitrate(mf);

	// All-zero UMID = Avid never wrote a real material number;
	// the file risks vanishing on the next consolidate.
	if (!mf.umid.isEmpty())
		mf.isBadUmid = MobId::isAllZero(mf.umid);
}
} // namespace

// MARK: - Log buffering

void ScannerWorker::emitLog(int level, const QString &module,
                            const QString &msg)
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

void ScannerWorker::flushLogs()
{
	// Swap-and-emit — mutex isn't held across queued signal.
	QVector<LogMsg> batch;
	{
		QMutexLocker lock(&m_logMutex);
		if (m_pendingLogs.isEmpty())
			return;
		batch.swap(m_pendingLogs);
	}
	emit logBatch(batch);
}

// MARK: - Path readability

bool ScannerWorker::canReadPath(const QString &path)
{
#ifdef Q_OS_MAC
	// access(2) R_OK avoids the full stat QFileInfo::isReadable pays for.
	return access(QFile::encodeName(path).constData(), R_OK) == 0;
#else
	return QFileInfo(path).isReadable();
#endif
}

// MARK: - Scan orchestration

void ScannerWorker::doScan()
{
	QVector<MediaFile> allFiles;

	emitLog(0, "scanner",
	        QString("Scanning %1 location(s)...")
	            .arg(m_options.volumePaths.size()));

	// MARK: Stage 1 — per-volume folder walk

	for (const QString &volumePath : m_options.volumePaths)
	{
		if (m_cancel.load(std::memory_order_acquire))
			break;

		QDir volumeDir(volumePath);
		QString volumeName = volumeDir.dirName();
		if (volumeName.isEmpty())
			volumeName = volumePath;

		if (!canReadPath(volumePath))
		{
			emitLog(2, "scanner",
			        QString("Permission denied: %1").arg(volumePath));
			emitLog(1, "scanner",
			        "Grant Full Disk Access in System Preferences > Privacy & "
			        "Security");
			continue;
		}

		emitLog(0, "scanner",
		        QString("Scanning: %1 (%2)").arg(volumeName, volumePath));
		emit progress("Scanning", 0, 0, volumePath);

		auto volumeFiles = scanVolume(volumePath, volumeName);
		allFiles.append(volumeFiles);

		if (!volumeFiles.isEmpty())
		{
			emitLog(3, "scanner",
			        QString("  %1: %2 media files found")
			            .arg(volumeName)
			            .arg(volumeFiles.size()));
		}
	}

	if (m_cancel.load(std::memory_order_acquire))
	{
		emitLog(1, "scanner", "Scan cancelled by user");
		emit finished(allFiles);
		return;
	}

	// MARK: Stage 2 — concurrent MXF header parse

	parseMxfHeadersConcurrently(allFiles);

	if (m_cancel.load(std::memory_order_acquire))
	{
		emitLog(1, "scanner", "Scan cancelled by user");
		emit finished(allFiles);
		return;
	}

	// MARK: Stage 3 — summary tally + cleanup

	int unmanaged = 0, badUmid = 0, unreferenced = 0, nonPortable = 0;
	for (const auto &f : allFiles)
	{
		if (f.isUnmanaged)
			++unmanaged;
		if (f.isBadUmid)
			++badUmid;
		if (f.isUnreferenced)
			++unreferenced;
		if (f.isNonPortable)
			++nonPortable;
	}

	if (allFiles.isEmpty())
	{
		emitLog(1, "scanner", "No media files found.");
		emitLog(
		    0, "scanner",
		    "Make sure the selected volumes contain Avid MediaFiles folders.");
	}
	else
	{
		emitLog(3, "scanner",
		        QString("Scan complete: %1 files found")
		            .arg(allFiles.size()));
	}

	if (unmanaged > 0)
		emitLog(1, "scanner", QString("%1 unmanaged files").arg(unmanaged));
	if (badUmid > 0)
		emitLog(1, "scanner", QString("%1 files with bad UMID").arg(badUmid));
	if (unreferenced > 0)
		emitLog(1, "scanner",
		        QString("%1 unreferenced files").arg(unreferenced));
	if (nonPortable > 0)
		emitLog(1, "scanner",
		        QString("%1 non-portable filenames").arg(nonPortable));

	// MARK: Aggregate over-cap folder summary

	{
		QMutexLocker lock(&m_overfullMutex);
		if (!m_overfullFolders.isEmpty())
		{
			QString msg = QString("%1 folder(s) over 4000 files "
			                      "(Avid recommends staying under 5000):")
			                  .arg(m_overfullFolders.size());
			for (const auto &p : m_overfullFolders)
				msg += QString("\n  %1 — %2 files")
				           .arg(p.first)
				           .arg(p.second);
			emitLog(1, "scanner", msg);
		}
		m_overfullFolders.clear();
	}

	// Drain buffered logs before scanFinished so the last batch
	// doesn't appear out of order.
	flushLogs();
	emit finished(allFiles);
}

// MARK: - Per-volume: locate Avid MediaFiles roots

QVector<MediaFile> ScannerWorker::scanVolume(const QString &volumePath,
                                             const QString &volumeName)
{
	QVector<MediaFile> files;
	QDir dir(volumePath);
	const QString dirName = dir.dirName();

	// MARK: Case 1 — Volume root contains Avid MediaFiles/MXF

	const QString mxfViaRoot = volumePath + "/Avid MediaFiles/MXF";
	if (QDir(mxfViaRoot).exists())
	{
		emitLog(0, "scanner", QStringLiteral("  Found Avid MediaFiles/MXF"));
		return scanMxfRoot(mxfViaRoot, volumeName, volumePath);
	}

	// MARK: Case 2 — Path is somewhere inside an Avid MediaFiles directory

	constexpr int kAvidLen = sizeof("Avid MediaFiles") - 1;
	const int avidIdx = volumePath.indexOf("Avid MediaFiles", 0, Qt::CaseInsensitive);
	if (avidIdx >= 0)
	{
		const QString avidPart = volumePath.left(avidIdx + kAvidLen);
		const QString mxfInside = avidPart + "/MXF";
		if (QDir(mxfInside).exists())
		{
			emitLog(0, "scanner", QString("  Found MXF folder at %1").arg(mxfInside));
			return scanMxfRoot(mxfInside, volumeName, avidPart);
		}

		if (volumePath.endsWith("/MXF", Qt::CaseInsensitive))
		{
			emitLog(0, "scanner", QStringLiteral("  Pointed directly at MXF folder"));
			return scanMxfRoot(volumePath, volumeName, QFileInfo(volumePath).absolutePath());
		}
	}

	// MARK: Case 3 — Path itself is an MXF or OMF root

	if (dirName.compare("MXF", Qt::CaseInsensitive) == 0 ||
	    dirName.compare("OMF", Qt::CaseInsensitive) == 0)
	{
		const QStringList subs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
		if (!subs.isEmpty())
		{
			emitLog(0, "scanner",
			        QString("  MXF folder with %1 subfolders").arg(subs.size()));
			return scanMxfRoot(volumePath, volumeName, QFileInfo(volumePath).absolutePath());
		}
	}

	// MARK: Case 4 — Single media folder with per-folder databases

	if (QFile::exists(volumePath + "/msmMMOB.mdb") ||
	    QFile::exists(volumePath + "/msmFMID.pmr"))
	{
		emitLog(0, "scanner", QStringLiteral("  Found database files in folder"));

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

	// Two levels of subdir catches the common
	// `~/Documents/Project/Avid MediaFiles` layout without
	// scanning the whole volume.
	emitLog(0, "scanner",
	        QString("  Searching for Avid MediaFiles in %1...").arg(volumeName));

	QStringList searchDirs = {volumePath};
	for (const QString &sub1 :
	     dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
	{
		if (m_cancel.load(std::memory_order_acquire))
			break;
		QString path1 = volumePath + "/" + sub1;
		if (!canReadPath(path1))
			continue;
		searchDirs.append(path1);

		QDir d1(path1);
		for (const QString &sub2 :
		     d1.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
		{
			QString path2 = path1 + "/" + sub2;
			if (!canReadPath(path2))
				continue;
			searchDirs.append(path2);
		}
	}

	for (const QString &searchDir : searchDirs)
	{
		if (m_cancel.load(std::memory_order_acquire))
			break;
		QString candidate = searchDir + "/Avid MediaFiles/MXF";
		if (QDir(candidate).exists())
		{
			emitLog(0, "scanner",
			        QString("  Found Avid media at %1").arg(candidate));
			auto subFiles = scanMxfRoot(candidate, volumeName, searchDir);
			files.append(subFiles);
		}
	}

	if (files.isEmpty())
	{
		emitLog(1, "scanner",
		        QString("  No Avid MediaFiles found in %1").arg(volumeName));
	}

	return files;
}

// MARK: - MXF root: parallel per-folder scan

QVector<MediaFile> ScannerWorker::scanMxfRoot(const QString &mxfRootPath,
                                              const QString &volumeName,
                                              const QString &volumePath)
{
	QVector<MediaFile> files;
	QDir mxfDir(mxfRootPath);

	if (!canReadPath(mxfRootPath))
	{
		emitLog(2, "scanner",
		        QString("  Permission denied: %1").arg(mxfRootPath));
		return files;
	}

	QStringList subFolders =
	    mxfDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

	QList<ScanTask> tasks;
	for (const QString &folder : subFolders)
	{
		if (m_cancel.load(std::memory_order_acquire))
			break;

		QString folderPath = mxfDir.filePath(folder);

		if (!canReadPath(folderPath))
		{
			emitLog(1, "scanner",
			        QString("  Permission denied: %1").arg(folder));
			continue;
		}

		ScanTask t;
		t.folderPath = folderPath;
		t.folderNumber = folder;
		t.volumeName = volumeName;
		t.volumePath = volumePath;
		tasks.append(t);
	}

	emitLog(0, "scanner",
	        QString("  %1 subfolders queued for concurrent scanning")
	            .arg(tasks.size()));

	std::atomic<int> completedFolders{0};
	const int totalFolders = tasks.size();

	// ~30 Hz emit cap when folders finish quickly.
	ProgressThrottle throttle;

	// QtConcurrent::mapped preserves input order — we replay
	// buffered logs in scan order.
	QFuture<FolderResult> future = QtConcurrent::mapped(
	    tasks, [this, &completedFolders, totalFolders, &throttle](const ScanTask &t)
	    {
            auto res = this->processFolderTask(t);
            int done = ++completedFolders;

            // No-op when slow mode is off.
            DebugSlowdown::pauseForMs(80);

            // Always emit on the last folder so the bar hits 100%;
            // gate everything else.
            if (done == totalFolders || throttle.shouldEmit()) {
                emit progress(QString("Scanning %1").arg(t.volumeName), done,
                              totalFolders, t.folderPath);
            }
            return res; });

	future.waitForFinished();

	// Drain per-task logs in input order — results() returns by
	// index regardless of pool thread finish order.
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

FolderResult ScannerWorker::processFolderTask(const ScanTask &task)
{
	FolderResult result;

	if (m_cancel.load(std::memory_order_acquire))
		return result;

	// Pool threads can't safely emit signals via direct connection,
	// so buffer logs in the result; the main worker thread replays
	// them via emitLog in input order.
	auto bufLog = [&result](int level, const QString &module, const QString &msg)
	{
		result.logs.append({level, module, msg});
	};

	// MARK: Parse the PMR

	// Missing PMR/MDB is normal in Interplay setups, so log as info
	// rather than warning.
	PmrParser::ProjectMaps pmrMaps;
	const QString pmrPath = task.folderPath + "/msmFMID.pmr";
	if (QFile::exists(pmrPath))
	{
		pmrMaps = PmrParser::buildFileMapWithFallback(pmrPath);
		bufLog(0, "pmr",
		       QString("  PMR: %1 file entries in /%2")
		           .arg(pmrMaps.primary.size())
		           .arg(task.folderNumber));
	}
	else
	{
		bufLog(0, "pmr", QString("  No msmFMID.pmr in /%1").arg(task.folderNumber));
	}

	// MARK: Parse the MDB

	MdbParser::RecordMap mdbMap;
	const QString mdbPath = task.folderPath + "/msmMMOB.mdb";
	if (QFile::exists(mdbPath))
	{
		mdbMap = MdbParser::buildMobMap(mdbPath);
		bufLog(0, "mdb",
		       QString("  MDB: %1 records in /%2")
		           .arg(mdbMap.size())
		           .arg(task.folderNumber));
	}
	else
	{
		bufLog(0, "mdb", QString("  No msmMMOB.mdb in /%1").arg(task.folderNumber));
	}

	// MARK: Enumerate files in this folder

	QFileInfoList entries;
	QDir folder(task.folderPath);

	if (task.folderNumber.compare("Quarantined Files", Qt::CaseInsensitive) == 0)
	{
		QDirIterator it(task.folderPath, QDir::Files | QDir::NoDotAndDotDot,
		                QDirIterator::Subdirectories);
		int mxfCount = 0;
		while (it.hasNext())
		{
			it.next();
			const QFileInfo fi = it.fileInfo();
			if (fi.suffix().compare("mxf", Qt::CaseInsensitive) == 0)
				++mxfCount;
			entries.append(fi);
		}

		if (entries.isEmpty())
			bufLog(0, "scanner",
			       QString("  Quarantined Files folder on %1 is empty")
			           .arg(task.volumeName));
		else if (mxfCount > 0)
			bufLog(1, "scanner",
			       QString("⚠️ Avid Quarantined Files folder on %1 contains %2 MXF file(s)!")
			           .arg(task.volumeName)
			           .arg(mxfCount));
		else
			bufLog(0, "scanner",
			       QString("  Quarantined Files folder on %1 contains %2 non-MXF file(s)")
			           .arg(task.volumeName)
			           .arg(entries.size()));
	}
	else
	{
		// Normal folders are flat — no recursion beneath `<n>/`.
		entries = folder.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
	}

	// MARK: Build a MediaFile for each entry

	for (const QFileInfo &entry : entries)
	{
		if (m_cancel.load(std::memory_order_acquire))
			break;

		QString fileName = entry.fileName();
		// PMR and MDB themselves aren't media files.
		if (fileName == "msmMMOB.mdb" || fileName == "msmFMID.pmr")
			continue;

		bool isJunk = junkFiles().contains(fileName.toLower());

		MediaFile mf =
		    buildMediaFile(entry.filePath(), task.volumeName, task.volumePath,
		                   task.folderNumber, pmrMaps, mdbMap);

		if (isJunk)
		{
			// OS housekeeping files (.DS_Store, Thumbs.db, ...) still
			// show up in the table — surfaced as unmanaged so the
			// editor can decide whether to sweep them.
			mf.isDSStore = true;
			mf.project = "UNMANAGED_FILES";
			mf.clipName = fileName;
			mf.isUnmanaged = true;
		}

		result.files.append(mf);
	}

	if (result.files.size() > 4000)
	{
		// Don't warn per-folder — N pool threads would all fire and
		// bury the progress logs. Record the (folder, count) pair
		// and let doScan emit one summary line at end-of-scan.
		QMutexLocker lock(&m_overfullMutex);
		m_overfullFolders.append(
		    {task.volumeName + QLatin1Char('/') + task.folderNumber,
		     int(result.files.size())});
	}

	return result;
}

// MARK: - MediaFile assembly (Stage 1)

MediaFile ScannerWorker::buildMediaFile(
    const QString &filePath, const QString &volumeName, const QString &volumePath,
    const QString &folderNumber, const PmrParser::ProjectMaps &pmrMaps,
    const MdbParser::RecordMap &mdbMap)
{
	MediaFile mf;
	QFileInfo fi(filePath);
	mf.filePath = filePath;
	mf.fileName = fi.fileName();
	mf.extension = "." + fi.suffix().toLower();
	mf.volumeName = volumeName;
	mf.volumePath = volumePath;
	mf.mxfFolder = folderNumber;

	// MARK: Volume display string

	// "Volume/Path/Inside" form. Strip the volume prefix from the
	// containing dir so the UI shows a clean relative path instead
	// of repeating the volume root.
	{
		const QString containingDir = fi.absolutePath();
		QString relInsideVolume = containingDir;
		if (!volumePath.isEmpty() && containingDir.startsWith(volumePath))
		{
			relInsideVolume = containingDir.mid(volumePath.length());
			while (relInsideVolume.startsWith('/') ||
			       relInsideVolume.startsWith('\\'))
				relInsideVolume.remove(0, 1);
		}
		mf.volumeDisplay = relInsideVolume.isEmpty()
		                       ? volumeName
		                       : (volumeName + QStringLiteral("/") +
		                          relInsideVolume);
	}

	// MARK: File-level metadata

	mf.sizeBytes = fi.size();
	mf.sizeMB = fi.size() / (1024.0 * 1024.0);
	mf.modified = fi.lastModified();
	mf.created = fi.birthTime().isValid() ? fi.birthTime() : fi.lastModified();
	mf.clipName = fi.completeBaseName();
	mf.isNonPortable = isNonPortableFilename(mf.fileName);

	// MARK: PMR lookup (primary key + fallback)

	const QString primaryKey = PmrKey::primary(mf.fileName);

	auto applyPmrHit = [&mf](const PmrEntry &pmr)
	{
		mf.project = pmr.project;
		mf.mobId = pmr.mobId;
		mf.compositionMobId = pmr.compositionMobId;
	};

	auto pmrIt = pmrMaps.primary.find(primaryKey);
	if (pmrIt != pmrMaps.primary.end() && !pmrIt->isEmpty())
	{
		applyPmrHit(pmrIt->first());
	}
	else
	{
		// Fallback key strips `.<digits>.mxf` suffixes — handles
		// Avid's `.aaf.1.mxf`, `.aaf.2.mxf` partial-relink names.
		pmrIt = pmrMaps.fallback.find(PmrKey::fallback(primaryKey));
		if (pmrIt != pmrMaps.fallback.end() && !pmrIt->isEmpty())
			applyPmrHit(pmrIt->first());
	}

	// MARK: MDB lookup (file MOB + composition MOB)

	// File-MOB / Composition-MOB carry overlapping fields:
	// clipName and startTimecode live on the composition; bin,
	// source, and isImported live on either. Try both and let the
	// field-by-field isEmpty checks fall in the right order.
	auto applyMdbRecord = [&mf](const MdbRecord &rec)
	{
		if (!rec.clipName.isEmpty())
			mf.clipName = rec.clipName;
		if (!rec.bin.isEmpty() && mf.originalBin.isEmpty())
			mf.originalBin = rec.bin;
		if (!rec.startTimecode.isEmpty() && mf.startTimecode.isEmpty())
			mf.startTimecode = rec.startTimecode;
		if (!rec.sourceFilePath.isEmpty() && mf.sourceFilePath.isEmpty())
			mf.sourceFilePath = rec.sourceFilePath;
		if (!rec.sourceFileName.isEmpty() && mf.sourceFileName.isEmpty())
			mf.sourceFileName = rec.sourceFileName;
		if (!rec.sourceContainer.isEmpty() && mf.sourceContainer.isEmpty())
			mf.sourceContainer = rec.sourceContainer;
		if (rec.isImported)
			mf.isImported = true;
	};

	if (!mf.mobId.isEmpty())
	{
		auto mdbIt = mdbMap.find(mf.mobId);
		if (mdbIt != mdbMap.end())
			applyMdbRecord(mdbIt.value());
	}
	if (!mf.compositionMobId.isEmpty() && mf.compositionMobId != mf.mobId)
	{
		auto mdbIt = mdbMap.find(mf.compositionMobId);
		if (mdbIt != mdbMap.end())
			applyMdbRecord(mdbIt.value());
	}

	// MARK: Defer MXF header parsing to Stage 2

	// Stage 2 parses all MXF headers in parallel via
	// parseMxfHeadersConcurrently.

	// MARK: Audio extension fallback

	if (mf.extension == ".wav" || mf.extension == ".aif" ||
	    mf.extension == ".aiff" || mf.extension == ".bwf")
	{
		mf.mediaType = MediaFile::Type::Audio;
		// Em-dash placeholder so the UI doesn't render "(empty)"
		// in the resolution column for audio.
		if (mf.resolution.isEmpty())
			mf.resolution = QStringLiteral("\xe2\x80\x94");
	}

	mf.kind = "Media";

	// MARK: Precompute detection

	// Filename prefix (`P##.`, `W...##.`) wins first — Avid generates
	// these names for precomputes by convention. Falls through to
	// clip-name keyword matching for older projects where the
	// prefix wasn't applied.
	static const QRegularExpression kPrecompPrefixRe(
	    QStringLiteral("^(?:P\\d+\\.|W[A-Z0-9]+[A-Z]\\d+\\.|WA\\d+\\.)"),
	    QRegularExpression::CaseInsensitiveOption);

	bool isPrecomp = kPrecompPrefixRe.match(mf.fileName).hasMatch();

	if (!isPrecomp)
	{
		static const QStringList kAvidEffectKeywords = {
		    "dissolve", "resize", "title", "color_correction", "color_adapter",
		    "precompute", "frameflex", "color_lut", "color_effect",
		    "timecode_burn-in", "stabilize", "submaster", "motion effect",
		    "flop", "flip"};

		const QString lowerClip = mf.clipName.toLower();
		for (const QString &kw : kAvidEffectKeywords)
		{
			if (lowerClip.contains(kw))
			{
				isPrecomp = true;
				break;
			}
		}

		// Boris Sapphire renders look like "Source,S_Glow,1". The
		// leading comma keeps clips that legitimately start with
		// "S_..." out of the precompute bucket.
		if (!isPrecomp && mf.clipName.contains(",S_"))
			isPrecomp = true;
	}

	if (isPrecomp)
		mf.kind = "Precompute";

	recomputeBitrate(mf);

	// MARK: Unmanaged / unreferenced classification

	// No PMR project → unmanaged. If there's a MOB ID, the file is
	// "unmanaged" (no PMR hit), and the folder has an MDB,
	// reclassify as unreferenced — the editor probably deleted the
	// project that owned this essence.
	if (mf.project.isEmpty())
	{
		mf.isUnmanaged = true;
		mf.project = "UNMANAGED_FILES";
	}
	if (!mf.mobId.isEmpty() && mf.isUnmanaged && !mdbMap.isEmpty())
	{
		mf.isUnreferenced = true;
		mf.isUnmanaged = false;
	}

	return mf;
}

// MARK: - MXF header parsing (Stage 2)

void ScannerWorker::parseMxfHeadersConcurrently(QVector<MediaFile> &files)
{
	if (!m_options.parseMxfHeaders)
		return;

	// Collect indices rather than copying MediaFiles — mutate in
	// place via applyMxfMetadata. Skip < 1 KB (invalid header).
	QVector<int> mxfIndices;
	mxfIndices.reserve(files.size() / 4);
	for (int i = 0; i < files.size(); ++i)
	{
		if (files[i].extension == QLatin1String(".mxf") && files[i].sizeBytes > 1024)
			mxfIndices.append(i);
	}
	if (mxfIndices.isEmpty())
		return;

	const int total = mxfIndices.size();
	emitLog(0, "scanner", QString("Parsing MXF headers for %1 files").arg(total));
	emit progress("Parsing MXF headers", 0, total, {});

	std::atomic<int> done{0};
	std::atomic<qint64> totalBytesRead{0};
	std::atomic<qint64> maxBytesRead{0};
	ProgressThrottle throttle;

	// Detach once before workers touch the vector. QVector::data()
	// triggers the CoW detach if shared; pool threads then write to
	// disjoint indices via a raw pointer without racing on detach.
	MediaFile *const base = files.data();

	QtConcurrent::blockingMap(mxfIndices, [&, base](int idx)
	                          {
		if (m_cancel.load(std::memory_order_acquire))
			return;
		MediaFile &mf = base[idx];
		qint64 bytesRead = 0;
		const MxfMetadata mxf = MxfParser::parseHeader(mf.filePath, &bytesRead);
		applyMxfMetadata(mf, mxf);

		totalBytesRead.fetch_add(bytesRead, std::memory_order_relaxed);

		// Lock-free max via CAS loop. Competing with every other
		// pool thread for the same atomic — retry until we either
		// lose (someone else set a bigger value, our read is
		// stale) or win.
		qint64 prev = maxBytesRead.load(std::memory_order_relaxed);
		while (bytesRead > prev &&
			   !maxBytesRead.compare_exchange_weak(prev, bytesRead,
												   std::memory_order_relaxed))
		{
		}

		const int n = ++done;
		if (n == total || throttle.shouldEmit())
			emit progress("Parsing MXF headers", n, total, mf.fileName); });

	// MARK: Stage 2 summary log

	const qint64 totalBytes = totalBytesRead.load();
	const qint64 maxBytes = maxBytesRead.load();
	const qint64 avgKB = total > 0 ? (totalBytes / total) / 1024 : 0;
	emitLog(0, "mxf",
	        QString("MXF parse: %1 files, avg %2 KB/file, max %3 KB, total %4 MB read")
	            .arg(total)
	            .arg(avgKB)
	            .arg(maxBytes / 1024)
	            .arg(totalBytes / (1024 * 1024)));
}

// MARK: - Portable filename test

bool ScannerWorker::isNonPortableFilename(const QString &name) const
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