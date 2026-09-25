#include "mediascanner.h"
#include "avideffects.h"
#include "avidusage.h"
#include "conventions.h"
#include "testpause.h"
#include "diagnostics.h"
#include "mobid.h"
#include "mxfparser.h"
#include "omfparser.h" // OMF-era: the Bento-tail twin of MxfParser for legacy essence
#include "pmrkey.h"
#include "progressthrottle.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QMutexLocker>
#include <QSet>
#include <QStringList>
#include <QtConcurrent>
#include <algorithm>
#include <array>

#ifdef Q_OS_MAC
#include <unistd.h>
#endif

// MARK: - MediaScanner construction

namespace
{
	QString scannerFolderKey(const QString &path)
	{
		const QFileInfo info(path);
		const QString canonical = info.canonicalFilePath();
		return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
	}

	bool isInsideOmfRoot(const QString &path)
	{
		const auto parts = QDir::cleanPath(QDir::fromNativeSeparators(path)).split(QLatin1Char('/'));
		return std::any_of(parts.cbegin(), parts.cend(), [](const QString &part)
						   { return Conventions::isOmfRootName(part); });
	}

	QString childDirectory(const QString &parent, QLatin1String name)
	{
		const QDir dir(parent);
		const QString expected = dir.filePath(name);
		if (QFileInfo(expected).isDir())
			return expected;
		for (const QFileInfo &child : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
			if (child.fileName().compare(name, Qt::CaseInsensitive) == 0)
				return child.absoluteFilePath();
		return {};
	}

	struct MediaRoot
	{
		AvidMediaLayout::Family family;
		QString path;
		QString volumePath;
	};

	QVector<MediaRoot> rootsForAddedPath(const QString &requestedPath)
	{
		if (!QFileInfo(requestedPath).isDir())
			return {};
		const QString path = scannerFolderKey(requestedPath);
		if (AvidMediaLayout::isInsideUmeRoot(requestedPath) || AvidMediaLayout::isInsideUmeRoot(path))
			return {};
		if (const auto location = AvidMediaLayout::locateMediaFolder(path))
			return {{location->family, location->rootPath, QFileInfo(location->rootPath).absolutePath()}};
		if (AvidMediaLayout::isMxfRoot(path))
			return {{AvidMediaLayout::Family::Mxf, path, QFileInfo(path).absolutePath()}};
		QVector<MediaRoot> roots;
		const bool isAvidRoot = QFileInfo(path).fileName().compare(Conventions::kAvidMediaFilesDir, Qt::CaseInsensitive) == 0;
		const QString avidRoot = isAvidRoot ? path : childDirectory(path, Conventions::kAvidMediaFilesDir);
		const QString mxfRoot = avidRoot.isEmpty() ? QString{} : childDirectory(avidRoot, Conventions::kMxfDir);
		if (!mxfRoot.isEmpty() && AvidMediaLayout::isMxfRoot(scannerFolderKey(mxfRoot)))
			roots.append({AvidMediaLayout::Family::Mxf, mxfRoot, path});
		const QString omfRoot = childDirectory(path, Conventions::kOmfMediaFilesDir);
		if (!omfRoot.isEmpty() && AvidMediaLayout::isOmfRoot(scannerFolderKey(omfRoot)))
			roots.append({AvidMediaLayout::Family::Omf, omfRoot, path});
		return roots;
	}
}

MediaScanner::MediaScanner(QObject *parent)
	: QObject(parent)
{
}

bool MediaScanner::canScanPath(const QString &path)
{
	return !rootsForAddedPath(path).isEmpty();
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
		m_seenFolders.clear();
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
	// Classification follows the selected master mob, never its name. MDB/OMF
	// master application codes 1 and 7 mean Precompute and Media respectively.
	// MXF combines the private MobAppCode with the standard UsageCode UID:
	// LowerLevel alone also covers groups and motion effects in MC 26.8.
	// See AvidUsage for the shared definitions and conflict handling.
	//
	// File-mob codes are not a verdict: the 2,493-file corpus includes 107
	// precomputes with file code 9 and another 64 with file code 0. Unknown or
	// conflicting master usage stays Unknown. Catalogue/name lookup happens
	// only after classification, so a title or renamed clip cannot establish
	// that the underlying media is a precompute.

	/// The one place a clip name is ever assigned. Takes only when the new
	/// name comes from a STRICTLY better source, so the rungs of the ladder
	/// can arrive in any order — which they do: pass 1 reads the MDB, pass 2
	/// the MXF header and then the MDB again via the UMID re-join.
	/// Strictly-better also means the first of two equal-ranked sources wins.
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

	// Copy non-empty MDB fields onto the MediaFile. Shared by pass 1 (the
	// master-MOB lookup) and pass 2's UMID re-join. assignIfMissing means
	// call order doesn't matter; first non-empty wins.
	//
	// The clip name is the MDB rung of the ladder (see
	// MediaFile::ClipNameSource): setClipName ranks it below a
	// MaterialPackage name, so it only ever shows for files whose MXF
	// header can't be read.
	//
	// PMR carries a project but no bin. Header metadata can also supply the
	// recorded original bin. After scanning, the table may fill a remaining
	// blank from an owning master clip's _ORG_BIN in a loaded AVB.
	void applyMdbRecord(MediaFile &mf, const MdbMasterMob &rec)
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
	/// says. Every value has already been through MediaMetadataUtil::finalise, so the
	/// two cannot disagree on a derived field.
	void applyMetadata(MediaFile &mf, const MediaMetadata &metadata)
	{
		// A failed/incomplete header read cannot negate an earlier database
		// classification or contribute half-read import/identity information.
		if (!metadata.valid && !metadata.classificationKnown)
			return;
		if (metadata.clipNameFromMaterial)
			setClipName(mf, metadata.clipName, MediaFile::ClipNameSource::MaterialPackage);
		if (metadata.valid)
		{
			if (!metadata.codec.isEmpty())
				mf.codec = metadata.codec;
			if (!metadata.resolution.isEmpty())
				mf.resolution = metadata.resolution;
			if (!metadata.fps.isEmpty())
				mf.fps = metadata.fps;
			if (!metadata.bitDepth.isEmpty())
				mf.bitDepth = metadata.bitDepth;
			if (metadata.sampleRate > 0)
				mf.sampleRate = metadata.sampleRate;
			if (metadata.channels > 0)
				mf.channels = metadata.channels;
			if (metadata.durationFrames > 0)
				mf.durationFrames = metadata.durationFrames;
			if (metadata.timecodeBase > 0)
				mf.timecodeBase = metadata.timecodeBase;
			if (metadata.dropFrame)
				mf.dropFrame = true;
			// The producer owns audio-ness end to end (descriptor sets or the
			// essence label's own bytes in a header; the descriptor class in
			// the MDB). No display-name comparisons here.
			if (metadata.isAudio)
				mf.kind = MediaFile::Kind::Audio;
			else if (metadata.width > 0 && metadata.height > 0)
				mf.kind = MediaFile::Kind::Video;
		}

		// Import facts a header carries as TaggedValues (UNC Path, Video,
		// _IMPORTSETTING). The MDB usually supplied them in pass 1; this is
		// what gives a row WITHOUT a database — Interplay — the same columns.
		assignIfMissing(mf.sourceFilePath, metadata.sourceFilePath);
		assignIfMissing(mf.sourceContainer, metadata.sourceContainer);
		if (mf.sourceFileName.isEmpty() && !mf.sourceFilePath.isEmpty())
			mf.sourceFileName = MediaMetadataUtil::sourceFileBaseName(mf.sourceFilePath);
		if (metadata.hasImportSetting)
			mf.isImported = true;

		// The one place a file is classified. Apply a producer's supported
		// verdict; absence of a verdict cannot stand in for ordinary media.
		if (metadata.classificationKnown)
		{
			mf.type = metadata.isPrecompute ? MediaFile::Type::Precompute : MediaFile::Type::Media;
			mf.precomputeCategory = metadata.isPrecompute ? metadata.precomputeCategory : MediaFile::PrecomputeCategory::Unknown;
		}
		else if (metadata.valid && metadata.hasMaterialPackage)
		{
			// A fully read material package with unsupported/conflicting usage
			// cannot retain an earlier database's positive classification.
			mf.type = MediaFile::Type::Unknown;
			mf.precomputeCategory = MediaFile::PrecomputeCategory::Unknown;
		}
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

	const int locationCount = m_options.volumePaths.size() + m_options.manualPaths.size();
	emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("Scanning %1 location(s)...").arg(locationCount));

	QElapsedTimer stageTimer;
	stageTimer.start();
	qCDebug(lcScanner) << "scan start:" << locationCount << "location(s)";

	// MARK: Pass 1 — per-location folder walk + databases

	// Volumes and hand-added folders share the readability gate and the
	// bookkeeping; they differ only in which locator runs (see the class
	// doc). A location is scanned once even if it appears in both lists.
	QSet<QString> scanned;
	auto scanLocation = [this, &allFiles, &scanned](const QString &path, bool manual)
	{
		// UME/OP1a is outside v1's supported media roots. Reject it before
		// scanAddedFolder can redirect an Avid MediaFiles path to sibling MXF.
		if (AvidMediaLayout::isInsideUmeRoot(path) || AvidMediaLayout::isInsideUmeRoot(scannerFolderKey(path)))
		{
			emitLog(QtInfoMsg, QStringLiteral("scanner"),
					QStringLiteral("Skipping unsupported UME media folder: %1").arg(path));
			return;
		}
		if (!m_options.includeOmf && isInsideOmfRoot(path))
			return;
		if (scanned.contains(path))
			return;
		scanned.insert(path);

		QDir locationDir(path);
		QString volumeName = locationDir.dirName();
		if (volumeName.isEmpty())
			volumeName = path;

		if (!canReadPath(path))
		{
			emitLog(QtCriticalMsg, QStringLiteral("scanner"), QStringLiteral("Permission denied: %1").arg(path));
			return;
		}

		emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("Scanning: %1 (%2)").arg(volumeName, path));
		emit scanProgress(0, 0, path);

		auto locationFiles = manual ? scanAddedFolder(path, volumeName) : scanVolumeRoot(path, volumeName);
		allFiles.append(locationFiles);

		if (!locationFiles.isEmpty())
		{
			emitLog(QtInfoMsg, QStringLiteral("scanner"),
					QStringLiteral("  %1: %2 media files found").arg(volumeName).arg(locationFiles.size()));
		}
	};

	for (const QString &volumePath : m_options.volumePaths)
	{
		if (m_job.isCancelled())
			break;
		scanLocation(volumePath, /*manual=*/false);
	}
	for (const QString &manualPath : m_options.manualPaths)
	{
		if (m_job.isCancelled())
			break;
		scanLocation(manualPath, /*manual=*/true);
	}

	qCDebug(lcScanner) << "pass 1 (walk + databases):" << allFiles.size() << "files in" << stageTimer.restart()
					   << "ms";

	// MARK: Pass 2 — headers for the rows the databases didn't cover

	if (!m_job.isCancelled())
	{
		readMediaHeadersConcurrently(allFiles);
		qCDebug(lcScanner) << "pass 2 (headers):" << stageTimer.restart() << "ms";
	}

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

	// Only metadata-confirmed precomputes receive name-derived effect details.
	// The name never decides their media type or precompute category.
	for (MediaFile &f : allFiles)
	{
		if (f.type != MediaFile::Type::Precompute)
			continue;
		const AvidEffects::Hit hit = AvidEffects::lookup(f.clipName);
		f.effect = hit.name;
		f.effectCategory = hit.category;
		f.effectSequence = hit.sequence;
	}

	int noReference = 0, noDatabase = 0, invalidUmid = 0, noProject = 0, nonPortable = 0;
	for (const auto &f : allFiles)
	{
		if (f.dbStatus == MediaFile::DbStatus::NoReference)
			++noReference;
		if (f.isNoDatabase())
			++noDatabase;
		if (f.isInvalidUmid)
			++invalidUmid;
		if (f.hasNoProject())
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

	QStringList notes;
	if (noReference > 0)
		notes.append(QStringLiteral("%1 file%2 without a local database reference")
						 .arg(noReference)
						 .arg(noReference == 1 ? "" : "s"));
	if (noDatabase > 0)
		notes.append(QStringLiteral("%1 file%2 with missing or unreadable databases")
						 .arg(noDatabase)
						 .arg(noDatabase == 1 ? "" : "s"));
	if (invalidUmid > 0)
		notes.append(QStringLiteral("%1 file%2 with an all-zero UMID")
						 .arg(invalidUmid)
						 .arg(invalidUmid == 1 ? "" : "s"));
	if (noProject > 0)
		notes.append(QStringLiteral("%1 file%2 without a project name")
						 .arg(noProject)
						 .arg(noProject == 1 ? "" : "s"));
	if (nonPortable > 0)
		notes.append(QStringLiteral("%1 non-portable filename%2")
						 .arg(nonPortable)
						 .arg(nonPortable == 1 ? "" : "s"));
	if (!notes.isEmpty())
		emitLog(QtWarningMsg, QStringLiteral("scanner"),
				QStringLiteral("Scan notes: %1").arg(notes.join(QStringLiteral("; "))));

	concludeScan(allFiles, /*cancelled=*/false);
}

// MARK: - Scan conclusion

// Clear shared scan state on every exit so cancellation cannot leave stale
// database records or folder counts for the next scan.
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
							  .arg(Conventions::kFolderWarn)
							  .arg(Conventions::kFolderMax);
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
		m_seenFolders.clear();
	}

	// Drain the log buffer first so the last batch doesn't land
	// after scanFinished.
	flushLogs();
	emit scanFinished(files);
}

// MARK: - Per-volume: the two roots at the top level

QVector<MediaFile> MediaScanner::scanVolumeRoot(const QString &volumePath, const QString &volumeName)
{
	// Avid's placement rule, and nothing else: a drive root (or a
	// system-drive base handed over as its own entry) holds its media roots
	// directly. Media someone moved into a subfolder by hand is found only
	// when that folder is added by hand — see scanAddedFolder.
	QVector<MediaFile> files;

	const QString avidRoot = childDirectory(volumePath, Conventions::kAvidMediaFilesDir);
	const QString mxfViaRoot = avidRoot.isEmpty() ? QString{} : childDirectory(avidRoot, Conventions::kMxfDir);
	if (!mxfViaRoot.isEmpty())
	{
		qCInfo(lcScanner).noquote() << "Found Avid MediaFiles/MXF:" << mxfViaRoot;
		files.append(scanMxfRoot(mxfViaRoot, volumeName, volumePath));
	}

	// OMF-era: the legacy root is a sibling of Avid MediaFiles, and a drive
	// may carry either or both.
	const QString omfViaRoot = m_options.includeOmf ? childDirectory(volumePath, Conventions::kOmfMediaFilesDir) : QString{};
	if (!omfViaRoot.isEmpty())
	{
		qCInfo(lcScanner).noquote() << "Found OMFI MediaFiles:" << omfViaRoot;
		files.append(scanOmfRoot(omfViaRoot, volumeName, volumePath));
	}

	if (files.isEmpty())
	{
		// Name the enabled roots and explain how to reach media buried
		// deeper, since a volume scan will not look for it.
		emitLog(QtWarningMsg, QStringLiteral("scanner"),
				QStringLiteral("  No %1 at the root of %2 "
							   "(media in a subfolder is found via File > Add Folder or Volume)")
					.arg(m_options.includeOmf ? QStringLiteral("Avid MediaFiles or OMFI MediaFiles")
											  : QStringLiteral("Avid MediaFiles"),
						 volumeName));
	}

	return files;
}

// MARK: - Hand-added managed media tree

QVector<MediaFile> MediaScanner::scanAddedFolder(const QString &folderPath, const QString &volumeName)
{
	const auto roots = rootsForAddedPath(folderPath);
	if (roots.isEmpty())
	{
		emitLog(QtWarningMsg, QStringLiteral("scanner"),
				QStringLiteral("Not an Avid media location: %1. Add an Avid MediaFiles or OMFI MediaFiles folder, or its containing folder.").arg(folderPath));
		return {};
	}
	QVector<MediaFile> files;
	for (const MediaRoot &root : roots)
	{
		if (m_job.isCancelled())
			break;
		if (root.family == AvidMediaLayout::Family::Mxf)
			files.append(scanMxfRoot(root.path, volumeName, root.volumePath));
		else if (m_options.includeOmf)
			files.append(scanOmfRoot(root.path, volumeName, root.volumePath));
	}
	return files;
}

// MARK: - OMF root: local media and legacy shared workstation folders

QVector<MediaFile> MediaScanner::scanOmfRoot(const QString &omfRootPath, const QString &volumeName,
											 const QString &volumePath)
{
	if (!m_options.includeOmf || !AvidMediaLayout::isOmfRoot(omfRootPath) ||
		!AvidMediaLayout::isOmfRoot(scannerFolderKey(omfRootPath)))
		return {};
	if (!canReadPath(omfRootPath))
	{
		emitLog(QtCriticalMsg, QStringLiteral("scanner"), QStringLiteral("Permission denied: %1").arg(omfRootPath));
		return {};
	}

	QStringList folders{omfRootPath};
	for (const QFileInfo &child : QDir(omfRootPath).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
		if (AvidMediaLayout::isOmfWorkstationFolderName(child.fileName()))
			folders.append(child.absoluteFilePath());

	QVector<MediaFile> files;
	int completed = 0;
	for (const QString &folder : folders)
	{
		if (m_job.isCancelled())
			break;
		ScanTask task;
		task.family = AvidMediaLayout::Family::Omf;
		task.folderPath = folder;
		task.folderNumber = QFileInfo(folder).fileName();
		task.volumeName = volumeName;
		task.volumePath = volumePath;
		auto result = processFolderTask(task);
		for (const auto &msg : result.logs)
			emitLog(msg.level, msg.module, msg.message);
		files.append(result.files);
		emit scanProgress(++completed, folders.size(), folder);
	}
	return files;
}

// MARK: - MXF root: parallel per-folder scan

QVector<MediaFile> MediaScanner::scanMxfRoot(const QString &mxfRootPath, const QString &volumeName,
											 const QString &volumePath)
{
	if (!AvidMediaLayout::isMxfRoot(mxfRootPath) ||
		!AvidMediaLayout::isMxfRoot(scannerFolderKey(mxfRootPath)))
		return {};
	QVector<MediaFile> files;
	QDir mxfDir(mxfRootPath);

	if (!canReadPath(mxfRootPath))
	{
		emitLog(QtCriticalMsg, QStringLiteral("scanner"), QStringLiteral("  Permission denied: %1").arg(mxfRootPath));
		return files;
	}

	QStringList subFolders = mxfDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

	QList<MediaScanner::ScanTask> tasks;
	for (const QString &folder : subFolders)
	{
		if (m_job.isCancelled())
			break;

		// Numbered media folders and Quarantined Files are the known MXF locations.
		const QString folderPath = mxfDir.filePath(folder);
		if (!AvidMediaLayout::locateMediaFolder(folderPath))
			continue;

		if (!canReadPath(folderPath))
		{
			emitLog(QtWarningMsg, QStringLiteral("scanner"), QStringLiteral("  Permission denied: %1").arg(folder));
			continue;
		}

		MediaScanner::ScanTask t;
		t.family = AvidMediaLayout::Family::Mxf;
		t.folderPath = folderPath;
		t.folderNumber = folder;
		t.volumeName = volumeName;
		t.volumePath = volumePath;
		tasks.append(t);
	}

	qCInfo(lcScanner).noquote() << tasks.size() << "subfolders queued for concurrent scanning in" << mxfRootPath;

	std::atomic<int> completedFolders{0};
	const int totalFolders = tasks.size();

	// ~30 Hz emit cap when folders finish quickly.
	ProgressThrottle throttle;

	// The parallel map preserves input order, so buffered logs
	// replay in scan order.
	QFuture<MediaScanner::FolderResult> future =
		QtConcurrent::mapped(tasks,
							 [this, &completedFolders, totalFolders, &throttle](const MediaScanner::ScanTask &t)
							 {
								 auto res = this->processFolderTask(t);
								 int done = ++completedFolders;

								 // No-op unless a test armed the seam.
								 TestPause::sleepMs(TestPause::kPerScannedFolderMs);

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

MediaScanner::FolderResult MediaScanner::processFolderTask(const ScanTask &task)
{
	FolderResult result;

	if (m_job.isCancelled())
		return result;
	const auto requested = AvidMediaLayout::locateMediaFolder(task.folderPath);
	if (!requested || requested->family != task.family)
		return result;
	const bool isQuarantineFolder = requested->isQuarantined;
	{
		const QString key = scannerFolderKey(task.folderPath);
		// Also cover UME folders reached through a link beneath a supported root.
		if (AvidMediaLayout::isInsideUmeRoot(task.folderPath) || AvidMediaLayout::isInsideUmeRoot(key))
			return result;
		if (!m_options.includeOmf && (isInsideOmfRoot(task.folderPath) || isInsideOmfRoot(key)))
			return result;
		const auto actual = AvidMediaLayout::locateMediaFolder(key);
		if (!actual || actual->family != task.family || actual->isQuarantined != isQuarantineFolder)
			return result;
		QMutexLocker lock(&m_mdbMapsMutex);
		if (m_seenFolders.contains(key))
			return result;
		m_seenFolders.insert(key);
	}

	// Buffer logs in the result instead of emitting from pool
	// threads. The orchestrator replays them in input order so
	// the console stays deterministic.
	auto bufLog = [&result](QtMsgType level, const QString &module, const QString &msg)
	{ result.logs.append({level, module, msg}); };

	// MARK: Parse the databases

	// Missing PMR/MDB is normal in Interplay environments.
	FolderDatabases dbs = readFolderDatabases(task, result.logs);
	const PmrIndex &pmrMap = dbs.pmr;
	MdbDatabase &mdb = dbs.mdb;

	// MARK: Folder database status

	// The status a file gets when this folder's PMR does NOT name it. The PMR
	// is the index of online files: if it exists and parsed, a file it omits
	// is a real miss ("No reference"). An unreadable database could have
	// listed anything, so nothing unmatched here can be called a miss; and
	// with no PMR at all there is no index to miss from.
	MediaFile::DbStatus folderStatus = MediaFile::DbStatus::NoReference;
	if ((dbs.pmrExists && !dbs.pmrOk) || (dbs.mdbExists && !dbs.mdbOk))
		folderStatus = MediaFile::DbStatus::DbUnreadable;
	else if (!dbs.pmrExists)
		folderStatus = MediaFile::DbStatus::NoDatabase;

	// MARK: Enumerate files in this folder

	// Managed media folders are flat, including Quarantined Files.
	const QDir folder(task.folderPath);
	const QFileInfoList entries = folder.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks);

	// Avid's own name for the folder it moves unreadable media into. Decided
	// once here; every row from this folder is stamped isQuarantined below,
	// and the table's Quarantined filter reads that flag.
	if (isQuarantineFolder)
	{
		int mxfCount = 0;
		for (const QFileInfo &entry : entries)
		{
			if (Conventions::countsAsEssenceName(entry.fileName()))
				++mxfCount;
		}

		if (mxfCount > 0)
			bufLog(QtWarningMsg, QStringLiteral("scanner"),
				   QStringLiteral("⚠️ Avid Quarantined Files folder on %1 contains %2 MXF file(s)!")
					   .arg(task.volumeName)
					   .arg(mxfCount));
	}

	// MARK: Build a MediaFile for each entry

	for (const QFileInfo &entry : entries)
	{
		if (m_job.isCancelled())
			break;

		const QString fileName = entry.fileName();
		// The managed tree selects the family; a cheap suffix check keeps
		// a misplaced file from entering another family's operations.
		if (!AvidMediaLayout::acceptsFileName(task.family, fileName))
			continue;
		if (task.family == AvidMediaLayout::Family::Omf && !m_options.includeOmf)
			continue;

		MediaFile mf = buildMediaFile(entry, task.volumeName, task.volumePath, task.folderNumber, task.family, pmrMap, mdb,
									  folderStatus);
		mf.isQuarantined = isQuarantineFolder;

		result.files.append(mf);
	}

	if (task.family == AvidMediaLayout::Family::Mxf && result.files.size() > Conventions::kFolderWarn)
	{
		// Don't warn per-folder; N pool threads firing would bury
		// the progress logs. Stash the (folder, count) and let
		// doScan emit one summary at the end.
		QMutexLocker lock(&m_overfullMutex);
		m_overfullFolders.append(
			{task.volumeName + QLatin1Char('/') + task.folderNumber, int(result.files.size())});
	}

	// Cache the clip records for pass 2's UMID re-join — only the masters;
	// the per-file essence is consumed above and dropped. Move because this
	// task is done with it. Skip empties as nothing to join.
	// Keep case-sensitive share directories distinct in both passes.
	if (!mdb.masters.isEmpty())
	{
		QMutexLocker lock(&m_mdbMapsMutex);
		m_mdbMapsByFolder.insert(scannerFolderKey(task.folderPath), std::move(mdb.masters));
	}

	return result;
}

// MARK: - Per-folder databases

MediaScanner::FolderDatabases MediaScanner::readFolderDatabases(const ScanTask &task, QVector<LogMsg> &logs)
{
	FolderDatabases dbs;
	auto bufLog = [&logs](QtMsgType level, const QString &module, const QString &msg)
	{ logs.append({level, module, msg}); };

	// Read msm* first. Its parse failure affects the folder status even if
	// ama* succeeds; an ama* failure is ignored when msm* already succeeded.
	const auto isPrimary = [](QLatin1String name, const auto &names)
	{ return name == names[0]; };

	// MARK: The PMRs

	// Every spelling present is read; entries for one filename append, so a
	// file both index files name keeps its msm* record first.
	bool anyPmrOk = false;
	for (const QLatin1String name : Conventions::kPmrFileNames)
	{
		const QString pmrPath = task.folderPath + QLatin1Char('/') + name;
		if (!QFile::exists(pmrPath))
			continue;
		dbs.pmrExists = true;
		const bool primary = isPrimary(name, Conventions::kPmrFileNames);

		bool ok = true;
		const PmrIndex index = PmrParser::buildFileMap(pmrPath, &ok);
		if (ok)
		{
			anyPmrOk = true;
			qCInfo(lcPmr).noquote() << pmrPath << ':' << index.size() << "file entries";
			for (auto it = index.constBegin(); it != index.constEnd(); ++it)
				dbs.pmr[it.key()].append(it.value());
		}
		else if (primary || !anyPmrOk)
		{
			dbs.pmrOk = false;
			bufLog(QtWarningMsg, QStringLiteral("scanner"),
				   QStringLiteral("  %1 in /%2 is unreadable; unmatched files here "
								  "surface as 'No database', not 'No reference'")
					   .arg(name)
					   .arg(task.folderNumber));
		}
		else
		{
			bufLog(QtInfoMsg, QStringLiteral("scanner"),
				   QStringLiteral("  %1 in /%2 is unreadable; ignored, the msmFMID.pmr index stands")
					   .arg(name)
					   .arg(task.folderNumber));
		}
	}
	// MARK: The MDBs

	// Records insert only when the mob is new, so the msm* database — read
	// first — is the one that describes a mob both spellings carry.
	bool anyMdbOk = false;
	for (const QLatin1String name : Conventions::kMdbFileNames)
	{
		const QString mdbPath = task.folderPath + QLatin1Char('/') + name;
		if (!QFile::exists(mdbPath))
			continue;
		dbs.mdbExists = true;
		const bool primary = isPrimary(name, Conventions::kMdbFileNames);

		bool ok = true;
		MdbDatabase db = MdbParser::load(mdbPath, &ok);
		if (ok)
		{
			anyMdbOk = true;
			qCInfo(lcMdb).noquote() << mdbPath << ':' << db.masters.size() << "clips," << db.files.size() << "files";
			if (dbs.mdb.isEmpty())
			{
				dbs.mdb = std::move(db);
			}
			else
			{
				for (auto it = db.masters.constBegin(); it != db.masters.constEnd(); ++it)
					if (!dbs.mdb.masters.contains(it.key()))
						dbs.mdb.masters.insert(it.key(), it.value());
				for (auto it = db.files.constBegin(); it != db.files.constEnd(); ++it)
					if (!dbs.mdb.files.contains(it.key()))
						dbs.mdb.files.insert(it.key(), it.value());
			}
		}
		else if (primary || !anyMdbOk)
		{
			dbs.mdbOk = false;
			bufLog(QtWarningMsg, QStringLiteral("scanner"),
				   QStringLiteral("  %1 in /%2 is unreadable; unmatched files here "
								  "surface as 'No database', not 'No reference'")
					   .arg(name)
					   .arg(task.folderNumber));
		}
		else
		{
			bufLog(QtInfoMsg, QStringLiteral("scanner"),
				   QStringLiteral("  %1 in /%2 is unreadable; ignored, the msmMMOB.mdb records stand")
					   .arg(name)
					   .arg(task.folderNumber));
		}
	}
	QStringList missingDatabases;
	if (!dbs.pmrExists)
		missingDatabases.append(QStringLiteral("PMR"));
	if (!dbs.mdbExists)
		missingDatabases.append(QStringLiteral("MDB"));
	if (!missingDatabases.isEmpty())
		qCInfo(lcScanner).noquote() << QStringLiteral("Missing databases in %1: %2")
										   .arg(task.folderPath, missingDatabases.join(QStringLiteral(", ")));

	return dbs;
}

// MARK: - MediaFile assembly (database pass)

MediaFile MediaScanner::buildMediaFile(const QFileInfo &fi, const QString &volumeName,
									   const QString &volumePath, const QString &folderNumber,
									   AvidMediaLayout::Family family,
									   const PmrIndex &pmrMap,
									   const MdbDatabase &mdb,
									   MediaFile::DbStatus folderStatus)
{
	// `fi` is the directory listing's own entry — its size and times are
	// already known, so nothing here stats the file again.
	MediaFile mf;
	mf.filePath = fi.filePath();
	mf.fileName = fi.fileName();
	mf.volumeName = volumeName;
	mf.volumePath = volumePath;
	mf.mediaFolderName = folderNumber;
	mf.omfEra = family == AvidMediaLayout::Family::Omf;

	// MARK: File-level metadata

	mf.sizeBytes = fi.size();
	// Preserve an unknown creation time; modification time is a different fact.
	mf.created = fi.birthTime();
	mf.modified = fi.lastModified();
	// Names come from media metadata or MDB, never from the filename.
	mf.isNonPortable = isNonPortableFilename(mf.fileName);

	// MARK: PMR lookup

	const QString primaryKey = PmrKey::primary(mf.fileName);

	const PmrEntry *pmrHit = nullptr;
	auto applyPmrHit = [&mf, &pmrHit](const PmrEntry &pmr)
	{
		pmrHit = &pmr;
		mf.project = pmr.project;
		mf.mobId = pmr.mobId;
		mf.masterMobId = pmr.masterMobId;
	};

	// The PMR records the on-disk filename verbatim; an exact match is the
	// only match there is (see PmrIndex).
	const auto pmrIt = pmrMap.constFind(primaryKey);
	if (pmrIt != pmrMap.constEnd() && !pmrIt->isEmpty())
		applyPmrHit(pmrIt->first());

	// The PMR v1 contains no embedded master/project. Recover a master only
	// when the MDB's source-reference graph establishes a unique relationship.
	const auto fileIt = mf.mobId.isEmpty() ? mdb.files.constEnd() : mdb.files.constFind(mf.mobId);
	if (mf.masterMobId.isEmpty() && fileIt != mdb.files.constEnd())
		mf.masterMobId = fileIt->masterMobId;
	const auto masterIt = mf.masterMobId.isEmpty() ? mdb.masters.constEnd() : mdb.masters.constFind(mf.masterMobId);
	if (masterIt != mdb.masters.constEnd())
	{
		applyMdbRecord(mf, masterIt.value());
		assignIfMissing(mf.project, masterIt->project);
	}
	if (fileIt != mdb.files.constEnd())
		assignIfMissing(mf.project, fileIt->project);

	// Missing timestamps leave database freshness unknown, so the media must
	// be checked rather than relying on the database alone.
	const bool headerReadable = mf.sizeBytes > 0;
	const bool described = fileIt != mdb.files.constEnd() && fileIt->essenceComplete &&
						   masterIt != mdb.masters.constEnd();
	const bool indexedFileCurrent = pmrHit && pmrHit->fileModifiedSecs != 0 &&
									PmrParser::trailerMatchesModified(pmrHit->fileModifiedSecs, fi.lastModified());
	mf.databaseMetadataCurrent = described && indexedFileCurrent;
	if (headerReadable && mf.databaseMetadataCurrent)
	{
		MediaMetadata essence = fileIt->essence;
		essence.isPrecompute = AvidUsage::masterClassification(masterIt->usageCode) ==
							   AvidUsage::Classification::Precompute;
		essence.classificationKnown = masterIt->classificationKnown;
		essence.precomputeCategory = masterIt->precomputeCategory;
		applyMetadata(mf, essence);
	}
	mf.needsHeaderRead = headerReadable && (!mf.databaseMetadataCurrent ||
											mf.project.isEmpty() || mf.masterMobId.isEmpty() || mf.type == MediaFile::Type::Unknown ||
											(mf.type == MediaFile::Type::Precompute && mf.precomputeCategory == MediaFile::PrecomputeCategory::Unknown));

	// An all-zero MOB ID means Avid never wrote a real identity for the file
	// or its clip; the media can't be tracked or relinked reliably.
	mf.isInvalidUmid = MobId::isAllZero(mf.mobId) || MobId::isAllZero(mf.masterMobId);

	// MARK: Local-database status

	// PMR membership remains separate from metadata recovered through MDB or
	// the file itself. A recovered project/name does not make the file listed.
	mf.dbStatus = pmrHit ? MediaFile::DbStatus::Listed : folderStatus;

	return mf;
}

// MARK: - Header pass (pass 2)

namespace
{
	// A reused filename must not inherit metadata from the old media. Keep
	// filesystem facts and the folder's PMR membership untouched.
	void clearReplacedMetadata(MediaFile &mf)
	{
		mf.project.clear();
		mf.mobId.clear();
		mf.masterMobId.clear();
		mf.clipName.clear();
		mf.clipNameSource = MediaFile::ClipNameSource::None;
		mf.originalBin.clear();
		mf.sourceFilePath.clear();
		mf.sourceFileName.clear();
		mf.sourceContainer.clear();
		mf.isImported = false;
		mf.codec.clear();
		mf.resolution.clear();
		mf.fps.clear();
		mf.bitDepth.clear();
		mf.sampleRate = 0;
		mf.channels = 0;
		mf.durationFrames = 0;
		mf.timecodeBase = 0;
		mf.dropFrame = false;
		mf.kind = MediaFile::Kind::Unknown;
		mf.type = MediaFile::Type::Unknown;
		mf.precomputeCategory = MediaFile::PrecomputeCategory::Unknown;
		mf.databaseMetadataCurrent = false;
	}

	const MdbMasterMob *findHeaderMaster(const QString &id, bool readingOmf,
										 const QHash<QString, MdbMasterMob> *masters)
	{
		if (!masters)
			return nullptr;
		auto record = masters->constFind(id);
		// MXF permits the PMR byte-order alias. OMF IDs already have the
		// database representation; swapping them would identify different media.
		if (record == masters->constEnd() && !readingOmf)
		{
			const QString swapped = MobId::toPmrForm(id);
			if (!swapped.isEmpty())
				record = masters->constFind(swapped);
		}
		return record == masters->constEnd() ? nullptr : &record.value();
	}

	// Owns only this row. Scheduling, cancellation and progress stay with
	// MediaScanner; the database records remain read-only throughout pass 2.
	void readMediaHeader(MediaFile &mf, AvidMediaLayout::Family family,
						 const QHash<QString, MdbMasterMob> *masters)
	{
		const bool readingOmf = family == AvidMediaLayout::Family::Omf;
		const auto databaseCategory = mf.precomputeCategory;
		MediaMetadata metadata;
		QString headerBin;
		bool omfIdentityKnown = false;
		if (readingOmf)
		{
			// OMF1/OMF2 return the same essence fields, with the master
			// bin and file identity obtained from their object graph.
			const OmfMetadata omf = OmfParser::parseHeader(mf.filePath);
			omfIdentityKnown = omf.hasMediaDescriptor;
			metadata = omf.essence;
			headerBin = omf.bin;
			metadata.fileMobId = omf.fileMobId;
		}
		else
		{
			metadata = MxfParser::parseHeader(mf.filePath);
		}
		const bool headerUsable = metadata.valid || metadata.classificationKnown;
		const auto canonicalHeaderId = [&](const QString &id)
		{
			if (readingOmf || id.isEmpty())
				return id;
			const QString canonical = MobId::toPmrForm(id);
			return canonical.isEmpty() ? id : canonical;
		};
		// A selected OMF file mob can prove identity even when its
		// descriptor lacks usable technical fields. A different old
		// file's database details must still be invalidated in that case.
		const QString headerFileId = headerUsable || omfIdentityKnown ? canonicalHeaderId(metadata.fileMobId) : QString{};
		const bool headerMasterKnown = readingOmf || metadata.hasMaterialPackage;
		const QString headerMasterId = headerUsable && headerMasterKnown ? canonicalHeaderId(metadata.umid) : QString{};
		const auto contradicts = [](const QString &oldId, const QString &actualId)
		{
			return !oldId.isEmpty() && !actualId.isEmpty() && !MobId::isAllZero(actualId) && oldId != actualId;
		};
		if (contradicts(mf.mobId, headerFileId) || contradicts(mf.masterMobId, headerMasterId))
		{
			// The name was reused for different media. None of the old
			// clip's editorial/technical fields belongs to the replacement.
			clearReplacedMetadata(mf);
		}
		assignIfMissing(mf.mobId, headerFileId);
		if (headerUsable)
		{
			assignIfMissing(mf.masterMobId, headerMasterId);
			assignIfMissing(mf.originalBin, headerBin);
		}
		applyMetadata(mf, metadata);
		// Current sources for the same identity must agree. Do not let the
		// later MDB name/bin re-join restore a disputed category. A stale
		// database (or one for replaced media) has no say in this decision.
		if (mf.databaseMetadataCurrent && metadata.classificationKnown && metadata.isPrecompute &&
			databaseCategory != MediaFile::PrecomputeCategory::Unknown &&
			metadata.precomputeCategory != MediaFile::PrecomputeCategory::Unknown &&
			databaseCategory != metadata.precomputeCategory)
			mf.precomputeCategory = MediaFile::PrecomputeCategory::Unknown;

		// Fill a project still missing after the PMR/MDB pass from usable
		// media metadata; preserve an existing database value.
		if (headerUsable && mf.project.isEmpty())
			mf.project = metadata.projectName;

		// Recover names by the header's master identity without changing the
		// row's PMR membership status.
		if (headerUsable && headerMasterKnown && !metadata.umid.isEmpty() && !MobId::isAllZero(metadata.umid))
		{
			if (const auto *record = findHeaderMaster(metadata.umid, readingOmf, masters))
			{
				applyMdbRecord(mf, *record);
				if (!record->mobIdHex.isEmpty())
					mf.masterMobId = record->mobIdHex;
			}
		}
		// The header's own identity can be the zero one too.
		mf.isInvalidUmid = MobId::isAllZero(mf.mobId) || MobId::isAllZero(mf.masterMobId) ||
						   (headerUsable && (MobId::isAllZero(metadata.umid) || MobId::isAllZero(metadata.fileMobId)));
	}
} // namespace

void MediaScanner::readMediaHeadersConcurrently(QVector<MediaFile> &files)
{
	// Pass 1 records which files need a header read for incomplete/stale
	// database facts or missing identity. Resolve a case-preserving cache
	// key once per folder.
	struct HeaderRow
	{
		int index;
		QString folderKey;
		AvidMediaLayout::Family family;
	};
	QVector<HeaderRow> rows;
	rows.reserve(files.size() / 4);
	QHash<QString, QString> folderKeyCache;
	int omfRows = 0;
	for (int i = 0; i < files.size(); ++i)
	{
		const MediaFile &f = files[i];
		// A row needs its header when the databases left it without technical
		// facts — or without a project name, which the header also carries.
		const bool omfCandidate = f.omfEra;
		if (!f.needsHeaderRead)
			continue;
		const QString rawFolder = QFileInfo(f.filePath).absolutePath();
		auto cacheIt = folderKeyCache.find(rawFolder);
		if (cacheIt == folderKeyCache.end())
			cacheIt = folderKeyCache.insert(rawFolder, scannerFolderKey(rawFolder));
		rows.append({i, cacheIt.value(), omfCandidate ? AvidMediaLayout::Family::Omf : AvidMediaLayout::Family::Mxf});
		if (omfCandidate)
			++omfRows;
	}
	if (rows.isEmpty())
		return;

	const int total = rows.size();
	const int mxfRows = total - omfRows;
	if (omfRows == 0)
		qCInfo(lcScanner).noquote()
			<< QStringLiteral("Reading MXF headers for %1 file(s) needing metadata verification").arg(total);
	else
		qCInfo(lcScanner).noquote()
			<< QStringLiteral("Reading MXF/OMF headers for %1 file(s) needing metadata verification (%2 MXF, %3 OMF)")
				   .arg(total)
				   .arg(mxfRows)
				   .arg(omfRows);
	emit scanProgress(0, total, {});

	std::atomic<int> done{0};
	ProgressThrottle throttle;

	// Pass 1 has joined, so nobody writes the cache any more: plain
	// concurrent reads below, no lock.
	const QHash<QString, QHash<QString, MdbMasterMob>> &clipsByFolder = m_mdbMapsByFolder;

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
			const auto folder = clipsByFolder.constFind(row.folderKey);
			readMediaHeader(mf, row.family, folder == clipsByFolder.constEnd() ? nullptr : &folder.value());

			const int n = ++done;
			if (n == total || throttle.shouldEmit())
				emit scanProgress(n, total, mf.fileName);
		});
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
