#include "mediascanner.h"
#include "debugslowdown.h"
#include "mobid.h"
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMutexLocker>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <array>

#ifdef Q_OS_MAC
#include <unistd.h>
#endif

// O(1) case-insensitive lookup for junk-file filtering across 290K+ files.
// All keys stored lowercase so Windows variants like "Desktop.ini" and
// "thumbs.db" still match.
static const QSet<QString> JUNK_FILES = {
	".ds_store", "thumbs.db", ".spotlight-v100", ".fseventsd",
	".trashes", "desktop.ini", "._.ds_store"};

// Lifecycle: startScan owns a QThread+worker pair; QPointer guards cancelScan
// against a worker already queued for deleteLater; dtor copies m_thread locally
// before waiting so the QThread::finished lambda can't null it mid-wait.
MediaScanner::MediaScanner(QObject *parent) : QObject(parent) {}

MediaScanner::~MediaScanner()
{
	cancelScan();
	// Local copy so QThread::finished can't null m_thread mid-wait.
	if (QThread *t = m_thread)
	{
		t->quit();
		// 5 s grace; terminate rather than leak the thread into the dying process.
		if (!t->wait(5000))
		{
			qWarning("MediaScanner: worker did not quit within 5s; terminating");
			t->terminate();
			t->wait(1000);
		}
		// No delete: thread is `this`'s child, already queued for deleteLater on finished.
	}
}

void MediaScanner::startScan(const Options &options)
{
	// CAS guards against re-entry from rapid double-clicks.
	bool expected = false;
	if (!m_running.compare_exchange_strong(expected, true))
		return;

	m_thread = new QThread(this);
	auto *worker = new ScannerWorker(options);
	m_worker = worker; // QPointer assignment
	worker->moveToThread(m_thread);

	// Cross-thread; explicit QueuedConnection.
	connect(worker, &ScannerWorker::progress, this,
			&MediaScanner::scanProgress, Qt::QueuedConnection);

	// Forward worker's coalesced log batches straight through to the GUI.
	connect(worker, &ScannerWorker::logBatch, this,
			[this](const QVector<LogMsg> &batch)
			{ emit scanLogBatch(batch); }, Qt::QueuedConnection);
	connect(worker, &ScannerWorker::finished, this, [this](const QVector<MediaFile> &results)
			{
                m_running.store(false);
                emit scanFinished(results); }, Qt::QueuedConnection);

	// Thread lifecycle management
	connect(m_thread, &QThread::started, worker, &ScannerWorker::process);
	connect(worker, &ScannerWorker::finished, m_thread, &QThread::quit);
	connect(worker, &ScannerWorker::finished, worker,
			&ScannerWorker::deleteLater);
	connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);
	connect(m_thread, &QThread::finished, this, [this]()
			{
				m_thread = nullptr;
				// m_worker is a QPointer; auto-clears on worker deleteLater.
			});

	m_thread->start();
}

void MediaScanner::cancelScan()
{
	// QPointer; safe even if the worker has been deleted on its thread.
	if (auto *w = m_worker.data())
		w->cancel();
}

ScannerWorker::ScannerWorker(const MediaScanner::Options &options)
	: m_options(options), m_cancel(false) {}

void ScannerWorker::cancel()
{
	m_cancel.store(true, std::memory_order_relaxed);
}

void ScannerWorker::process() { doScan(); }

namespace
{
	// 50 entries or 100 ms, whichever comes first.
	constexpr int kLogBatchMaxSize = 50;
	constexpr qint64 kLogBatchMaxAgeMs = 100;

	// Per-thread flush clock; avoids cross-thread time reads.
	thread_local qint64 t_lastFlushElapsed = 0;
	thread_local QElapsedTimer t_flushTimer;
} // namespace

void ScannerWorker::emitLog(int level, const QString &module,
							const QString &msg)
{
	bool shouldFlush = false;
	{
		QMutexLocker lock(&m_logMutex);
		m_pendingLogs.append({level, module, msg});

		if (!t_flushTimer.isValid())
		{
			t_flushTimer.start();
			t_lastFlushElapsed = 0;
		}

		const qint64 nowMs = t_flushTimer.elapsed();
		shouldFlush = m_pendingLogs.size() >= kLogBatchMaxSize ||
					  (nowMs - t_lastFlushElapsed) >= kLogBatchMaxAgeMs;
		if (shouldFlush)
			t_lastFlushElapsed = nowMs;
	}
	if (shouldFlush)
		flushLogs();
}

void ScannerWorker::flushLogs()
{
	QVector<LogMsg> batch;
	{
		QMutexLocker lock(&m_logMutex);
		if (m_pendingLogs.isEmpty())
			return;
		batch.swap(m_pendingLogs);
	}
	emit logBatch(batch);
}

bool ScannerWorker::canReadPath(const QString &path)
{
#ifdef Q_OS_MAC
	return access(path.toUtf8().constData(), R_OK) == 0;
#else // Q_OS_WIN
	return QFileInfo(path).isReadable();
#endif
}

void ScannerWorker::doScan()
{
	QVector<MediaFile> allFiles;

	emitLog(0, "scanner",
			QString("Scanning %1 location(s)...")
				.arg(static_cast<int>(m_options.drivePaths.size())));

	for (const QString &drivePath : m_options.drivePaths)
	{
		if (m_cancel.load(std::memory_order_relaxed))
			break;

		QDir driveDir(drivePath);
		QString driveName = driveDir.dirName();
		if (driveName.isEmpty())
			driveName = drivePath;

		if (!canReadPath(drivePath))
		{
			emitLog(2, "scanner",
					QString("Permission denied: %1").arg(drivePath));
			emitLog(1, "scanner",
					"Grant Full Disk Access in System Preferences > Privacy & "
					"Security"); // do not change to System Settings.
			continue;
		}

		emitLog(0, "scanner",
				QString("Scanning: %1 (%2)").arg(driveName, drivePath));
		emit progress("Scanning", 0, 0, drivePath);

		auto driveFiles = scanDrive(drivePath, driveName);
		allFiles.append(driveFiles);

		if (!driveFiles.isEmpty())
		{
			emitLog(3, "scanner",
					QString("  %1: %2 media files found")
						.arg(driveName)
						.arg(static_cast<int>(driveFiles.size())));
		}
	}

	if (m_cancel.load(std::memory_order_relaxed))
	{
		emitLog(1, "scanner", "Scan cancelled by user");
		emit finished(allFiles);
		return;
	}

	// Summary
	int unmanaged = 0, badUmid = 0, unreferenced = 0, nonPortable = 0;
	for (const auto &f : allFiles)
	{
		if (f.isUnmanaged)
			unmanaged++;
		if (f.isBadUmid)
			badUmid++;
		if (f.isUnreferenced)
			unreferenced++;
		if (f.isNonPortable)
			nonPortable++;
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
					.arg(static_cast<int>(allFiles.size())));
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

	// Drain any final buffered logs before the GUI is told the scan ended,
	// so the console reflects the complete picture by the time the user
	// looks at it.
	flushLogs();

	emit finished(allFiles);
}

QVector<MediaFile> ScannerWorker::scanDrive(const QString &drivePath,
											const QString &driveName)
{
	QVector<MediaFile> files;
	QDir dir(drivePath);
	QString dirName = dir.dirName();

	// strategy 1: <path>/Avid MediaFiles/MXF exists at the volume root
	QString mxfViaRoot = drivePath + "/Avid MediaFiles/MXF";
	if (QDir(mxfViaRoot).exists())
	{
		emitLog(0, "scanner", QString("  Found Avid MediaFiles/MXF"));
		return scanMxfRoot(mxfViaRoot, driveName, drivePath);
	}

	// strategy 2: path contains "Avid MediaFiles"
	if (dirName == "Avid MediaFiles" ||
		drivePath.contains("Avid MediaFiles", Qt::CaseInsensitive))
	{

		QString avidPart = drivePath;
		int idx = drivePath.indexOf("Avid MediaFiles", 0, Qt::CaseInsensitive);
		if (idx >= 0)
		{
			avidPart =
				drivePath.left(idx) +
				drivePath.mid(
					idx, static_cast<int>(QString("Avid MediaFiles").length()));
		}
		QString mxfInside = avidPart + "/MXF";
		if (QDir(mxfInside).exists())
		{
			emitLog(0, "scanner",
					QString("  Found MXF folder at %1").arg(mxfInside));
			return scanMxfRoot(mxfInside, driveName, avidPart);
		}

		if (drivePath.endsWith("/MXF", Qt::CaseInsensitive))
		{
			emitLog(0, "scanner", QString("  Pointed directly at MXF folder"));
			return scanMxfRoot(drivePath, driveName,
							   QFileInfo(drivePath).absolutePath());
		}
	}

	// strategy 3: path is itself an MXF folder
	if (dirName.compare("MXF", Qt::CaseInsensitive) == 0 ||
		dirName.compare("OMF", Qt::CaseInsensitive) == 0)
	{
		QStringList subs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
		if (!subs.isEmpty())
		{
			emitLog(0, "scanner",
					QString("  MXF folder with %1 subfolders")
						.arg(static_cast<int>(subs.size())));
			return scanMxfRoot(drivePath, driveName,
							   QFileInfo(drivePath).absolutePath());
		}
	}

	// strategy 4: path has .mdb or .pmr — single media folder
	if (QFile::exists(drivePath + "/msmMMOB.mdb") ||
		QFile::exists(drivePath + "/msmFMID.pmr"))
	{
		emitLog(0, "scanner", QString("  Found database files in folder"));

		ScanTask t;
		t.folderPath = drivePath;
		t.folderNumber = dir.dirName();
		t.driveName = driveName;
		t.drivePath = QFileInfo(drivePath).absolutePath();
		t.parseMxf = m_options.parseMxfHeaders;

		auto result = processFolderTask(t);
		// Emit buffered logs (single-threaded path, safe to emit directly)
		for (const auto &msg : result.logs)
			emitLog(msg.level, msg.module, msg.message);
		return result.files;
	}

	// strategy 5: deep search for Avid MediaFiles
	emitLog(0, "scanner",
			QString("  Searching for Avid MediaFiles in %1...").arg(driveName));

	QStringList searchDirs = {drivePath};
	for (const QString &sub1 :
		 dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
	{
		if (m_cancel.load(std::memory_order_relaxed))
			break;
		QString path1 = drivePath + "/" + sub1;
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
		if (m_cancel.load(std::memory_order_relaxed))
			break;
		QString candidate = searchDir + "/Avid MediaFiles/MXF";
		if (QDir(candidate).exists())
		{
			emitLog(0, "scanner",
					QString("  Found Avid media at %1").arg(candidate));
			auto subFiles = scanMxfRoot(candidate, driveName, searchDir);
			files.append(subFiles);
		}
	}

	if (files.isEmpty())
	{
		emitLog(1, "scanner",
				QString("  No Avid MediaFiles found in %1").arg(driveName));
	}

	return files;
}

QVector<MediaFile> ScannerWorker::scanMxfRoot(const QString &mxfRootPath,
											  const QString &driveName,
											  const QString &drivePath)
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
		if (m_cancel.load(std::memory_order_relaxed))
			break;

		// Avid puts problematic files here; preserve the deep-scan path.
		if (folder.compare("Quarantined Files", Qt::CaseInsensitive) == 0)
		{
		}

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
		t.driveName = driveName;
		t.drivePath = drivePath;
		t.parseMxf = m_options.parseMxfHeaders;
		tasks.append(t);
	}

	emitLog(0, "scanner",
			QString("  %1 subfolders queued for concurrent scanning")
				.arg(tasks.size()));

	std::atomic<int> completedFolders{0};
	int totalFolders = tasks.size();

	// Cap progress emits at ~30 Hz to spare the GUI event loop.
	QElapsedTimer progressTimer;
	progressTimer.start();
	std::atomic<qint64> lastProgressMs{0};

	// Concurrent per-folder scan; results returned in input order.
	QFuture<FolderResult> future = QtConcurrent::mapped(
		tasks, [this, &completedFolders, totalFolders, &progressTimer,
				&lastProgressMs](const ScanTask &t)
		{
            auto res = this->processFolderTask(t);
            int done = ++completedFolders;

            // Slow-mode throttle; no-op otherwise.
            DebugSlowdown::pauseForMs(80);

            // Always emit on the last folder so the bar hits 100%; otherwise gate to ~30 Hz.
            const qint64 nowMs = progressTimer.elapsed();
            const qint64 lastMs = lastProgressMs.load(std::memory_order_relaxed);
            if (done == totalFolders || (nowMs - lastMs) >= 33) {
                lastProgressMs.store(nowMs, std::memory_order_relaxed);
                emit progress(QString("Scanning %1").arg(t.driveName), done,
                              totalFolders, t.folderPath);
            }
            return res; });

	// Wait for all threads to finish
	future.waitForFinished();

	// Drain per-task logs in input order for deterministic console output.
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

FolderResult ScannerWorker::processFolderTask(const ScanTask &task)
{
	FolderResult result;

	if (m_cancel.load(std::memory_order_relaxed))
		return result;

	// Helper lambda to buffer logs instead of emitting from pool threads
	auto bufLog = [&result](int level, const QString &module, const QString &msg)
	{
		result.logs.append({level, module, msg});
	};

	// One pass builds primary (NFC+lowercase) + fallback (ext stripped, dots→'_') maps.
	PmrParser::ProjectMaps pmrMaps;
	QString pmrPath = task.folderPath + "/msmFMID.pmr";
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
		// missing PMR files in Interplay / non-numbered folders is normal.
		bufLog(0, "pmr",
			   QString("  No msmFMID.pmr in /%1").arg(task.folderNumber));
	}

	// MDB extraction.
	MdbParser::RecordMap mdbMap;
	QString mdbPath = task.folderPath + "/msmMMOB.mdb";
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
		// missing MDB files in Interplay / non-numbered folders is normal.
		bufLog(0, "mdb",
			   QString("  No msmMMOB.mdb in /%1").arg(task.folderNumber));
	}

	// Now walk the actual files in the folder.
	QFileInfoList entries;
	QDir folder(task.folderPath);

	// Deep recursive scan specifically for the Quarantined folder to catch nested items
	if (task.folderNumber.compare("Quarantined Files", Qt::CaseInsensitive) == 0)
	{
		QDirIterator it(task.folderPath, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
		while (it.hasNext())
		{
			it.next();
			entries.append(it.fileInfo());
		}

		int mxfCount = 0;
		for (const QFileInfo &entry : entries)
		{
			if (entry.suffix().toLower() == "mxf")
				mxfCount++;
		}

		if (entries.isEmpty())
		{
			bufLog(0, "scanner", QString("  Quarantined Files folder on %1 is empty").arg(task.driveName));
		}
		else if (mxfCount > 0)
		{
			bufLog(1, "scanner", QString("⚠️ Avid Quarantined Files folder on %1 contains %2 MXF file(s)!").arg(task.driveName).arg(mxfCount));
		}
		else
		{
			bufLog(0, "scanner", QString("  Quarantined Files folder on %1 contains %2 non-MXF file(s)").arg(task.driveName).arg(entries.size()));
		}
	}
	else
	{
		// Standard single-level extraction for all normal media folders
		entries = folder.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
	}

	for (const QFileInfo &entry : entries)
	{
		if (m_cancel.load(std::memory_order_relaxed))
			break;

		QString fileName = entry.fileName();
		if (fileName == "msmMMOB.mdb" || fileName == "msmFMID.pmr")
			continue;

		bool isJunk = JUNK_FILES.contains(fileName.toLower());

		MediaFile mf =
			buildMediaFile(entry.filePath(), task.driveName, task.drivePath,
						   task.folderNumber, pmrMaps, mdbMap, task.parseMxf);

		if (isJunk)
		{
			mf.isDSStore = true;
			mf.project = "UNMANAGED_FILES";
			mf.clipName = fileName;
			mf.isUnmanaged = true;
		}

		result.files.append(mf);
	}

	if (result.files.size() > 4000)
	{
		bufLog(1, "scanner",
			   QString("Warning: folder %1/%2 has %3 files (>4000)")
				   .arg(task.driveName, task.folderNumber)
				   .arg(result.files.size()));
	}

	return result;
}

MediaFile ScannerWorker::buildMediaFile(
	const QString &filePath, const QString &driveName, const QString &drivePath,
	const QString &folderNumber, const PmrParser::ProjectMaps &pmrMaps,
	const MdbParser::RecordMap &mdbMap, bool parseMxf)
{
	MediaFile mf;
	QFileInfo fi(filePath);

	// No synthetic UUID — filePath + mobId/compositionMobId is enough; UUID gen was
	// measurable CPU at 290k files for a value no lookup uses.
	mf.filePath = filePath;
	mf.fileName = fi.fileName();
	mf.extension = "." + fi.suffix().toLower();
	mf.driveName = driveName;
	mf.drivePath = drivePath;
	mf.mxfFolder = folderNumber;

	// Pre-compute Volume column ("DATA/Avid MediaFiles/MXF/2") once — paint hot path.
	{
		const QString containingDir = fi.absolutePath();
		QString relInsideVolume = containingDir;
		if (!drivePath.isEmpty() && containingDir.startsWith(drivePath))
		{
			relInsideVolume = containingDir.mid(drivePath.length());
			while (relInsideVolume.startsWith('/') ||
				   relInsideVolume.startsWith('\\'))
				relInsideVolume.remove(0, 1);
		}
		mf.volumeDisplay = relInsideVolume.isEmpty()
							   ? driveName
							   : (driveName + QStringLiteral("/") +
								  relInsideVolume);
	}

	mf.sizeBytes = fi.size();
	mf.sizeMB = fi.size() / (1024.0 * 1024.0);
	mf.modified = fi.lastModified();
	mf.created = fi.birthTime().isValid() ? fi.birthTime() : fi.lastModified();
	mf.clipName = fi.completeBaseName();
	mf.isNonPortable = isNonPortableFilename(mf.fileName);

	// Primary key normalises NFC+case (HFS+/APFS NFD vs Avid NFC, SMB/NEXIS/NTFS case);
	// fallback strips extension and punctuation. Both pre-built per PMR — each lookup O(1).
	const QString primaryKey =
		mf.fileName.normalized(QString::NormalizationForm_C).toLower();

	// Apply a PMR hit; shared by primary and fallback branches.
	auto applyPmrHit = [&mf](const PmrEntry &pmr)
	{
		mf.project = pmr.project;
		mf.mobId = pmr.mobIdHex;
		mf.compositionMobId = pmr.compositionMobIdHex;
		if (!pmr.bin.isEmpty())
			mf.originalBin = pmr.bin;
	};

	auto pmrIt = pmrMaps.primary.find(primaryKey);
	if (pmrIt != pmrMaps.primary.end() && !pmrIt->isEmpty())
	{
		applyPmrHit(pmrIt->first());
	}
	else
	{
		const int lastDot = primaryKey.lastIndexOf('.');
		QString base = (lastDot > 0) ? primaryKey.left(lastDot) : primaryKey;
		const QString fallbackKey = base.replace('.', '_');
		pmrIt = pmrMaps.fallback.find(fallbackKey);
		if (pmrIt != pmrMaps.fallback.end() && !pmrIt->isEmpty())
			applyPmrHit(pmrIt->first());
	}

	// Merge file-MOB and composition-MOB records: clip name/TC live on the
	// composition; bin/source/imported live on either.
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

	if (parseMxf && mf.extension == ".mxf" && fi.size() > 1024)
	{
		MxfMetadata mxf = MxfParser::parseHeader(filePath);
		if (mxf.valid)
		{
			if (!mxf.umid.isEmpty())
				mf.umid = mxf.umid;
			if (mf.clipName == fi.completeBaseName() && !mxf.clipName.isEmpty())
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
			{
				mf.mediaType = MediaFile::Type::Audio;
			}
		}
	}

	if (mf.extension == ".wav" || mf.extension == ".aif" ||
		mf.extension == ".aiff" || mf.extension == ".bwf")
	{
		mf.mediaType = MediaFile::Type::Audio;
		if (mf.resolution.isEmpty())
			mf.resolution = QStringLiteral("\xe2\x80\x94");
	}

	mf.kind = "Media";

	// Precompute detection in three passes:
	//   filename prefix (V##/A## source, P##/W… precomp) → Avid effect vocabulary
	//   in the clip name → "precomput" substring as a catch-all.
	// Filename-first because clip names can legitimately have commas/punctuation
	// (e.g. "Track 01, String Quartet, Movement III") so a raw comma count misclassifies.
	static const QRegularExpression kPrecompPrefixRe(
		QStringLiteral("^(?:P\\d+\\.|W[A-Z0-9]+[A-Z]\\d+\\.|WA\\d+\\.)"),
		QRegularExpression::CaseInsensitiveOption);

	bool isPrecomp = kPrecompPrefixRe.match(mf.fileName).hasMatch();

	if (!isPrecomp)
	{
		// Frequent effects first for early-out; matched against lowerClip.
		static const QStringList kAvidEffectKeywords = {
			// Most common in daily editing
			"Dissolve",
			"Resize",
			"Title",
			"Color_Correction",
			"Color_Adapter",
			// Common but less frequent
			"Precompute",
			"FrameFlex",
			"Color_LUT",
			"Color_Effect",
			"Timecode_Burn-In",
			"Stabilize",
			"Submaster",
			"Motion Effect",
			"Flop",
			"Flip",
		};

		const QString lowerClip = mf.clipName.toLower();

		for (const QString &kw : kAvidEffectKeywords)
		{
			if (lowerClip.contains(kw.toLower()))
			{
				isPrecomp = true;
				break;
			}
		}

		// Boris Sapphire renders show up as "Source,S_Glow,1" — require the leading comma
		// so source clips that legitimately start with "S_" don't get swept up.
		if (!isPrecomp && mf.clipName.contains(",S_"))
			isPrecomp = true;
	}

	if (isPrecomp)
		mf.kind = "Precompute";

	if (mf.mediaType == MediaFile::Type::Audio)
	{
		if (mf.sampleRate > 0)
		{
			mf.bitRateString = QString::number(mf.sampleRate / 1000) + " kHz";
		}
	}
	else
	{
		float fpsNum = mf.fps.toFloat();
		if (fpsNum > 0 && mf.durationFrames > 0 && mf.sizeBytes > 0)
		{
			float seconds = mf.durationFrames / fpsNum;
			if (seconds > 0)
			{
				int mbps = qRound((mf.sizeBytes * 8.0) / (seconds * 1000000.0));
				// Snap to a common DNxHD bitrate; ordered hot-first for early break
				// (DNxHD 36/145/220 dominate real projects).
				static constexpr std::array<int, 12> kStandards{
					36, 145, 220, 115, 120, 175, 185, 45, 50, 100, 350, 440};
				for (int std : kStandards)
				{
					if (qAbs(mbps - std) <= 10)
					{
						mbps = std;
						break;
					}
				}
				mf.bitRateString = QString::number(mbps);
			}
		}
	}

	if (mf.project.isEmpty())
	{
		mf.isUnmanaged = true;
		mf.project = "UNMANAGED_FILES";
	}
	if (!mf.umid.isEmpty())
	{
		// All-zero UMID — Avid never wrote a real material number.
		mf.isBadUmid = MobId::isAllZero(mf.umid);
	}
	if (!mf.mobId.isEmpty() && mf.isUnmanaged && !mdbMap.isEmpty())
	{
		mf.isUnreferenced = true;
		mf.isUnmanaged = false;
	}

	return mf;
}

bool ScannerWorker::isNonPortableFilename(const QString &name) const
{
	// ASCII fast-path; replaced a thread_local QRegularExpression that was
	// ~6% of scan CPU at 290k files. Allowed: A-Z a-z 0-9 . _ - space , ( ) [ ] + = ' ~ @ # % &
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

	if (name.isEmpty())
		return false;
	for (QChar ch : name)
	{
		const ushort u = ch.unicode();
		if (u >= 128)
			return true; // non-ASCII
		if (!kPortableChars[u])
			return true;
	}
	return false;
}