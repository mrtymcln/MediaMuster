// Prepares exact media roots, owns the background scan and forwards results.
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
#include <QSet>
#include <QStorageInfo>
#include <exception>
#include <filesystem>
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
						   const MediaEngine::Cancellation *cancellation = nullptr,
						   QVector<MediaEngine::DiscoveryIssue> *issues = nullptr)
	{
		const auto reportError = [&](const QString &path, const std::error_code &error)
		{
			if (issues)
				issues->append({MediaEngine::DiscoveryIssue::Kind::UnreadableFolder, path,
								QStringLiteral("Cannot inspect media folders: %1").arg(QString::fromLocal8Bit(error.message().c_str()))});
		};
		if (cancellation && cancellation->cancelled())
			return {};
		const QDir dir(parent);
		const QString expected = dir.filePath(name);
		if (QFileInfo(expected).isDir())
			return expected;
		if (cancellation && cancellation->cancelled())
			return {};
		std::error_code error;
		std::filesystem::directory_iterator current(dir.filesystemPath(), error), end;
		while (!error && current != end)
		{
			if (cancellation && cancellation->cancelled())
				return {};
			const QFileInfo child(current->path());
			if (child.fileName().compare(name, Qt::CaseInsensitive) == 0)
			{
				std::error_code statusError;
				if (current->is_directory(statusError))
					return child.absoluteFilePath();
				if (statusError)
					reportError(child.absoluteFilePath(), statusError);
			}
			current.increment(error);
		}
		if (error)
			reportError(parent, error);
		return {};
	}

	bool isAvidTree(const QString &path)
	{
		return QFileInfo(path).fileName().compare(Conventions::kAvidMediaFilesDir, Qt::CaseInsensitive) == 0;
	}
}

struct MediaScanner::FolderList
{
	struct MediaRoot
	{
		QString path;
		QString volumePath;
		QString volumeName;
	};
	MediaEngine::ScanRequest request;
	QVector<MediaRoot> contexts;
};

MediaScanner::MediaScanner(QObject *parent)
	: QObject(parent)
{
}

std::optional<MediaScanner::Location> MediaScanner::addedLocation(const QString &requestedPath)
{
	if (requestedPath.isEmpty())
		return std::nullopt;
	const QFileInfo requested(requestedPath);
	if (!requested.isDir() || !requested.isReadable())
		return std::nullopt;
	const QString path = scannerFolderKey(requestedPath);
	if (AvidMediaLayout::isInsideUmeRoot(requestedPath) || AvidMediaLayout::isInsideUmeRoot(path))
		return std::nullopt;
	const auto hasMxfTree = [](const QString &tree)
	{
		const QString root = childDirectory(tree, Conventions::kMxfDir);
		return !root.isEmpty() && AvidMediaLayout::isMxfRoot(scannerFolderKey(root));
	};
	if ((isAvidTree(path) && hasMxfTree(path)) || AvidMediaLayout::isOmfRoot(path))
		return Location{Location::Kind::MediaTree, path};
	const QStorageInfo storage(path);
	if (!storage.isValid() || !storage.isReady() || storage.rootPath().isEmpty())
		return std::nullopt;
	const QString volumeRoot = scannerFolderKey(storage.rootPath());
#ifdef Q_OS_WIN
	if (volumeRoot.compare(path, Qt::CaseInsensitive) != 0)
#else
	if (volumeRoot != path)
#endif
		return std::nullopt;
	const QString avid = childDirectory(path, Conventions::kAvidMediaFilesDir);
	const QString omf = childDirectory(path, Conventions::kOmfMediaFilesDir);
	if ((!avid.isEmpty() && hasMxfTree(avid)) ||
		(!omf.isEmpty() && AvidMediaLayout::isOmfRoot(scannerFolderKey(omf))))
		return Location{Location::Kind::RequiredMediaBase, path};
	return std::nullopt;
}

MediaScanner::Location MediaScanner::rescanLocation(const QString &requestedPath)
{
	QString path = requestedPath.isEmpty() ? QString{} : QDir::cleanPath(QDir::fromNativeSeparators(requestedPath));
	if (AvidMediaLayout::isMxfRoot(path))
		path = QFileInfo(path).absolutePath();
	const bool tree = isAvidTree(path) || AvidMediaLayout::isOmfRoot(path);
	return {tree ? Location::Kind::MediaTree : Location::Kind::RequiredMediaBase, path};
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
		QMetaObject::invokeMethod(this, [this, failure] { publishFailure(failure); }, Qt::QueuedConnection); });
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
		{QtInfoMsg, module, QStringLiteral("Scan %1: %2 files found").arg(completion.cancelled ? QStringLiteral("cancelled") : QStringLiteral("complete")).arg(completion.rows.size())},
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

MediaScanner::FolderList MediaScanner::buildFolderList(const MediaEngine::Cancellation &cancellation)
{
	MediaEngine::ScanRequest request;
	QVector<FolderList::MediaRoot> contexts;
	QSet<QString> preparedRoots;
	const auto addRoot = [&](const QString &root, const QString &volumePath, const QString &label)
	{
		if (cancellation.cancelled())
			return false;
		const QString physical = scannerFolderKey(root);
		if (cancellation.cancelled())
			return false;
		if (AvidMediaLayout::isInsideUmeRoot(root) || AvidMediaLayout::isInsideUmeRoot(physical) ||
			(!AvidMediaLayout::isMxfRoot(physical) && !AvidMediaLayout::isOmfRoot(physical)))
		{
			request.preparationIssues.append({MediaEngine::DiscoveryIssue::Kind::UnmanagedRoot, root,
											  QStringLiteral("Requested location is not a supported MXF or OMFI media root")});
			return false;
		}
		if (!preparedRoots.contains(physical))
		{
			preparedRoots.insert(physical);
			contexts.append({physical, volumePath, label});
			request.roots.append(physical);
		}
		return true;
	};
	for (const auto &location : m_options.locations)
	{
		if (cancellation.cancelled())
			break;
		if (location.path.isEmpty())
		{
			request.preparationIssues.append({MediaEngine::DiscoveryIssue::Kind::UnavailableRoot, {}, QStringLiteral("No directory was supplied for this scan location")});
			continue;
		}
		emit scanDiscovering(location.path);
		if (cancellation.cancelled())
			break;
		const QString path = scannerFolderKey(location.path);
		if (cancellation.cancelled())
			break;
		if (AvidMediaLayout::isInsideUmeRoot(location.path) || AvidMediaLayout::isInsideUmeRoot(path))
		{
			request.preparationIssues.append({MediaEngine::DiscoveryIssue::Kind::UnmanagedRoot, location.path,
											  QStringLiteral("UME media is outside the supported scan scope")});
			continue;
		}
		if (location.kind == Location::Kind::MediaTree && AvidMediaLayout::isOmfRoot(path) && !m_options.includeOmf)
			continue;
		const QFileInfo info(path);
		if (!info.isDir() || !info.isReadable())
		{
			request.preparationIssues.append({MediaEngine::DiscoveryIssue::Kind::UnavailableRoot, location.path,
											  QStringLiteral("Requested directory is unavailable or unreadable")});
			continue;
		}
		if (cancellation.cancelled())
			break;
		const QString label = QDir(location.path).dirName();
		if (location.kind != Location::Kind::MediaTree)
		{
			const bool required = location.kind == Location::Kind::RequiredMediaBase;
			const QString volumePath = required ? path : location.path;
			const auto issuesBefore = request.preparationIssues.size();
			bool found = false;
			const QString avid = childDirectory(path, Conventions::kAvidMediaFilesDir, &cancellation, &request.preparationIssues);
			const QString mxf = avid.isEmpty() ? QString{} : childDirectory(avid, Conventions::kMxfDir, &cancellation, &request.preparationIssues);
			if (!mxf.isEmpty())
				found = addRoot(mxf, volumePath, label);
			const QString omf = m_options.includeOmf || required
									? childDirectory(path, Conventions::kOmfMediaFilesDir, &cancellation, &request.preparationIssues)
									: QString{};
			if (!omf.isEmpty())
			{
				// A disabled OMF tree is recognized without reading its contents.
				const bool legacyFound = !cancellation.cancelled() && (m_options.includeOmf
																		   ? addRoot(omf, volumePath, label)
																		   : AvidMediaLayout::isOmfRoot(scannerFolderKey(omf)));
				found = found || legacyFound;
			}
			if (required && !found && !cancellation.cancelled() && request.preparationIssues.size() == issuesBefore)
				request.preparationIssues.append({MediaEngine::DiscoveryIssue::Kind::UnmanagedRoot, location.path,
												  QStringLiteral("No managed media trees are present at the requested location")});
		}
		else if (isAvidTree(path))
		{
			const auto issuesBefore = request.preparationIssues.size();
			const QString mxf = childDirectory(path, Conventions::kMxfDir, &cancellation, &request.preparationIssues);
			if (!mxf.isEmpty())
				addRoot(mxf, path, label);
			else if (!cancellation.cancelled() && request.preparationIssues.size() == issuesBefore)
				request.preparationIssues.append({MediaEngine::DiscoveryIssue::Kind::UnmanagedRoot, location.path,
												  QStringLiteral("No MXF media root is present in the selected Avid MediaFiles folder")});
		}
		else if (AvidMediaLayout::isOmfRoot(path))
			addRoot(path, info.absolutePath(), label);
		else
			request.preparationIssues.append({MediaEngine::DiscoveryIssue::Kind::UnmanagedRoot, location.path,
											  QStringLiteral("Select an Avid MediaFiles or OMFI MediaFiles tree")});
	}
	request.omfScan = m_options.includeOmf;
	return {std::move(request), std::move(contexts)};
}

MediaScanner::ScanCompletion MediaScanner::doScan()
{
	QElapsedTimer timer;
	timer.start();
	Diagnostics::appendConsoleLine(QtInfoMsg, QStringLiteral("scanner"),
								   QStringLiteral("Scan starting: %1 location(s)").arg(m_options.locations.size()));
	const MediaEngine::Cancellation cancellation(&m_job.cancelFlag());
	const auto folders = buildFolderList(cancellation);
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
	logs.append({QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("Scanning %1 location(s) with MediaEngine...").arg(folders.request.roots.size())});
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
	auto session = QSharedPointer<MediaEngine::ScanResult>::create(MediaEngine::ScanEngine{}.scan(folders.request, cancellation, callbacks));
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
			for (const auto &context : folders.contexts)
				if (file.path.startsWith(context.path + QLatin1Char('/')))
				{
					volumePath = context.volumePath;
					volumeName = context.volumeName;
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
