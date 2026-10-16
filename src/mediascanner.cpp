// Owns the background scan and forwards progress/results to the existing UI.
// MediaEngine owns source reading, matching and temporary working scan evidence.
#include "mediascanner.h"
#include "mediaengine/scanengine.h"
#include "mediaengineadapter.h"
#include "app_avidmedialayout.h"
#include "conventions.h"
#include "testpause.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <exception>
#include <new>
#include <utility>

namespace
{
	constexpr qint64 kSourceCheckpointIntervalMs = 10000;

	QString scannerFolderKey(const QString &path)
	{
		const QFileInfo info(path);
		const QString canonical = info.canonicalFilePath();
		return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
	}

	QString childDirectory(const QString &parent, QLatin1String name,
						   const MediaEngine::Cancellation *cancellation = nullptr)
	{
		if (cancellation && cancellation->cancelled())
			return {};
		const QDir dir(parent);
		const QString expected = dir.filePath(name);
		if (QFileInfo(expected).isDir())
			return expected;
		if (cancellation && cancellation->cancelled())
			return {};
		for (const QFileInfo &child : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
		{
			if (cancellation && cancellation->cancelled())
				return {};
			if (child.fileName().compare(name, Qt::CaseInsensitive) == 0)
				return child.absoluteFilePath();
		}
		return {};
	}

	struct MediaRoot
	{
		AvidMediaLayout::Family family;
		QString path;
		QString volumePath;
	};

	QVector<MediaRoot> rootsForAddedPath(const QString &requestedPath, bool includeOmf = true,
									   const MediaEngine::Cancellation *cancellation = nullptr)
	{
		if (cancellation && cancellation->cancelled())
			return {};
		if (!QFileInfo(requestedPath).isDir())
			return {};
		if (cancellation && cancellation->cancelled())
			return {};
		const QString path = scannerFolderKey(requestedPath);
		if (cancellation && cancellation->cancelled())
			return {};
		if (AvidMediaLayout::isInsideUmeRoot(requestedPath) || AvidMediaLayout::isInsideUmeRoot(path))
			return {};
		if (const auto location = AvidMediaLayout::locateMediaFolder(path))
			return {{location->family, location->rootPath, QFileInfo(location->rootPath).absolutePath()}};
		if (AvidMediaLayout::isMxfRoot(path))
			return {{AvidMediaLayout::Family::Mxf, path, QFileInfo(path).absolutePath()}};
		QVector<MediaRoot> roots;
		const bool isAvidRoot = QFileInfo(path).fileName().compare(Conventions::kAvidMediaFilesDir, Qt::CaseInsensitive) == 0;
		const QString avidRoot = isAvidRoot ? path : childDirectory(path, Conventions::kAvidMediaFilesDir, cancellation);
		const QString mxfRootPath = avidRoot.isEmpty() ? QString{} : childDirectory(avidRoot, Conventions::kMxfDir, cancellation);
		if (cancellation && cancellation->cancelled())
			return {};
		if (!mxfRootPath.isEmpty() && AvidMediaLayout::isMxfRoot(scannerFolderKey(mxfRootPath)))
			roots.append({AvidMediaLayout::Family::Mxf, mxfRootPath, path});
		const QString omfRoot = includeOmf ? childDirectory(path, Conventions::kOmfMediaFilesDir, cancellation) : QString{};
		if (cancellation && cancellation->cancelled())
			return roots;
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

void MediaScanner::startScan(const Options &options)
{
	{
		const std::lock_guard<std::mutex> lock(m_stateMutex);
		if (m_state != ScanState::Idle)
			return;
		m_state = ScanState::Active;
	}
	m_options = options;
	m_job.start([this]
				{
		QString failure;
		// start() resets the worker flag after joining the previous job. Keep
		// a stop requested during that startup window effective for this run.
		{
			const std::lock_guard<std::mutex> lock(m_stateMutex);
			if (m_state == ScanState::CancelRequested)
				m_job.cancel();
		}
		try
		{
			auto completion = doScan();
			checkpoint(Checkpoint::Prepared, completion.rows.size());
			QMetaObject::invokeMethod(this, [this, completion = std::move(completion)]() mutable
				{
					try
					{
						publishCompletion(std::move(completion));
					}
					catch (const std::bad_alloc &)
					{
						publishFailure(QStringLiteral("The scan stopped because memory could not be allocated. Please try scanning fewer locations at once."));
					}
					catch (const std::exception &error)
					{
						publishFailure(QStringLiteral("The scan stopped: %1").arg(QString::fromUtf8(error.what())));
					}
					catch (...)
					{
						publishFailure(QStringLiteral("The scan stopped because of an unexpected error."));
					}
				}, Qt::QueuedConnection);
			return;
		}
		catch (const std::bad_alloc &)
		{
			failure = QStringLiteral("The scan stopped because memory could not be allocated. Please try scanning fewer locations at once.");
		}
		catch (const std::exception &error)
		{
			failure = QStringLiteral("The scan stopped: %1").arg(QString::fromUtf8(error.what()));
		}
		catch (...)
		{
			failure = QStringLiteral("The scan stopped because of an unexpected error.");
		}
		// Unwind the failed scan's temporary owners before publishing failure.
		QMetaObject::invokeMethod(this, [this, failure] { publishFailure(failure); }, Qt::QueuedConnection);
	});
}

void MediaScanner::checkpoint(Checkpoint phase, qsizetype row)
{
	if (m_testCheckpoint)
		m_testCheckpoint(phase, row);
}

void MediaScanner::publishCompletion(ScanCompletion completion)
{
	checkpoint(Checkpoint::BeforePublication, completion.rows.size());
	{
		// This is the completion boundary. Cancel and acceptance cannot race
		// between deciding the outcome and reporting it to Console/the table.
		const std::lock_guard<std::mutex> lock(m_stateMutex);
		completion.cancelled |= m_state == ScanState::CancelRequested;
		m_state = ScanState::Publishing;
	}
	if (completion.cancelled)
		for (auto &issue : completion.issues)
			issue.scopeComplete = false;
	const QString module = QStringLiteral("scanner");
	QVector<LogMessage> logs{
		{QtInfoMsg, module, QStringLiteral("Scan %1: %2 files found")
			.arg(completion.cancelled ? QStringLiteral("cancelled") : QStringLiteral("complete")).arg(completion.rows.size())},
		{QtInfoMsg, module, QStringLiteral("Scan total including row formatting: %1 ms").arg(completion.elapsedMs)}};
	if (completion.cancelled)
		logs.append({QtInfoMsg, module, QStringLiteral("Scan cancelled by user; results are incomplete")});
	for (const auto &message : std::as_const(logs))
		Diagnostics::appendConsoleLine(message.level, message.module, message.message);
	emit scanLogBatch(logs);
	{
		// A queued result receiver may immediately start another scan. Its
		// BackgroundJob joins the old worker before resetting cancellation.
		const std::lock_guard<std::mutex> lock(m_stateMutex);
		m_state = ScanState::Idle;
	}
	emit scanFinished(completion.rows);
	emit scanIssuesFinished(completion.issues);
}

void MediaScanner::publishFailure(const QString &message)
{
	{
		const std::lock_guard<std::mutex> lock(m_stateMutex);
		m_state = ScanState::Idle;
	}
	reportFailure(message);
}

void MediaScanner::reportFailure(const QString &message)
{
	const QString module = QStringLiteral("scanner");
	Diagnostics::appendConsoleLine(QtCriticalMsg, module, message);
	emit scanLogBatch({{QtCriticalMsg, module, message}});
	emit scanFailed(message);
}

bool MediaScanner::cancelScan()
{
	const std::lock_guard<std::mutex> lock(m_stateMutex);
	if (m_state != ScanState::Active && m_state != ScanState::CancelRequested)
		return false;
	m_state = ScanState::CancelRequested;
	m_job.cancel();
	return true;
}

MediaScanner::ScanCompletion MediaScanner::doScan()
{
	QElapsedTimer timer;
	timer.start();
	Diagnostics::appendConsoleLine(QtInfoMsg, QStringLiteral("scanner"),
								   QStringLiteral("Scan starting: %1 volume(s), %2 added location(s)").arg(m_options.volumePaths.size()).arg(m_options.manualPaths.size()));
	const MediaEngine::Cancellation cancellation(&m_job.cancelFlag());
	MediaEngine::ScanRequest request;
	QVector<MediaRoot> contexts;
	QHash<QString, QString> labels;
	const auto addRoot = [&](MediaRoot root, const QString &label)
	{
		if (cancellation.cancelled())
			return;
		const QString physical = scannerFolderKey(root.path);
		if (cancellation.cancelled())
			return;
		if (!labels.contains(physical))
		{
			root.path = physical;
			contexts.append(root);
			labels.insert(physical, label);
			request.roots.append(physical);
		}
	};
	for (const auto &base : m_options.volumePaths)
	{
		if (cancellation.cancelled())
			break;
		emit scanDiscovering(base);
		if (cancellation.cancelled())
			break;
		if (!QFileInfo(base).isDir() || !QFileInfo(base).isReadable())
		{
			request.roots.append(base); // Preserve an unavailable requested scope as a discovery issue.
			continue;
		}
		const QString avid = childDirectory(base, Conventions::kAvidMediaFilesDir, &cancellation);
		const QString mxf = avid.isEmpty() ? QString{} : childDirectory(avid, Conventions::kMxfDir, &cancellation);
		const QString omf = m_options.includeOmf ? childDirectory(base, Conventions::kOmfMediaFilesDir, &cancellation) : QString{};
		if (!mxf.isEmpty())
			addRoot({AvidMediaLayout::Family::Mxf, mxf, base}, QDir(base).dirName());
		if (!omf.isEmpty())
			addRoot({AvidMediaLayout::Family::Omf, omf, base}, QDir(base).dirName());
	}
	for (const auto &path : m_options.manualPaths)
	{
		if (cancellation.cancelled())
			break;
		emit scanDiscovering(path);
		if (cancellation.cancelled())
			break;
		const auto roots = rootsForAddedPath(path, m_options.includeOmf, &cancellation);
		if (roots.isEmpty())
			request.roots.append(path); // Let discovery retain the inaccessible/unsupported scope diagnostic.
		for (const auto &root : roots)
			addRoot(root, QDir(path).dirName());
	}
	request.roots.removeDuplicates();
	request.omfScan = m_options.includeOmf;
	const qint64 rootLookupMs = timer.elapsed();
	QVector<LogMessage> logs;
	const auto flush = [&]
	{
		if (logs.isEmpty())
			return;
		// Persist from the worker: queued Console updates may arrive much later.
		for (const auto &message : std::as_const(logs))
			Diagnostics::appendConsoleLine(message.level, message.module, message.message);
		emit scanLogBatch(logs);
		logs.clear();
	};
	logs.append({QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("Scanning %1 location(s) with MediaEngine...").arg(request.roots.size())});
	logs.append({QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("Root lookup: %1 ms").arg(rootLookupMs)});
	flush();
	bool preparationReported = false;
	const auto reportPreparation = [&]
	{
		if (preparationReported)
			return;
		preparationReported = true;
		logs.append({QtInfoMsg, QStringLiteral("scanner"),
					 QStringLiteral("Discovery and source preparation: %1 ms").arg(timer.elapsed() - rootLookupMs)});
		flush();
	};
	MediaEngine::ScanCallbacks callbacks;
	QElapsedTimer sourceCheckpoint;
	sourceCheckpoint.start();
	callbacks.reading = [&](const MediaEngine::SourceCandidate &candidate)
	{
		const bool database = candidate.hint == MediaEngine::SourceCandidate::ReaderHint::Pmr ||
							  candidate.hint == MediaEngine::SourceCandidate::ReaderHint::Mdb;
		// Database starts are rare; throttle header checkpoints to avoid a log
		// write for every media file on large network scans.
		if (database || sourceCheckpoint.elapsed() >= kSourceCheckpointIntervalMs)
		{
			logs.append({QtInfoMsg, QStringLiteral("scanner"),
						 QStringLiteral("Reading %1: %2").arg(database ? QStringLiteral("database") : QStringLiteral("media header"), candidate.path)});
			flush();
			sourceCheckpoint.restart();
		}
	};
	callbacks.discovering = [&](const QString &path)
	{ emit scanDiscovering(path); };
	callbacks.progress = [&](int current, int total, const QString &path)
	{
		reportPreparation();
		emit scanProgress(current, total, path);
		if (current == 0)
			TestPause::sleepMs(TestPause::kPerScannedFolderMs);
	};
	callbacks.warning = [&](const QString &message)
	{
		logs.append({QtWarningMsg, QStringLiteral("mediaengine"), message});
		if (logs.size() >= 50)
			flush();
	};
	callbacks.finalising = [&]
	{
		logs.append({QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("Finalising scan: matching media and selecting metadata")});
		flush();
		emit scanFinalising();
	};
	auto session = QSharedPointer<MediaEngine::ScanResult>::create(MediaEngine::ScanEngine{}.scan(request, cancellation, callbacks));
	reportPreparation(); // Empty or cancelled scans may never reach source progress.
	logs.append({QtInfoMsg, QStringLiteral("scanner"),
				 QStringLiteral("Scan evidence: %1 source receipts plus extracted metadata and observations").arg(session->sources.size())});
	QVector<MediaFile> rows;
	rows.reserve(session->files.size());
	{
		// Capture read outcomes and safety flags once; the rows keep no scan owners.
		const ScanRowLookup rowChecks(*session);
		for (const auto &file : session->files)
		{
			checkpoint(Checkpoint::FormattingRow, rows.size());
			QString volumePath;
			QString volumeName;
			for (const auto &context : contexts)
				if (file.path.startsWith(context.path + QLatin1Char('/')))
				{
					volumePath = context.volumePath;
					volumeName = labels.value(context.path);
					break;
				}
			rows.append(mediaEngineMediaFile(file, rowChecks, volumePath, volumeName,
				session->cancelled || cancellation.cancelled()));
		}
	}
	for (const auto &issue : session->reconciliationIssues)
		callbacks.warning(QStringLiteral("%1: %2%3").arg(issue.expectedPath.isEmpty() && issue.source ? issue.source->path : issue.expectedPath, issue.explanation, issue.matchingPaths.isEmpty() ? QString{} : QStringLiteral("; matching locations: %1").arg(issue.matchingPaths.join(QStringLiteral("; ")))));
	QHash<QString, int> folderCounts;
	for (const auto &row : rows)
		if (!row.omfEra)
			++folderCounts[QFileInfo(row.mediaFilePath).absolutePath()];
	for (auto it = folderCounts.cbegin(); it != folderCounts.cend(); ++it)
		if (it.value() > Conventions::kFolderWarn)
			callbacks.warning(QStringLiteral("%1 contains %2 media files (folder warning threshold %3)").arg(it.key()).arg(it.value()).arg(Conventions::kFolderWarn));
	flush();
	// The UI replaces rows on scanFinished, which clears the previous issues.
	// Queue this scan's issues after that replacement.
	return {std::move(rows), session->reconciliationIssues, session->cancelled, timer.elapsed()};
}
