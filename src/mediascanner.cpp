#include "mediascanner.h"
#include "mediaobservations.h"
#include "featureflags.h"
#include "avideffects.h"
#include "avidusage.h"
#include "conventions.h"
#include "testpause.h"
#include "diagnostics.h"
#include "mobid.h"
#include "mxfparser.h"
#include "omfparser.h"
#include "pmrkey.h"
#include "progressthrottle.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QMutexLocker>
#include <QSet>
#include <QScopeGuard>
#include <QStringList>
#include <QtConcurrent>
#include <algorithm>
#include <array>
#include <QStorageInfo>
#include "volumeidentity.h"
#include <numeric>

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
		const QString mxfRootPath = avidRoot.isEmpty() ? QString{} : childDirectory(avidRoot, Conventions::kMxfDir);
		if (!mxfRootPath.isEmpty() && AvidMediaLayout::isMxfRoot(scannerFolderKey(mxfRootPath)))
			roots.append({AvidMediaLayout::Family::Mxf, mxfRootPath, path});
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
	// Ignore another scan request while one is running.
	bool expected = false;
	if (!m_running.compare_exchange_strong(expected, true))
		return;

	m_options = options;
	m_scopeComplete.store(true);

	// Locked so leftover threads from a prior scan can't race us.
	{
		QMutexLocker lock(&m_logMutex);
		m_pendingLogs.clear();
	}
	// Start with fresh folder counts and database records.
	{
		QMutexLocker lock(&m_overfullMutex);
		m_overfullFolders.clear();
	}
	{
		QMutexLocker lock(&m_mdbMapsMutex);
		m_mdbMapsByFolder.clear();
		m_seenFolders.clear();
		m_mdbSources.clear();
		m_pmrSources.clear();
	}
	m_flushTimer.start();
	m_lastFlushElapsed = 0;

	m_job.start(
		[this]
		{
			const auto resetRunning = qScopeGuard([this]
												  { m_running.store(false); });
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
	// Precomputes may have file-mob code 0 or 9; classification needs the
	// master usage. Unknown or conflicting usage stays Unknown. Name lookup
	// supplies effect details only after classification.

	/// Applies ranked clip names within the scanner; higher-ranked non-empty names win.
	/// The first of two equally ranked names is retained. Loaded-bin fallback
	/// is applied later by the table.
	void setClipName(MediaFile &mf, const QString &name, MediaFile::ClipNameSource src)
	{
		if (name.isEmpty() || int(src) <= int(mf.clipNameSource))
			return;
		mf.clipName = name;
		mf.clipNameSource = src;
	}

	// Fill an empty field without replacing an earlier source's value.
	template <typename T>
	void assignIfMissing(T &dst, const T &src)
	{
		if (!src.isEmpty() && dst.isEmpty())
			dst = src;
	}

	// Fill missing fields from MDB during the database pass and header rejoin.
	// Existing non-empty values from earlier sources are retained; clip names
	// use the separate source ranking.
	//
	// MDB clip names rank below material-package names. They can remain on the
	// database fast path, which skips reading the media header.
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

	/// Applies database or header metadata after MediaMetadataUtil::finalise.
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
			if (!metadata.frameRate.isEmpty())
				mf.frameRate = metadata.frameRate;
			if (metadata.frameRateRatio.valid())
				mf.frameRateRatio = metadata.frameRateRatio;
			if (!metadata.bitDepth.isEmpty())
				mf.bitDepth = metadata.bitDepth;
			if (!metadata.sampleFormat.isEmpty())
				mf.sampleFormat = metadata.sampleFormat;
			if (metadata.sampleRate > 0)
				mf.sampleRate = metadata.sampleRate;
			if (metadata.sampleRateRatio.valid())
				mf.sampleRateRatio = metadata.sampleRateRatio;
			if (!metadata.sampleRateEncoding.isEmpty())
				mf.sampleRateEncoding = metadata.sampleRateEncoding;
			if (metadata.channels > 0)
				mf.channels = metadata.channels;
			if (!metadata.clipDurations.isEmpty())
				mf.clipDurations = metadata.clipDurations;
			if (metadata.duration.units > 0)
				mf.duration = metadata.duration;
			if (metadata.timecodeBase > 0)
				mf.timecodeBase = metadata.timecodeBase;
			if (metadata.dropFrame)
				mf.dropFrame = true;
			// Parsers identify audio from descriptors or essence labels.
			if (metadata.isAudio)
				mf.kind = MediaFile::Kind::Audio;
			else if (metadata.width > 0 && metadata.height > 0)
				mf.kind = MediaFile::Kind::Video;
		}

		// Header import metadata can fill fields missing from the database pass.
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
	QVector<LogMessage> batch;
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
	const int locationCount = m_options.volumePaths.size() + m_options.manualPaths.size();
	emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("Scanning %1 location(s)...").arg(locationCount));

	QElapsedTimer stageTimer;
	stageTimer.start();
	qCDebug(lcScanner) << "scan start:" << locationCount << "location(s)";

	// MARK: Pass 1 — per-location folder walk + databases

	QVector<MediaFile> allFiles = scanRequestedLocations();
	KelpieIdAllocator ids;
	for (auto &file : allFiles)
		file.kelpieId = ids.allocate();

	qCDebug(lcScanner) << "pass 1 (walk + databases):" << allFiles.size() << "files in" << stageTimer.restart()
					   << "ms";

	// MARK: Pass 2 — headers for admitted nonempty media

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

	if (!applyEffectDetails(allFiles))
	{
		concludeScan(allFiles, /*cancelled=*/true);
		return;
	}
	const auto notes = collectScanNotes(allFiles);
	if (!notes)
	{
		concludeScan(allFiles, /*cancelled=*/true);
		return;
	}
	logScanSummary(allFiles.size(), *notes);

	concludeScan(allFiles, /*cancelled=*/false);
}

// MARK: - Requested locations

QVector<MediaFile> MediaScanner::scanRequestedLocations()
{
	QVector<MediaFile> allFiles;

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
		if (scanned.contains(path))
			return;
		scanned.insert(path);

		QDir locationDir(path);
		QString volumeName = locationDir.dirName();
		if (volumeName.isEmpty())
			volumeName = path;

		if (!canReadPath(path))
		{
			m_scopeComplete.store(false);
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

	return allFiles;
}

// MARK: - Scan finalisation

bool MediaScanner::applyEffectDetails(QVector<MediaFile> &files)
{
	// Only metadata-confirmed precomputes receive name-derived effect details.
	// The name never decides their media type or precompute category.
	for (MediaFile &f : files)
	{
		if (m_job.isCancelled())
			return false;
		if (f.type != MediaFile::Type::Precompute)
			continue;
		const AvidEffects::Hit hit = AvidEffects::lookup(f.clipName);
		f.effect = hit.name;
		f.effectCategory = hit.category;
		f.effectSequence = hit.sequence;
	}
	return true;
}

std::optional<QStringList> MediaScanner::collectScanNotes(const QVector<MediaFile> &files)
{
	int noReference = 0, noDatabase = 0, invalidUmid = 0, noProject = 0, nonPortable = 0;
	for (const auto &f : files)
	{
		if (m_job.isCancelled())
			return std::nullopt;
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

	if (m_job.isCancelled())
		return std::nullopt;

	qCDebug(lcScanner) << "scan tally:" << files.size() << "files —" << noReference
					   << "no reference," << noDatabase << "no database," << invalidUmid << "invalid umid,"
					   << noProject << "no project," << nonPortable << "non-portable";

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
	return notes;
}

void MediaScanner::logScanSummary(qsizetype fileCount, const QStringList &notes)
{
	if (fileCount == 0)
	{
		emitLog(QtWarningMsg, QStringLiteral("scanner"), "No media files found.");
	}
	else
	{
		emitLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("Scan complete: %1 files found").arg(fileCount));
	}

	if (!notes.isEmpty())
		emitLog(QtWarningMsg, QStringLiteral("scanner"),
				QStringLiteral("Scan notes: %1").arg(notes.join(QStringLiteral("; "))));
}

// MARK: - Scan conclusion

// Clear shared scan state on every exit so cancellation cannot leave stale
// database records or folder counts for the next scan.
void MediaScanner::concludeScan(QVector<MediaFile> &files, bool cancelled)
{
	QVector<ScanIssue> issues;
	QHash<QString, QStringList> pathsByMob;
	QHash<QString, QSet<QString>> namesByFolder;
	for (const auto &file : files)
	{
		namesByFolder[scannerFolderKey(QFileInfo(file.mediaFilePath).absolutePath())].insert(PmrKey::primary(file.fileName));
		if (!file.fileMobId.isEmpty())
			pathsByMob[file.fileMobId].append(file.mediaFilePath);
	}
	const bool completeScope = !cancelled && m_scopeComplete.load();
	for (const auto &source : m_pmrSources)
	{
		const QDir folder(QFileInfo(source.snapshot->path).absolutePath());
		for (auto it = source.index.cbegin(); it != source.index.cend(); ++it)
			for (const auto &entry : it.value())
			{
				// A filename is a database claim, never permission to escape the folder.
				if (entry.fileName.contains(QLatin1Char('/')) || entry.fileName.contains(QLatin1Char('\\')))
					continue;
				const QString suffix = QFileInfo(entry.fileName).suffix().toLower();
				const bool legacy = source.snapshot->path.contains(QStringLiteral("/OMFI MediaFiles/"), Qt::CaseInsensitive);
				if (legacy ? (suffix != QStringLiteral("omf") && suffix != QStringLiteral("aif") && suffix != QStringLiteral("wav")) : suffix != QStringLiteral("mxf"))
					continue;
				const QString expected = folder.filePath(entry.fileName);
				if (namesByFolder.value(scannerFolderKey(folder.path())).contains(PmrKey::primary(entry.fileName)))
					continue;
				ScanIssue issue;
				issue.source = source.snapshot;
				issue.expectedPath = expected;
				issue.fileMobId = entry.fileMobId;
				issue.matchingPaths = pathsByMob.value(entry.fileMobId);
				issue.scopeComplete = completeScope;
				issue.explanation = QStringLiteral("Database entry has no media row at its recorded local path");
				issues.append(issue);
				emitLog(QtWarningMsg, QStringLiteral("reconciliation"),
					QStringLiteral("Database reference not found locally: %1; source %2; %3 matching location(s) elsewhere; scan scope %4")
					.arg(expected, source.snapshot->path).arg(issue.matchingPaths.size())
					.arg(completeScope ? QStringLiteral("complete") : QStringLiteral("incomplete")));
			}
	}
	QHash<QString, int> unmatchedBySource;
	for (const auto &source : m_mdbSources)
		for (auto file = source.database.files.cbegin(); file != source.database.files.cend(); ++file)
			if (!pathsByMob.contains(file.key()))
			{
				ScanIssue issue;
				issue.kind = ScanIssue::Kind::UnmatchedDatabaseIdentity;
				issue.source = source.snapshot;
				issue.fileMobId = file.key();
				issue.scopeComplete = completeScope;
				issue.explanation = QStringLiteral("MDB file/source object has no matching scanned media identity; this does not establish a missing physical file");
				issues.append(issue);
				++unmatchedBySource[source.snapshot->path];
			}
	for (auto source = unmatchedBySource.cbegin(); source != unmatchedBySource.cend(); ++source)
		emitLog(QtWarningMsg, QStringLiteral("reconciliation"),
			QStringLiteral("%1 unmatched MDB file/source identities in %2; no matching scanned media identity. This does not prove missing files; scan scope %3")
			.arg(source.value()).arg(source.key()).arg(completeScope ? QStringLiteral("complete") : QStringLiteral("incomplete")));

	QHash<QString, QString> volumeIds;
	for (auto &file : files)
	{
		const QString folder = QFileInfo(file.mediaFilePath).absolutePath();
		auto volume = volumeIds.find(folder);
		if (volume == volumeIds.end())
			volume = volumeIds.insert(folder, VolumeIdentity::capture(folder).identifier());
		// Scan-wide joins retain shared masters across folders and volumes.
		for (const auto &source : m_mdbSources)
		{
			const auto record = source.database.files.constFind(file.fileMobId);
			if (record != source.database.files.cend())
			{
				MediaObservations::metadata(file, record->essence, source.snapshot);
				bool headerIdentityEstablished = false;
				for (const auto &identity : file.evidence.observations(MediaProperty::FileMobId))
					if (identity.eligible && identity.readState == PropertyReadState::Present &&
						identity.snapshot && (identity.snapshot->source == MetadataSource::Mxf || identity.snapshot->source == MetadataSource::Omf) &&
						identity.value.toString() == file.fileMobId)
						headerIdentityEstablished = true;
				MediaObservations::qualifyTechnical(file, source.snapshot,
					record->essence.valid && (file.databaseMetadataCurrent || headerIdentityEstablished));
				for (const auto &masterId : record->masterMobIds)
				{
					if (!file.masterMobIds.contains(masterId))
						file.masterMobIds.append(masterId);
					MediaObservations::add(file, MediaProperty::MasterMobId, source.snapshot,
						QStringLiteral("master source-clip graph references file mob"), masterId,
						EvidenceBasis::Derived, {}, file.fileMobId);
				}
			}
			for (const auto &masterId : file.masterMobIds)
			{
				const auto master = source.database.masters.constFind(masterId);
				if (master != source.database.masters.cend())
					MediaObservations::add(file, MediaProperty::ClipName, source.snapshot,
						QStringLiteral("OMFI:CPNT:Name"), master->clipName, EvidenceBasis::Recorded, {}, masterId);
			}
		}
		MediaObservations::resolveTechnical(file);
		file.masterMobIds.sort();
		file.scanStamp = {file.mediaFilePath, volume.value(), file.modified, file.fileMobId, file.masterMobIds};
		for (MediaProperty field : {MediaProperty::Codec, MediaProperty::Resolution, MediaProperty::BitDepth, MediaProperty::ClipName})
		{
			const auto selected = file.evidence.selected(field);
			if (selected.agreement == PropertyAgreement::Conflicting)
			{
				ScanIssue issue;
				issue.kind = ScanIssue::Kind::MetadataConflict;
				issue.expectedPath = file.mediaFilePath;
				issue.fileMobId = file.fileMobId;
				issue.scopeComplete = completeScope;
				issue.explanation = QStringLiteral("Property %1: %2").arg(static_cast<int>(field)).arg(selected.reason);
				issues.append(issue);
				emitLog(QtWarningMsg, QStringLiteral("metadata"),
					QStringLiteral("Metadata disagreement for %1 (property %2): %3")
					.arg(file.mediaFilePath).arg(static_cast<int>(field)).arg(selected.reason));
			}
		}
	}

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

	// Discard cached records so the next scan reads current metadata.
	{
		QMutexLocker lock(&m_mdbMapsMutex);
		m_mdbMapsByFolder.clear();
		m_seenFolders.clear();
		m_mdbSources.clear();
		m_pmrSources.clear();
	}

	// Drain the log buffer first so the last batch doesn't land
	// after scanFinished.
	flushLogs();
	emit scanFinished(files);
	emit scanIssuesFinished(issues);
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
		// An empty result does not mean the media roots themselves are absent.
		emitLog(QtWarningMsg, QStringLiteral("scanner"),
				QStringLiteral("  No supported media found at the root of %1 "
							   "(use File > Add Folder or Volume for media trees elsewhere)")
					.arg(volumeName));
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
	for (const QString &mediaFolderPath : folders)
	{
		if (m_job.isCancelled())
			break;
		ScanTask task;
		task.family = AvidMediaLayout::Family::Omf;
		task.mediaFolderPath = mediaFolderPath;
		task.mediaFolderName = QFileInfo(mediaFolderPath).fileName();
		task.volumeName = volumeName;
		task.volumePath = volumePath;
		auto result = processFolderTask(task);
		for (const auto &msg : result.logs)
			emitLog(msg.level, msg.module, msg.message);
		files.append(result.files);
		emit scanProgress(++completed, folders.size(), mediaFolderPath);
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
	for (const QString &mediaFolderName : subFolders)
	{
		if (m_job.isCancelled())
			break;

		// Direct MXF children may be numbered or named; layout rules exclude staging folders.
		const QString mediaFolderPath = mxfDir.filePath(mediaFolderName);
		if (!AvidMediaLayout::locateMediaFolder(mediaFolderPath))
			continue;

		if (!canReadPath(mediaFolderPath))
		{
			emitLog(QtWarningMsg, QStringLiteral("scanner"), QStringLiteral("  Permission denied: %1").arg(mediaFolderName));
			continue;
		}

		MediaScanner::ScanTask t;
		t.family = AvidMediaLayout::Family::Mxf;
		t.mediaFolderPath = mediaFolderPath;
		t.mediaFolderName = mediaFolderName;
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
									 emit scanProgress(done, totalFolders, t.mediaFolderPath);
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

	if (m_job.isCancelled() || (task.family == AvidMediaLayout::Family::Omf && !m_options.includeOmf))
		return result;
	const auto requested = AvidMediaLayout::locateMediaFolder(task.mediaFolderPath);
	if (!requested || requested->family != task.family)
		return result;
	const bool isQuarantineFolder = requested->isQuarantined;
	const QString key = scannerFolderKey(task.mediaFolderPath);
	{
		// Also cover UME folders reached through a link beneath a supported root.
		if (AvidMediaLayout::isInsideUmeRoot(task.mediaFolderPath) || AvidMediaLayout::isInsideUmeRoot(key))
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

	if (!canReadPath(task.mediaFolderPath))
	{
		m_scopeComplete.store(false);
		result.logs.append({QtWarningMsg, QStringLiteral("scanner"),
			QStringLiteral("Folder unreadable; scan scope incomplete: %1").arg(task.mediaFolderPath)});
		return result;
	}

	// MARK: Parse the databases

	// Missing PMR/MDB is normal in Interplay environments.
	const QDir folder(task.mediaFolderPath);
	const QStringList filters = task.family == AvidMediaLayout::Family::Mxf
		? QStringList{QStringLiteral("*.mxf"), QStringLiteral("*.pmr"), QStringLiteral("*.mdb")}
		: QStringList{QStringLiteral("*.omf"), QStringLiteral("*.aif"), QStringLiteral("*.wav"), QStringLiteral("*.pmr"), QStringLiteral("*.mdb")};
	const QFileInfoList entries = folder.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks);
	FolderDatabases dbs = readFolderDatabases(task, entries, result.logs);
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

		MediaFile mf = buildMediaFile(entry, task.volumeName, task.volumePath, task.mediaFolderName, task.family, pmrMap, mdb,
									  folderStatus);
		mf.isQuarantined = isQuarantineFolder;
		const auto filesystem = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Filesystem, mf.mediaFilePath, mf.modified, SourceReadState::Complete});
		MediaObservations::add(mf, MediaProperty::Location, filesystem, QStringLiteral("filesystem path"), mf.mediaFilePath);
		MediaObservations::add(mf, MediaProperty::Filename, filesystem, QStringLiteral("filesystem filename"), mf.fileName);
		MediaObservations::add(mf, MediaProperty::Size, filesystem, QStringLiteral("filesystem byte size"), mf.sizeBytes);
		MediaObservations::add(mf, MediaProperty::Created, filesystem, QStringLiteral("filesystem birth time"), mf.created.isValid() ? QVariant(mf.created) : QVariant{});
		MediaObservations::add(mf, MediaProperty::Modified, filesystem, QStringLiteral("filesystem modification time"), mf.modified.isValid() ? QVariant(mf.modified) : QVariant{});
		const QString fileKey = PmrKey::primary(mf.fileName);
		for (const auto &source : dbs.pmrSources)
			for (const auto &record : source.index.value(fileKey))
			{
				MediaObservations::add(mf, MediaProperty::FileMobId, source.snapshot, QStringLiteral("PMR file MobId"), record.fileMobId);
				MediaObservations::add(mf, MediaProperty::MasterMobId, source.snapshot, QStringLiteral("PMR master MobId"), record.masterMobId, EvidenceBasis::Recorded, {}, record.fileMobId);
				MediaObservations::add(mf, MediaProperty::Project, source.snapshot, QStringLiteral("PMR project"), record.project, EvidenceBasis::Recorded, {}, record.fileMobId);
				if (!record.masterMobId.isEmpty() && !mf.masterMobIds.contains(record.masterMobId))
					mf.masterMobIds.append(record.masterMobId);
				// These properties are absent by the established PMR record definition.
				for (MediaProperty field : {MediaProperty::Codec, MediaProperty::SampleRate})
				{
					MetadataObservation absent;
					absent.snapshot = source.snapshot;
					absent.objectIdentity = record.fileMobId;
					absent.property = QStringLiteral("PMR record format");
					absent.readState = PropertyReadState::Absent;
					absent.explanation = QStringLiteral("The supported PMR record layout does not store this property");
					mf.evidence.observe(field, std::move(absent));
				}
			}
		for (const auto &source : dbs.mdbSources)
		{
			const auto record = source.database.files.constFind(mf.fileMobId);
			if (record != source.database.files.cend())
			{
				MediaObservations::metadata(mf, record->essence, source.snapshot);
				MediaObservations::qualifyTechnical(mf, source.snapshot, mf.databaseMetadataCurrent,
					mf.databaseMetadataCurrent ? SourceFreshness::TimestampConsistent : SourceFreshness::Unknown);
				for (const auto &master : record->masterMobIds)
					if (!mf.masterMobIds.contains(master))
						mf.masterMobIds.append(master);
			}
			for (const auto &masterId : mf.masterMobIds)
			{
				const auto master = source.database.masters.constFind(masterId);
				if (master != source.database.masters.cend())
				{
					MediaObservations::add(mf, MediaProperty::ClipName, source.snapshot, QStringLiteral("OMFI:CPNT:Name"), master->clipName, EvidenceBasis::Recorded, {}, masterId);
					MediaObservations::add(mf, MediaProperty::OriginalBin, source.snapshot, QStringLiteral("_ORG_BIN"), master->bin, EvidenceBasis::Recorded, {}, masterId);
				}
			}
		}

		result.files.append(mf);
	}

	if (task.family == AvidMediaLayout::Family::Mxf && result.files.size() > Conventions::kFolderWarn)
	{
		// Don't warn per-folder; N pool threads firing would bury
		// the progress logs. Stash the (folder, count) and let
		// doScan emit one summary at the end.
		QMutexLocker lock(&m_overfullMutex);
		m_overfullFolders.append(
			{task.volumeName + QLatin1Char('/') + task.mediaFolderName, int(result.files.size())});
	}

	{
		QMutexLocker lock(&m_mdbMapsMutex);
		m_pmrSources.append(dbs.pmrSources);
		m_mdbSources.append(dbs.mdbSources);
	}

	// Cache the clip records for pass 2's UMID re-join — only the masters;
	// the per-file essence is consumed above and dropped. Move because this
	// task is done with it. Skip empties as nothing to join.
	// Keep case-sensitive share directories distinct in both passes.
	if (!mdb.masters.isEmpty())
	{
		QMutexLocker lock(&m_mdbMapsMutex);
		m_mdbMapsByFolder.insert(key, std::move(mdb.masters));
	}

	return result;
}

// MARK: - Per-folder databases

MediaScanner::FolderDatabases MediaScanner::readFolderDatabases(const ScanTask &task,
	const QFileInfoList &entries, QVector<LogMessage> &logs)
{
	FolderDatabases dbs;
	readFolderPmrs(task, entries, dbs, logs);
	readFolderMdbs(task, entries, dbs, logs);
	return dbs;
}

void MediaScanner::readFolderPmrs(const ScanTask &task, const QFileInfoList &entries,
	FolderDatabases &dbs, QVector<LogMessage> &logs)
{
	for (const auto &entry : entries)
	{
		if (entry.suffix().compare(QStringLiteral("pmr"), Qt::CaseInsensitive) != 0)
			continue;
		dbs.pmrExists = true;
		bool ok = true;
		const PmrIndex index = PmrParser::buildFileMap(entry.filePath(), &ok);
		const SourceSnapshotRef snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Pmr, entry.filePath(), entry.lastModified(),
			ok ? SourceReadState::Complete : SourceReadState::Incomplete});
		dbs.pmrSources.append({snapshot, index});
		// Keep partial recovery observations; they cannot prove absence.
		if (ok)
			for (auto it = index.cbegin(); it != index.cend(); ++it)
				dbs.pmr[it.key()].append(it.value());
		if (!ok)
		{
			dbs.pmrOk = false;
			logs.append({QtWarningMsg, QStringLiteral("scanner"),
				QStringLiteral("Database read incomplete/unreadable: %1; absence in /%2 cannot be established")
				.arg(entry.filePath(), task.mediaFolderName)});
		}
	}
}

void MediaScanner::readFolderMdbs(const ScanTask &task, const QFileInfoList &entries,
	FolderDatabases &dbs, QVector<LogMessage> &logs)
{
	for (const auto &entry : entries)
	{
		if (entry.suffix().compare(QStringLiteral("mdb"), Qt::CaseInsensitive) != 0)
			continue;
		dbs.mdbExists = true;
		bool ok = true;
		const MdbDatabase database = MdbParser::load(entry.filePath(), &ok);
		const SourceSnapshotRef snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Mdb, entry.filePath(), entry.lastModified(),
			ok ? SourceReadState::Complete : SourceReadState::Unreadable});
		dbs.mdbSources.append({snapshot, database});
		// Compatibility indexes are only lookup aids. All versions remain in mdbSources.
		for (auto it = database.masters.cbegin(); it != database.masters.cend(); ++it)
			if (!dbs.mdb.masters.contains(it.key()))
				dbs.mdb.masters.insert(it.key(), it.value());
		for (auto it = database.files.cbegin(); it != database.files.cend(); ++it)
			if (!dbs.mdb.files.contains(it.key()))
				dbs.mdb.files.insert(it.key(), it.value());
		if (!ok)
		{
			dbs.mdbOk = false;
			logs.append({QtWarningMsg, QStringLiteral("scanner"),
				QStringLiteral("Database unreadable: %1 in /%2; other sources remain available")
				.arg(entry.filePath(), task.mediaFolderName)});
		}
	}
}

// MARK: - MediaFile assembly (database pass)

MediaFile MediaScanner::buildMediaFile(const QFileInfo &fi, const QString &volumeName,
									   const QString &volumePath, const QString &mediaFolderName,
									   AvidMediaLayout::Family family,
									   const PmrIndex &pmrMap,
									   const MdbDatabase &mdb,
									   MediaFile::DbStatus folderStatus)
{
	// `fi` is the directory listing's own entry — its size and times are
	// already known, so nothing here stats the file again.
	MediaFile mf;
	mf.mediaFilePath = fi.filePath();
	mf.fileName = fi.fileName();
	mf.volumeName = volumeName;
	mf.volumePath = volumePath;
	mf.mediaFolderName = mediaFolderName;
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

	// Match the normalised filename, including its punctuation and extension.
	const auto pmrIt = pmrMap.constFind(primaryKey);
	if (pmrIt != pmrMap.constEnd() && !pmrIt->isEmpty())
	{
		pmrHit = &pmrIt->first();
		mf.project = pmrHit->project;
		mf.fileMobId = pmrHit->fileMobId;
		mf.masterMobId = pmrHit->masterMobId;
	}

	// The PMR v1 contains no embedded master/project. Recover a master only
	// when the MDB's source-reference graph establishes a unique relationship.
	const auto fileIt = mf.fileMobId.isEmpty() ? mdb.files.constEnd() : mdb.files.constFind(mf.fileMobId);
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
	const bool isNonEmpty = mf.sizeBytes > 0;
	const bool databaseMetadataComplete = fileIt != mdb.files.constEnd() && fileIt->essenceComplete &&
										  masterIt != mdb.masters.constEnd();
	const bool databaseTimestampMatches = pmrHit && pmrHit->fileModifiedSecs != 0 &&
										  PmrParser::trailerMatchesModified(pmrHit->fileModifiedSecs, mf.modified);
	mf.databaseMetadataCurrent = databaseMetadataComplete && databaseTimestampMatches;
	if (isNonEmpty && mf.databaseMetadataCurrent)
	{
		MediaMetadata essence = fileIt->essence;
		essence.isPrecompute = AvidUsage::masterClassification(masterIt->usageCode) ==
							   AvidUsage::Classification::Precompute;
		essence.classificationKnown = masterIt->classificationKnown;
		essence.precomputeCategory = masterIt->precomputeCategory;
		applyMetadata(mf, essence);
	}
	mf.needsHeaderRead = isNonEmpty;

	// An all-zero MOB ID means Avid never wrote a real identity for the file
	// or its clip; the media can't be tracked or relinked reliably.
	mf.isInvalidUmid = MobId::isAllZero(mf.fileMobId) || MobId::isAllZero(mf.masterMobId);

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
		mf.fileMobId.clear();
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
		mf.frameRate.clear();
		mf.frameRateRatio = {};
		mf.bitDepth.clear();
		mf.sampleFormat.clear();
		mf.masterMobIds.clear();
		mf.evidence.excludeDatabaseMetadata();
		mf.sampleRate = 0;
		mf.sampleRateRatio = {};
		mf.sampleRateEncoding.clear();
		mf.channels = 0;
		mf.duration = {};
		mf.clipDurations.clear();
		mf.timecodeBase = 0;
		mf.dropFrame = false;
		mf.kind = MediaFile::Kind::Unknown;
		mf.type = MediaFile::Type::Unknown;
		mf.precomputeCategory = MediaFile::PrecomputeCategory::Unknown;
		mf.databaseMetadataCurrent = false;
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
		bool hasOmfMediaDescriptor = false;
		if (readingOmf)
		{
			// OMF1/OMF2 return the same essence fields, with the master
			// bin and file identity obtained from their object graph.
			const OmfMetadata omf = OmfParser::parseHeader(mf.mediaFilePath);
			hasOmfMediaDescriptor = omf.hasMediaDescriptor;
			metadata = omf.essence;
			headerBin = omf.bin;
			metadata.fileMobId = omf.fileMobId;
		}
		else
		{
			metadata = MxfParser::parseHeader(mf.mediaFilePath);
		}
		const bool headerUsable = metadata.valid || metadata.classificationKnown;
		const auto status = metadata.headerStatus;
		const SourceReadState sourceState = status == MediaMetadata::HeaderStatus::Complete ? SourceReadState::Complete :
			status == MediaMetadata::HeaderStatus::Incomplete ? SourceReadState::Incomplete : SourceReadState::Unreadable;
		const SourceSnapshotRef headerSource = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			readingOmf ? MetadataSource::Omf : MetadataSource::Mxf, mf.mediaFilePath, mf.modified, sourceState});
		// MXF stores material fields in network byte order; rows use the PMR/MDB
		// representation. OMF and AVB readers already normalize their integer fields.
		const auto canonicalHeaderId = [&](const QString &id)
		{
			if (readingOmf || id.isEmpty())
				return id;
			const QString canonical = MobId::swapMaterialByteOrder(id);
			return canonical.isEmpty() ? id : canonical;
		};
		// A selected OMF file mob can prove identity even when its
		// descriptor lacks usable technical fields. A different old
		// file's database details must still be invalidated in that case.
		const QString headerFileMobId = headerUsable || hasOmfMediaDescriptor ? canonicalHeaderId(metadata.fileMobId) : QString{};
		const bool headerMasterKnown = readingOmf || metadata.hasMaterialPackage;
		const QString headerMasterMobId = headerUsable && headerMasterKnown ? canonicalHeaderId(metadata.umid) : QString{};
		MediaObservations::metadata(mf, metadata, headerSource);
		MediaObservations::qualifyTechnical(mf, headerSource, metadata.valid);
		MediaObservations::add(mf, MediaProperty::FileMobId, headerSource, QStringLiteral("selected file/source mob identity"),
			headerFileMobId, EvidenceBasis::Recorded, metadata.fileMobId);
		MediaObservations::add(mf, MediaProperty::MasterMobId, headerSource, QStringLiteral("selected material/master identity"),
			headerMasterMobId, EvidenceBasis::Recorded, metadata.umid);
		MediaObservations::add(mf, MediaProperty::OriginalBin, headerSource, QStringLiteral("selected original-bin attribute"), headerBin);
		const auto contradicts = [](const QString &oldId, const QString &actualId)
		{
			return !oldId.isEmpty() && !actualId.isEmpty() && !MobId::isAllZero(actualId) && oldId != actualId;
		};
		if (contradicts(mf.fileMobId, headerFileMobId))
		{
			// The name was reused for different media. None of the old
			// clip's editorial/technical fields belongs to the replacement.
			clearReplacedMetadata(mf);
		}
		assignIfMissing(mf.fileMobId, headerFileMobId);
		if (!headerMasterMobId.isEmpty() && !mf.masterMobIds.contains(headerMasterMobId))
			mf.masterMobIds.append(headerMasterMobId);
		if (headerUsable)
		{
			assignIfMissing(mf.masterMobId, headerMasterMobId);
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
		if (headerUsable && headerMasterKnown && masters && !headerMasterMobId.isEmpty() && !MobId::isAllZero(headerMasterMobId))
		{
			const auto record = masters->constFind(headerMasterMobId);
			if (record != masters->constEnd())
				applyMdbRecord(mf, record.value());
		}
		// The header's own identity can be the zero one too.
		mf.isInvalidUmid = MobId::isAllZero(mf.fileMobId) || MobId::isAllZero(mf.masterMobId) ||
						   (headerUsable && (MobId::isAllZero(metadata.umid) || MobId::isAllZero(metadata.fileMobId)));
	}
} // namespace

void MediaScanner::readMediaHeadersConcurrently(QVector<MediaFile> &files)
{
	// Pass 1 admits nonempty media for header observations. Resolve a
	// case-preserving cache key once per folder.
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
		// Empty media remains a physical row without an attempted header read.
		const bool omfCandidate = f.omfEra;
		if (!f.needsHeaderRead)
			continue;
		const QString mediaFolderPath = QFileInfo(f.mediaFilePath).absolutePath();
		auto cacheIt = folderKeyCache.find(mediaFolderPath);
		if (cacheIt == folderKeyCache.end())
			cacheIt = folderKeyCache.insert(mediaFolderPath, scannerFolderKey(mediaFolderPath));
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
