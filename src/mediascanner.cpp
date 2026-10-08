// Owns the background scan and forwards progress/results to the existing UI.
// Canon owns discovery, source parsing, matching and metadata selection.
#include "mediascanner.h"
#include "canon/scanengine.h"
#include "canonadapter.h"
#include "avidmedialayout.h"
#include "conventions.h"
#include "testpause.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QScopeGuard>

namespace
{
	QString scannerFolderKey(const QString &path)
	{
		const QFileInfo info(path);
		const QString canonical = info.canonicalFilePath();
		return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
	}

	QString childDirectory(const QString &parent, QLatin1String name,
						   const Canon::Cancellation *cancellation = nullptr)
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
									   const Canon::Cancellation *cancellation = nullptr)
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
	bool expected = false;
	if (!m_running.compare_exchange_strong(expected, true))
		return;
	m_options = options;
	m_job.start([this]
				{
		const auto running = qScopeGuard([this] { m_running.store(false); });
		doScan(); });
}

void MediaScanner::cancelScan()
{
	m_job.cancel();
}

void MediaScanner::doScan()
{
	QElapsedTimer timer;
	timer.start();
	const Canon::Cancellation cancellation(&m_job.cancelFlag());
	Canon::ScanRequest request;
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
		emit scanLogBatch(logs);
		logs.clear();
	};
	logs.append({QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("Scanning %1 location(s) with Canon...").arg(request.roots.size())});
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
	Canon::ScanCallbacks callbacks;
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
		logs.append({QtWarningMsg, QStringLiteral("canon"), message});
		if (logs.size() >= 50)
			flush();
	};
	callbacks.finalising = [&]
	{ emit scanFinalising(); };
	auto session = QSharedPointer<Canon::ScanResult>::create(Canon::ScanEngine{}.scan(request, cancellation, callbacks));
	reportPreparation(); // Empty or cancelled scans may never reach source progress.
	QVector<MediaFile> rows;
	rows.reserve(session->files.size());
	for (const auto &file : session->files)
	{
		QString volumePath;
		QString volumeName;
		for (const auto &context : contexts)
			if (file.path.startsWith(context.path + QLatin1Char('/')))
			{
				volumePath = context.volumePath;
				volumeName = labels.value(context.path);
				break;
			}
		rows.append(canonMediaFile(file, session, volumePath, volumeName));
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
	logs.append({QtInfoMsg, QStringLiteral("scanner"),
				 QStringLiteral("Scan %1: %2 files found").arg(session->cancelled ? QStringLiteral("cancelled") : QStringLiteral("complete")).arg(rows.size())});
	logs.append({QtInfoMsg, QStringLiteral("scanner"),
				 QStringLiteral("Scan total including row formatting: %1 ms").arg(timer.elapsed())});
	if (session->cancelled)
		logs.append({QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("Scan cancelled by user")});
	flush();
	// The UI replaces rows on scanFinished, which clears the previous issues.
	// Queue this scan's issues after that replacement.
	emit scanFinished(rows);
	emit scanIssuesFinished(session->reconciliationIssues);
}
