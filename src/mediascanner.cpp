#include "mediascanner.h"
#include "avideffects.h"
#include "conventions.h"
#include "testpause.h"
#include "logcategories.h"
#include "mobid.h"
#include "mxfparser.h"
#include "omfparser.h" // OMF-era: the Bento-tail twin of MxfParser for legacy essence
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
#include <QSet>
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
	/// OMF-era: a row is legacy essence — worth a Bento-tail read, and
	/// counted as header-readable — only when it sits where Avid writes
	/// legacy essence: the flat OMFI MediaFiles root (the scanner names
	/// that folder Conventions::kOmfMediaFilesDir whichever way it was
	/// reached). A stray .wav/.aif in an MXF-era numbered folder is listed
	/// and never opened, exactly as before OMF support: no per-scan tail
	/// read on a share, and the folder's coverage and pass-2 console lines
	/// stay byte-identical for MXF-era folders.
	bool isOmfEraRow(QStringView extension, QStringView folderName)
	{
		return Conventions::hasOmfEraExtension(extension) && Conventions::isOmfRootName(folderName);
	}

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
	// The bin has no other source in the app: PmrEntry carries a project but
	// no bin, and AvbParser yields only MOB IDs for the Bin Filter. Unknown
	// therefore means blank, never a guess.
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
			emitLog(QtWarningMsg, QStringLiteral("scanner"),
					"Grant Full Disk Access in System Preferences > Privacy & "
					"Security");
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
		const AvidEffects::Hit hit = AvidEffects::lookup(f.clipName);
		f.effect = hit.name;
		f.effectCategory = hit.category;
		f.effectSequence = hit.sequence;
		f.effectInstance = hit.instance;
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
				QStringLiteral("%1 file%2 with no project name anywhere").arg(noProject).arg(noProject == 1 ? "" : "s"));
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

	const QString mxfViaRoot = Conventions::mxfRootUnder(volumePath);
	if (QDir(mxfViaRoot).exists())
	{
		emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("  Found Avid MediaFiles/MXF"));
		files.append(scanMxfRoot(mxfViaRoot, volumeName, volumePath));
	}

	// OMF-era: the legacy root is a sibling of Avid MediaFiles, and a drive
	// may carry either or both.
	const QString omfViaRoot = Conventions::omfRootUnder(volumePath);
	if (QDir(omfViaRoot).exists())
	{
		emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("  Found OMFI MediaFiles"));
		files.append(scanOmfRoot(omfViaRoot, volumeName, volumePath));
	}

	if (files.isEmpty())
	{
		// Both names, so a miss on a drive with media buried deeper is
		// visible for what it is rather than read as "no media" — and the
		// way to reach that media is named, since a volume scan will not.
		emitLog(QtWarningMsg, QStringLiteral("scanner"),
				QStringLiteral("  No Avid MediaFiles or OMFI MediaFiles at the root of %1 "
							   "(media in a subfolder is found via File > Add Folder or Volume)")
					.arg(volumeName));
	}

	return files;
}

// MARK: - Hand-added folder: shape cases + two-level search

QVector<MediaFile> MediaScanner::scanAddedFolder(const QString &folderPath, const QString &volumeName)
{
	QVector<MediaFile> files;
	QDir dir(folderPath);
	const QString dirName = dir.dirName();

	// MARK: Case 1 — Folder itself holds Avid MediaFiles/MXF (or OMFI MediaFiles)

	// The root shape, decided first and alone: what the volume scan probes,
	// found here with no directory listing and no deeper search. A nested
	// root under a folder that already has one at the top is deliberately
	// not looked for — that was the rule before the two-level search was
	// ever added, and it keeps a hand-added drive root scanning exactly as
	// the ticked volume does.
	const QString mxfViaRoot = Conventions::mxfRootUnder(folderPath);
	if (QDir(mxfViaRoot).exists())
	{
		emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("  Found Avid MediaFiles/MXF"));
		files.append(scanMxfRoot(mxfViaRoot, volumeName, folderPath));
	}
	// OMF-era: the legacy root is a sibling at the same level; a folder may
	// carry either or both, as a drive root may.
	const QString omfViaRoot = Conventions::omfRootUnder(folderPath);
	if (QDir(omfViaRoot).exists())
	{
		emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("  Found OMFI MediaFiles"));
		files.append(scanOmfRoot(omfViaRoot, volumeName, folderPath));
	}
	if (!files.isEmpty())
		return files;

	// MARK: Case 2 — Path is somewhere inside an Avid MediaFiles directory

	const int avidIdx = folderPath.indexOf(Conventions::kAvidMediaFilesDir, 0, Qt::CaseInsensitive);
	if (avidIdx >= 0)
	{
		const QString avidPart = folderPath.left(avidIdx + Conventions::kAvidMediaFilesDir.size());
		const QString mxfInside = avidPart + "/MXF";
		if (QDir(mxfInside).exists())
		{
			emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("  Found MXF folder at %1").arg(mxfInside));
			return scanMxfRoot(mxfInside, volumeName, avidPart);
		}

		if (Conventions::isMxfRootName(dirName))
		{
			emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("  Pointed directly at MXF folder"));
			return scanMxfRoot(folderPath, volumeName, QFileInfo(folderPath).absolutePath());
		}
	}

	// MARK: Case 3 — Path itself is an OMF root or an MXF root

	// OMF-era: decided by the folder's NAME, before the MXF branch below.
	// The OMF root is flat, so it must never be handed to scanMxfRoot —
	// which would walk its subfolders (Avid's transient `Creating`, if
	// present) and skip the media sitting at the top level.
	if (Conventions::isOmfRootName(dirName))
	{
		emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("  Pointed directly at OMFI MediaFiles"));
		return scanOmfRoot(folderPath, volumeName, QFileInfo(folderPath).absolutePath());
	}

	if (Conventions::isMxfRootName(dirName))
	{
		const QStringList subs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
		if (!subs.isEmpty())
		{
			emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("  MXF folder with %1 subfolders").arg(subs.size()));
			return scanMxfRoot(folderPath, volumeName, QFileInfo(folderPath).absolutePath());
		}
	}

	// MARK: Case 4 — Single media folder with per-folder databases

	const auto hasAnyDatabase = [&folderPath]
	{
		for (const QLatin1String name : Conventions::kPmrFileNames)
			if (QFile::exists(folderPath + QLatin1Char('/') + name))
				return true;
		for (const QLatin1String name : Conventions::kMdbFileNames)
			if (QFile::exists(folderPath + QLatin1Char('/') + name))
				return true;
		return false;
	};
	if (hasAnyDatabase())
	{
		emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("  Found database files in folder"));

		MediaScanner::ScanTask t;
		t.folderPath = folderPath;
		t.folderNumber = dir.dirName();
		t.volumeName = volumeName;
		t.volumePath = QFileInfo(folderPath).absolutePath();

		auto result = processFolderTask(t);
		for (const auto &msg : result.logs)
			emitLog(msg.level, msg.module, msg.message);
		return result.files;
	}

	// MARK: Case 5 — Deep search (two levels)

	// Two levels covers the usual `~/Documents/Project/Avid MediaFiles`
	// layout without scanning the entire volume.
	emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("  Searching for Avid MediaFiles in %1...").arg(volumeName));

	QStringList searchDirs = {folderPath};
	for (const QString &sub1 : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
	{
		if (m_job.isCancelled())
			break;
		QString path1 = folderPath + "/" + sub1;
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
		QString candidate = Conventions::mxfRootUnder(searchDir);
		if (QDir(candidate).exists())
		{
			emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("  Found Avid media at %1").arg(candidate));
			auto subFiles = scanMxfRoot(candidate, volumeName, searchDir);
			files.append(subFiles);
		}
		// OMF-era: the legacy root is probed beside the MXF one at every
		// level of the search.
		const QString omfCandidate = Conventions::omfRootUnder(searchDir);
		if (QDir(omfCandidate).exists())
		{
			emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("  Found Avid media at %1").arg(omfCandidate));
			files.append(scanOmfRoot(omfCandidate, volumeName, searchDir));
		}
	}

	if (files.isEmpty())
	{
		emitLog(QtWarningMsg, QStringLiteral("scanner"), QStringLiteral("  No Avid MediaFiles found in %1").arg(volumeName));
	}

	return files;
}

// MARK: - OMF-era root: one flat folder

QVector<MediaFile> MediaScanner::scanOmfRoot(const QString &omfRootPath, const QString &volumeName,
											 const QString &volumePath)
{
	// OMF-era: the Case-4 shape (one folder, its databases beside the
	// media) applied to the root itself. processFolderTask enumerates files
	// only, so the `Creating` subfolder never enters the listing.
	if (!canReadPath(omfRootPath))
	{
		emitLog(QtCriticalMsg, QStringLiteral("scanner"), QStringLiteral("  Permission denied: %1").arg(omfRootPath));
		return {};
	}

	MediaScanner::ScanTask t;
	t.folderPath = omfRootPath;
	t.folderNumber = Conventions::kOmfMediaFilesDir;
	t.volumeName = volumeName;
	t.volumePath = volumePath;

	auto result = processFolderTask(t);
	for (const auto &msg : result.logs)
		emitLog(msg.level, msg.module, msg.message);
	emit scanProgress(1, 1, omfRootPath);
	return result.files;
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

	QList<MediaScanner::ScanTask> tasks;
	for (const QString &folder : subFolders)
	{
		if (m_job.isCancelled())
			break;

		// Avid's staging folder: half-written captures that will be renamed
		// into a real folder when the capture finishes. Not media, not a
		// folder that counts (the rebalancer already leaves it alone).
		if (Conventions::isCreatingFolderName(folder))
			continue;

		QString folderPath = mxfDir.filePath(folder);

		if (!canReadPath(folderPath))
		{
			emitLog(QtWarningMsg, QStringLiteral("scanner"), QStringLiteral("  Permission denied: %1").arg(folder));
			continue;
		}

		MediaScanner::ScanTask t;
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
			if (Conventions::countsAsEssenceName(fi.fileName()))
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
		// Only Avid media appears in the table: .mxf, plus (OMF-era: the
		// legacy essence set, .omf/.aif/.wav/.sd2, admitted by the one gate
		// in Conventions::hasAvidMediaExtension). Everything else — the msm
		// databases, OS junk, stray exports, AppleDouble "._clip.mxf" twins
		// — is invisible to the table, the counts, and every media operation.
		if (!Conventions::isAvidMediaName(fileName))
			continue;

		MediaFile mf = buildMediaFile(entry, task.volumeName, task.volumePath, task.folderNumber, pmrMap, mdb,
									  folderStatus, tally);
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

	if (result.files.size() > Conventions::kFolderWarn)
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
	// PathKey::normalise keeps both passes agreeing on folder identity
	// across different Qt path APIs.
	if (!mdb.masters.isEmpty())
	{
		QMutexLocker lock(&m_mdbMapsMutex);
		m_mdbMapsByFolder.insert(PathKey::normalise(task.folderPath), std::move(mdb.masters));
	}

	return result;
}

// MARK: - Per-folder databases

MediaScanner::FolderDatabases MediaScanner::readFolderDatabases(const ScanTask &task, QVector<LogMsg> &logs)
{
	FolderDatabases dbs;
	auto bufLog = [&logs](QtMsgType level, const QString &module, const QString &msg)
	{ logs.append({level, module, msg}); };

	// The msm* spelling is the primary of each kind: it keeps the console
	// line it always had ("PMR:" / "MDB:"), and it alone decides the
	// folder's verdict. An ama* twin that fails to parse while its msm*
	// sibling read fine is logged and ignored — the folder's index still
	// stands, so an unmatched row is a real miss, exactly as it was before
	// the twins were read at all. A twin fails the folder only when it is
	// the only file of its kind that was there.
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
			bufLog(QtInfoMsg, QStringLiteral("pmr"),
				   QStringLiteral("  %1: %2 file entries in /%3")
					   .arg(primary ? QStringLiteral("PMR") : QString(name))
					   .arg(index.size())
					   .arg(task.folderNumber));
			for (auto it = index.constBegin(); it != index.constEnd(); ++it)
				dbs.pmr[it.key()].append(it.value());
		}
		else if (primary || !anyPmrOk)
		{
			dbs.pmrOk = false;
			bufLog(QtWarningMsg, QStringLiteral("pmr"),
				   QStringLiteral("  %1 in /%2 is unreadable; unmatched files here "
								  "surface as 'No database', not 'No reference'")
					   .arg(name)
					   .arg(task.folderNumber));
		}
		else
		{
			bufLog(QtInfoMsg, QStringLiteral("pmr"),
				   QStringLiteral("  %1 in /%2 is unreadable; ignored, the msmFMID.pmr index stands")
					   .arg(name)
					   .arg(task.folderNumber));
		}
	}
	if (!dbs.pmrExists)
		bufLog(QtInfoMsg, QStringLiteral("pmr"), QStringLiteral("  No msmFMID.pmr in /%1").arg(task.folderNumber));

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
			bufLog(QtInfoMsg, QStringLiteral("mdb"),
				   QStringLiteral("  %1: %2 clips, %3 files in /%4")
					   .arg(primary ? QStringLiteral("MDB") : QString(name))
					   .arg(db.masters.size())
					   .arg(db.files.size())
					   .arg(task.folderNumber));
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
			bufLog(QtWarningMsg, QStringLiteral("mdb"),
				   QStringLiteral("  %1 in /%2 is unreadable; unmatched files here "
								  "surface as 'No database', not 'No reference'")
					   .arg(name)
					   .arg(task.folderNumber));
		}
		else
		{
			bufLog(QtInfoMsg, QStringLiteral("mdb"),
				   QStringLiteral("  %1 in /%2 is unreadable; ignored, the msmMMOB.mdb records stand")
					   .arg(name)
					   .arg(task.folderNumber));
		}
	}
	if (!dbs.mdbExists)
		bufLog(QtInfoMsg, QStringLiteral("mdb"), QStringLiteral("  No msmMMOB.mdb in /%1").arg(task.folderNumber));

	return dbs;
}

// MARK: - MediaFile assembly (Stage 1)

MediaFile MediaScanner::buildMediaFile(const QFileInfo &fi, const QString &volumeName,
									   const QString &volumePath, const QString &folderNumber,
									   const PmrIndex &pmrMap,
									   const MdbDatabase &mdb,
									   MediaFile::DbStatus folderStatus, CoverageTally &tally)
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

	// MARK: MDB lookup (the master clip's record)

	// The clip-level facts — name, bin, source, import flag — live on the
	// MASTER mob, the one the PMR's MASTER record names. The file mob's own
	// record is not consulted: its CPNT:Name is usually the source filename,
	// and reading it first used to put "Avid DNx SQ.mov" in the Clip Name
	// column on 67 of 795 corpus rows whenever the header went unread.
	// applyMdbRecord is the file-scope helper above; pass 1 and the pass-2
	// re-join both call it so the merge rules can't drift.
	const auto masterIt = mf.masterMobId.isEmpty() ? mdb.masters.constEnd() : mdb.masters.constFind(mf.masterMobId);
	if (masterIt != mdb.masters.constEnd())
		applyMdbRecord(mf, masterIt.value());

	// OMF-era: a version-2 PMR carries no project, so the row takes the
	// `_PJ` the MDB read from the file mob (or the source mob it points at)
	// — the same attribute OmfParser would read from the file itself, which
	// keeps a database-covered row out of the header pass. MXF-era rows are
	// left exactly as before: their PMR names the project, and the header
	// pass reads `_PJ` for any row still without one.
	const bool isOmfEra = isOmfEraRow(mf.extension, folderNumber);
	const auto fileIt = mf.mobId.isEmpty() ? mdb.files.constEnd() : mdb.files.constFind(mf.mobId);
	if (isOmfEra && fileIt != mdb.files.constEnd())
	{
		assignIfMissing(mf.project, fileIt->project);
		if (masterIt != mdb.masters.constEnd())
			assignIfMissing(mf.project, masterIt->project);
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
	// pass 2 picks up the rows that don't.
	//
	// OMF-era: a legacy essence file has a readable tail too (OmfParser),
	// so it is admitted to both branches on the same terms as an .mxf; the
	// sub-1 KB floor keeps stubs and plain RIFF fragments out.
	const bool headerReadable = (Conventions::hasMxfExtension(mf.extension) || isOmfEra) && mf.sizeBytes > 1024;
	if (headerReadable && pmrHit && !m_options.forceHeaderScan && !mdb.isEmpty())
	{
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
	else if (headerReadable)
	{
		++tally.header;
	}

	// An all-zero MOB ID means Avid never wrote a real identity for the file
	// or its clip; the media can't be tracked or relinked reliably.
	mf.isInvalidUmid = MobId::isAllZero(mf.mobId) || MobId::isAllZero(mf.masterMobId);

	// MARK: Local-database status

	// The PMR is the folder's index of online files: named there = Listed;
	// otherwise the row takes the folder's verdict computed above. The
	// project is a separate fact — whatever the PMR entry said, possibly
	// nothing — and pass 2 reads the header's own `_PJ` for any row still
	// without one (the same attribute Media Composer reads when it rebuilds
	// a PMR), so "No project" ends up meaning exactly that: nothing names one.
	mf.dbStatus = pmrHit ? MediaFile::DbStatus::Listed : folderStatus;

	return mf;
}

// MARK: - Header pass (pass 2)

void MediaScanner::parseMxfHeadersConcurrently(QVector<MediaFile> &files)
{
	// The rows the databases did not describe: an .mxf worth opening that
	// still has no codec (finalise always names one for essence the MDB
	// vouched for; a row without a database, or sent here by the staleness
	// guard or the Debug toggle, has none yet). Everything else — sub-1 KB
	// stubs, every database-covered row, stray audio in an MXF folder — is
	// left alone. OMF-era: the legacy essence rows (.omf/.aif/.wav/.sd2)
	// INSIDE an OMFI MediaFiles root are admitted on the same terms and
	// dispatched to OmfParser below (see isOmfEraRow); a plain RIFF file
	// that Avid never wrote costs one 24-byte tail read and yields nothing.
	// Each row
	// carries its folder's key so the re-join below can find that folder's
	// clip records; PathKey::normalise is a filesystem round-trip, so it
	// runs once per distinct folder, not once per file.
	struct HeaderRow
	{
		int index;
		QString folderKey;
		bool omfEra; ///< OMF-era: routes the row to OmfParser instead of MxfParser.
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
		const bool omfEra = isOmfEraRow(f.extension, f.mxfFolder); // OMF-era: admitted beside .mxf
		if ((!Conventions::hasMxfExtension(f.extension) && !omfEra) || f.sizeBytes <= 1024 ||
			(!f.codec.isEmpty() && !f.project.isEmpty()))
			continue;
		const QString rawFolder = QFileInfo(f.filePath).absolutePath();
		auto cacheIt = folderKeyCache.find(rawFolder);
		if (cacheIt == folderKeyCache.end())
			cacheIt = folderKeyCache.insert(rawFolder, PathKey::normalise(rawFolder));
		rows.append({i, cacheIt.value(), omfEra});
		if (omfEra)
			++omfRows;
	}
	if (rows.isEmpty())
		return;

	const int total = rows.size();
	const int mxfRows = total - omfRows;
	if (omfRows == 0)
		emitLog(QtInfoMsg, QStringLiteral("scanner"),
				QStringLiteral("Reading MXF headers for %1 file(s) the databases don't describe").arg(total));
	else // OMF-era: name both kinds so the console says what is being opened; the MXF-only line above is unchanged
		emitLog(QtInfoMsg, QStringLiteral("scanner"),
				QStringLiteral("Reading MXF/OMF headers for %1 file(s) the databases don't describe (%2 MXF, %3 OMF)")
					.arg(total)
					.arg(mxfRows)
					.arg(omfRows));
	emit scanProgress(0, total, {});

	std::atomic<int> done{0};
	std::atomic<int> recovered{0};
	std::atomic<qint64> totalBytesRead{0};
	std::atomic<qint64> maxBytesRead{0};
	// OMF-era: the legacy reads are tallied apart so the MXF summary line
	// keeps its meaning (a Bento tail is ~15 KB; an MXF fast read 256 KB).
	std::atomic<qint64> omfBytesRead{0};
	std::atomic<qint64> omfMaxBytesRead{0};
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
			qint64 bytesRead = 0;
			MxfMetadata mxf;
			if (row.omfEra)
			{
				// OMF-era: the Bento tail gives the same MxfMetadata the
				// header path does, plus two facts a header never carries —
				// the master's bin, and the FILE mob's own identity (the
				// media-data object's MobID, which is exactly what the v2
				// PMR would have named). Both fill only what pass 1 left
				// empty; a database-listed row keeps its PMR/MDB values.
				const OmfMetadata omf = OmfParser::parseHeader(mf.filePath, &bytesRead);
				mxf = omf.essence;
				assignIfMissing(mf.originalBin, omf.bin);
				if (mf.mobId.isEmpty())
					mf.mobId = omf.fileMobId;
			}
			else
			{
				mxf = MxfParser::parseHeader(mf.filePath, &bytesRead);
			}
			applyMetadata(mf, mxf);

			// Pass 1 found no project in the PMR (no entry, or a blank one):
			// take the one Avid wrote into the file — the very attribute
			// Media Composer reads back when it rebuilds a folder's PMR.
			if (mf.project.isEmpty())
				mf.project = mxf.projectName;

			// Re-join by the header's own UMID (its MaterialPackage UID = the
			// master MOB in MXF byte order): a file the PMR doesn't name but
			// the MDB still knows — copied in before Avid re-indexed, another
			// seat's media, or a PMR that was corrupt while the MDB read fine.
			// Recovers name/bin/source and the master MOB so the bin filter
			// and Select Relatives see the row; the FILE mob is left empty
			// (the header can't identify it reliably). The database status is
			// NOT changed: the folder's PMR still doesn't list the file, and
			// that is what the status reports. Media vs Precompute is NOT
			// touched: the header just said.
			const bool unlisted = mf.dbStatus != MediaFile::DbStatus::Listed;
			if (unlisted && !mxf.umid.isEmpty() && !MobId::isAllZero(mxf.umid))
			{
				const auto mapIt = clipsByFolder.constFind(row.folderKey);
				if (mapIt != clipsByFolder.constEnd() && !mapIt->isEmpty())
				{
					// Direct match is rare: the MXF stores the middle fields
					// little-endian, the MDB big-endian. Try direct (free),
					// then swapped.
					auto recIt = mapIt->constFind(mxf.umid);
					// OMF-era: the wrapped id is already the database's key
					// form, and swapping its middle fields would name a
					// DIFFERENT (equally well-formed) OMF id — so no retry.
					if (recIt == mapIt->constEnd() && !row.omfEra)
					{
						const QString swapped = MobId::toPmrForm(mxf.umid);
						if (!swapped.isEmpty())
							recIt = mapIt->constFind(swapped);
					}
					if (recIt != mapIt->constEnd())
					{
						applyMdbRecord(mf, recIt.value());
						if (!recIt->mobIdHex.isEmpty())
							mf.masterMobId = recIt->mobIdHex;
						++recovered;
					}
				}
			}
			// The header's own identity can be the zero one too.
			mf.isInvalidUmid = MobId::isAllZero(mf.mobId) || MobId::isAllZero(mf.masterMobId) ||
							   (!mxf.umid.isEmpty() && MobId::isAllZero(mxf.umid));

			// OMF-era: separate counters, see above.
			std::atomic<qint64> &sumCounter = row.omfEra ? omfBytesRead : totalBytesRead;
			std::atomic<qint64> &maxCounter = row.omfEra ? omfMaxBytesRead : maxBytesRead;
			sumCounter.fetch_add(bytesRead, std::memory_order_relaxed);

			// Lock-free max via CAS loop. Every pool thread fights for
			// the same atomic, so retry until we win or someone else
			// sets a bigger value.
			qint64 prev = maxCounter.load(std::memory_order_relaxed);
			while (bytesRead > prev &&
				   !maxCounter.compare_exchange_weak(prev, bytesRead, std::memory_order_relaxed))
			{
			}

			const int n = ++done;
			if (n == total || throttle.shouldEmit())
				emit scanProgress(n, total, mf.fileName);
		});

	// MARK: Pass 2 summary log

	if (mxfRows > 0)
	{
		const qint64 totalBytes = totalBytesRead.load();
		const qint64 maxBytes = maxBytesRead.load();
		const qint64 avgKB = (totalBytes / mxfRows) / 1024;
		emitLog(QtInfoMsg, QStringLiteral("mxf"),
				QStringLiteral("MXF parse: %1 files, avg %2 KB/file, max %3 KB, total %4 MB read")
					.arg(mxfRows)
					.arg(avgKB)
					.arg(maxBytes / 1024)
					.arg(totalBytes / (1024 * 1024)));
	}
	if (omfRows > 0)
	{
		// OMF-era: the legacy reader's own line, on its own console tag.
		const qint64 totalBytes = omfBytesRead.load();
		const qint64 maxBytes = omfMaxBytesRead.load();
		const qint64 avgKB = (totalBytes / omfRows) / 1024;
		emitLog(QtInfoMsg, QStringLiteral("omf"),
				QStringLiteral("OMF parse: %1 files, avg %2 KB/file, max %3 KB, total %4 KB read")
					.arg(omfRows)
					.arg(avgKB)
					.arg(maxBytes / 1024)
					.arg(totalBytes / 1024));
	}
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