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

// Meyers singleton: the QSet is constructed on first call (thread-safe in
// C++11+) rather than during file-scope static init, sidestepping any
// initialization-order surprises against other translation units' statics.
static const QSet<QString> &junkFiles()
{
	static const QSet<QString> s = {
		".ds_store", "thumbs.db", ".spotlight-v100", ".fseventsd",
		".trashes", "desktop.ini", "._.ds_store"};
	return s;
}

MediaScanner::MediaScanner(QObject *parent) : QObject(parent) {}

MediaScanner::~MediaScanner()
{
	cancelScan();
	// Local copy so QThread::finished can't null m_thread out from under us.
	// 30s is generous compared to the worst-case cancel-respond time
	// (cooperative cancel is checked at folder/file boundaries — sub-second
	// in normal flow, single-digit seconds on a stuttering Nexis).
	WorkerThread::joinOrTerminate(m_thread, 30000);
	// No delete: thread is parented to `this` and self-deletes on finished.
}

void MediaScanner::startScan(const Options &options)
{
	// CAS guards against re-entry from rapid double-clicks.
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

ScannerWorker::ScannerWorker(const MediaScanner::Options &options)
	: m_options(options) {}

void ScannerWorker::cancel()
{
	m_cancel.store(true, std::memory_order_relaxed);
}

void ScannerWorker::process()
{
	// Reset every entry so the flush clock starts at scan-begin even if a
	// previous process() call left m_flushTimer running.
	m_flushTimer.start();
	m_lastFlushElapsed = 0;
	doScan();
}

namespace
{
	constexpr int kLogBatchMaxSize = 50;
	constexpr qint64 kLogBatchMaxAgeMs = 100;

	// Bitrate string from durationFrames + sizeBytes for video, or sampleRate
	// for audio. Called once in Stage 1, then again in Stage 2 after MXF
	// data has been merged — both inputs can change.
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
		// Snap to a common DNxHD bitrate; ordered hot-first for early break.
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

	// Merge MXF metadata into a MediaFile and recompute the dependent fields
	// (audio override, bitRateString, isBadUmid). Used by Stage 2.
	void applyMxfMetadata(MediaFile &mf, const MxfMetadata &mxf)
	{
		if (mxf.valid)
		{
			const int dot = mf.fileName.lastIndexOf(QLatin1Char('.'));
			const QString fileBase = (dot > 0) ? mf.fileName.left(dot) : mf.fileName;

			if (!mxf.umid.isEmpty())
				mf.umid = mxf.umid;
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
		// UMID with zeros = Avid never wrote a real material number.
		if (!mf.umid.isEmpty())
			mf.isBadUmid = MobId::isAllZero(mf.umid);
	}
}

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
	return access(QFile::encodeName(path).constData(), R_OK) == 0;
#else
	return QFileInfo(path).isReadable();
#endif
}

void ScannerWorker::doScan()
{
	QVector<MediaFile> allFiles;

	emitLog(0, "scanner",
			QString("Scanning %1 location(s)...")
				.arg(m_options.drivePaths.size()));

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
					"Security");
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
						.arg(driveFiles.size()));
		}
	}

	if (m_cancel.load(std::memory_order_relaxed))
	{
		emitLog(1, "scanner", "Scan cancelled by user");
		emit finished(allFiles);
		return;
	}

	parseMxfHeadersConcurrently(allFiles);

	if (m_cancel.load(std::memory_order_relaxed))
	{
		emitLog(1, "scanner", "Scan cancelled by user");
		emit finished(allFiles);
		return;
	}

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

	// Drain any final buffered logs before the UI is told the scan ended.
	flushLogs();
	emit finished(allFiles);
}

QVector<MediaFile> ScannerWorker::scanDrive(const QString &drivePath,
											const QString &driveName)
{
	QVector<MediaFile> files;
	QDir dir(drivePath);
	const QString dirName = dir.dirName();

	// 1) Volume root contains Avid MediaFiles/MXF.
	const QString mxfViaRoot = drivePath + "/Avid MediaFiles/MXF";
	if (QDir(mxfViaRoot).exists())
	{
		emitLog(0, "scanner", QStringLiteral("  Found Avid MediaFiles/MXF"));
		return scanMxfRoot(mxfViaRoot, driveName, drivePath);
	}

	// 2) Path is somewhere inside an Avid MediaFiles directory.
	constexpr int kAvidLen = sizeof("Avid MediaFiles") - 1;
	const int avidIdx = drivePath.indexOf("Avid MediaFiles", 0, Qt::CaseInsensitive);
	if (avidIdx >= 0)
	{
		const QString avidPart = drivePath.left(avidIdx + kAvidLen);
		const QString mxfInside = avidPart + "/MXF";
		if (QDir(mxfInside).exists())
		{
			emitLog(0, "scanner", QString("  Found MXF folder at %1").arg(mxfInside));
			return scanMxfRoot(mxfInside, driveName, avidPart);
		}

		if (drivePath.endsWith("/MXF", Qt::CaseInsensitive))
		{
			emitLog(0, "scanner", QStringLiteral("  Pointed directly at MXF folder"));
			return scanMxfRoot(drivePath, driveName, QFileInfo(drivePath).absolutePath());
		}
	}

	// 3) Path itself is an MXF or OMF root.
	if (dirName.compare("MXF", Qt::CaseInsensitive) == 0 ||
		dirName.compare("OMF", Qt::CaseInsensitive) == 0)
	{
		const QStringList subs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
		if (!subs.isEmpty())
		{
			emitLog(0, "scanner",
					QString("  MXF folder with %1 subfolders").arg(subs.size()));
			return scanMxfRoot(drivePath, driveName, QFileInfo(drivePath).absolutePath());
		}
	}

	// 4) Single media folder — has the per-folder Avid databases.
	if (QFile::exists(drivePath + "/msmMMOB.mdb") ||
		QFile::exists(drivePath + "/msmFMID.pmr"))
	{
		emitLog(0, "scanner", QStringLiteral("  Found database files in folder"));

		ScanTask t;
		t.folderPath = drivePath;
		t.folderNumber = dir.dirName();
		t.driveName = driveName;
		t.drivePath = QFileInfo(drivePath).absolutePath();

		auto result = processFolderTask(t);
		for (const auto &msg : result.logs)
			emitLog(msg.level, msg.module, msg.message);
		return result.files;
	}

	// 5) Deep search.
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
		tasks.append(t);
	}

	emitLog(0, "scanner",
			QString("  %1 subfolders queued for concurrent scanning")
				.arg(tasks.size()));

	std::atomic<int> completedFolders{0};
	const int totalFolders = tasks.size();

	// Cap progress emits at ~30 Hz to spare the UI event loop.
	ProgressThrottle throttle;

	// Concurrent per-folder scan; results returned in input order.
	QFuture<FolderResult> future = QtConcurrent::mapped(
		tasks, [this, &completedFolders, totalFolders, &throttle](const ScanTask &t)
		{
            auto res = this->processFolderTask(t);
            int done = ++completedFolders;

            // Slow-mode throttle; a no-op when slow mode is off.
            DebugSlowdown::pauseForMs(80);

            // Always emit on the last folder so the bar hits 100%; otherwise gate to ~30 Hz.
            if (done == totalFolders || throttle.shouldEmit()) {
                emit progress(QString("Scanning %1").arg(t.driveName), done,
                              totalFolders, t.folderPath);
            }
            return res; });

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

	// Pool threads can't emit signals safely; buffer logs and drain back on the worker thread.
	auto bufLog = [&result](int level, const QString &module, const QString &msg)
	{
		result.logs.append({level, module, msg});
	};

	// Missing PMR and MDB is normal in Interplay environments.
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

	QFileInfoList entries;
	QDir folder(task.folderPath);

	// Deep recursive scan for the Quarantined folder to catch nested items;
	// flat scan everywhere else.
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
					   .arg(task.driveName));
		else if (mxfCount > 0)
			bufLog(1, "scanner",
				   QString("⚠️ Avid Quarantined Files folder on %1 contains %2 MXF file(s)!")
					   .arg(task.driveName)
					   .arg(mxfCount));
		else
			bufLog(0, "scanner",
				   QString("  Quarantined Files folder on %1 contains %2 non-MXF file(s)")
					   .arg(task.driveName)
					   .arg(entries.size()));
	}
	else
	{
		entries = folder.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
	}

	for (const QFileInfo &entry : entries)
	{
		if (m_cancel.load(std::memory_order_relaxed))
			break;

		QString fileName = entry.fileName();
		if (fileName == "msmMMOB.mdb" || fileName == "msmFMID.pmr")
			continue;

		bool isJunk = junkFiles().contains(fileName.toLower());

		MediaFile mf =
			buildMediaFile(entry.filePath(), task.driveName, task.drivePath,
						   task.folderNumber, pmrMaps, mdbMap);

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
	const MdbParser::RecordMap &mdbMap)
{
	MediaFile mf;
	QFileInfo fi(filePath);
	mf.filePath = filePath;
	mf.fileName = fi.fileName();
	mf.extension = "." + fi.suffix().toLower();
	mf.driveName = driveName;
	mf.drivePath = drivePath;
	mf.mxfFolder = folderNumber;

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
		pmrIt = pmrMaps.fallback.find(PmrKey::fallback(primaryKey));
		if (pmrIt != pmrMaps.fallback.end() && !pmrIt->isEmpty())
			applyPmrHit(pmrIt->first());
	}

	// File-MOB / Composition-MOB carry overlapping fields:
	// clipname/timecode live on the composition; bin/source/imported on either.
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

	// MXF header parsing is deferred to parseMxfHeadersConcurrently so all
	// idle cores can attack it in parallel rather than serially per folder.

	if (mf.extension == ".wav" || mf.extension == ".aif" ||
		mf.extension == ".aiff" || mf.extension == ".bwf")
	{
		mf.mediaType = MediaFile::Type::Audio;
		if (mf.resolution.isEmpty())
			mf.resolution = QStringLiteral("\xe2\x80\x94");
	}

	mf.kind = "Media";

	// Precomp detection: filename prefix (P##/W##) first, then Avid Effect
	// keywords in the clip name. Filename takes precedence because clips
	// can legitimately contain commas.
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

		// Boris Sapphire renders look like "Source,S_Glow,1". The leading
		// comma keeps clips that legitimately start with "S_" out.
		if (!isPrecomp && mf.clipName.contains(",S_"))
			isPrecomp = true;
	}

	if (isPrecomp)
		mf.kind = "Precompute";

	recomputeBitrate(mf);

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

void ScannerWorker::parseMxfHeadersConcurrently(QVector<MediaFile> &files)
{
	if (!m_options.parseMxfHeaders)
		return;

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

	QtConcurrent::blockingMap(mxfIndices, [&](int idx)
	{
		if (m_cancel.load(std::memory_order_relaxed))
			return;
		MediaFile &mf = files[idx];
		qint64 bytesRead = 0;
		const MxfMetadata mxf = MxfParser::parseHeader(mf.filePath, &bytesRead);
		applyMxfMetadata(mf, mxf);

		totalBytesRead.fetch_add(bytesRead, std::memory_order_relaxed);
		// Lock-free max via CAS loop.
		qint64 prev = maxBytesRead.load(std::memory_order_relaxed);
		while (bytesRead > prev &&
			   !maxBytesRead.compare_exchange_weak(prev, bytesRead,
												   std::memory_order_relaxed))
		{
		}

		const int n = ++done;
		if (n == total || throttle.shouldEmit())
			emit progress("Parsing MXF headers", n, total, mf.fileName);
	});

	const qint64 totalBytes = totalBytesRead.load();
	const qint64 maxBytes = maxBytesRead.load();
	const qint64 avgKB = total > 0 ? (totalBytes / total) / 1024 : 0;
	emitLog(0, "mxf",
			QString("MXF parse: %1 files, avg %2 KB/file, max %3 KB, total %4 MB read")
				.arg(total).arg(avgKB).arg(maxBytes / 1024).arg(totalBytes / (1024 * 1024)));
}

bool ScannerWorker::isNonPortableFilename(const QString &name) const
{
	// Allowed: A-Z a-z 0-9 . _ - space , ( ) [ ] + = ' ~ @ # % &
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