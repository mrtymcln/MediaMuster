#include "scan_manager.h"
#include "scan_result_p.h"

#include "scanner/bella_scanner.h"
#include "media/avid_media_layout.h"
#include "media/conventions.h"
#include "support/diagnostics.h"

#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QStorageInfo>
#include <QThread>
#include <QTimer>
#include <exception>
#include <filesystem>
#include <new>
#include <utility>

// MARK: - Scan helpers

namespace
{
	const QString kModule = QStringLiteral("scanner");
	constexpr int kConsoleWarningLimit = 5;
	constexpr int kReadinessRetryMs = 10;

	struct PreparedScan
	{
		QVector<MediaFileRecord> rows;
		Bella::ScanOutcome outcome = Bella::ScanOutcome::Failed;
		bool discoveryComplete = false;
		qint64 elapsedMs = 0;
		QString failure;
	};

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
		std::error_code error;
		std::filesystem::directory_iterator current(dir.filesystemPath(), error), end;
		while (!error && current != end)
		{
			const QFileInfo child(current->path());
			if (child.fileName().compare(name, Qt::CaseInsensitive) == 0)
			{
				std::error_code statusError;
				if (current->is_directory(statusError))
					return child.absoluteFilePath();
			}
			current.increment(error);
		}
		return {};
	}

	bool isAvidTree(const QString &path)
	{
		return QFileInfo(path).fileName().compare(Conventions::kAvidMediaFilesDir, Qt::CaseInsensitive) == 0;
	}

	QString stageName(Bella::ScanStage stage)
	{
		switch (stage)
		{
		case Bella::ScanStage::ReadingDatabases:
			return ScanManager::tr("Reading databases");
		case Bella::ScanStage::CheckingFiles:
			return ScanManager::tr("Checking files");
		case Bella::ScanStage::ReadingHeaders:
			return ScanManager::tr("Reading required headers");
		}
		Q_UNREACHABLE();
	}

	QString exceptionMessage() noexcept
	{
		try
		{
			throw;
		}
		catch (const std::bad_alloc &)
		{
			return QStringLiteral("The scan stopped because memory could not be allocated. Please try scanning fewer locations at once.");
		}
		catch (const std::exception &error)
		{
			try
			{
				return QStringLiteral("The scan stopped: %1").arg(QString::fromUtf8(error.what()));
			}
			catch (...)
			{
				// The short fallback also works when formatting the error runs out of memory.
			}
		}
		catch (...)
		{
		}
		return QStringLiteral("The scan stopped because of an unexpected error.");
	}
}

// MARK: - Scan worker

struct ScanManager::Run
{
	Bella::ScanControl control;
	qsizetype warningCount = 0;
	// The organiser writes once. The manager reads after the thread is ready.
	std::optional<PreparedScan> completion;

	PreparedScan prepare(const Bella::ScanRequest &request, const QPointer<ScanManager> &owner);
};

PreparedScan ScanManager::Run::prepare(const Bella::ScanRequest &request,
									   const QPointer<ScanManager> &owner)
{
	QElapsedTimer timer;
	timer.start();
	try
	{
		// The thread object lives on the GUI thread and outlives the callable.
		// Callbacks can queue events without accessing a window or facade from here.
		const auto post = [worker = QThread::currentThread(), owner](auto action)
		{
			QMetaObject::invokeMethod(worker, [worker, owner, action = std::move(action)]
									  {
					auto *scanner = owner.data();
					if (scanner && scanner->m_worker == worker && !scanner->m_publishing)
						action(*scanner); }, Qt::QueuedConnection);
		};

		QVector<LogMessage> logs;
		const auto flush = [&]
		{
			if (logs.isEmpty())
				return;
			post([batch = std::exchange(logs, {})](ScanManager &scanner)
				 { emit scanner.scanLogBatch(batch); });
		};
		const auto logProblem = [&](const Bella::ScanProblem &problem)
		{
			const auto message = problem.path.isEmpty() ? problem.reason
														: QStringLiteral("%1: %2").arg(problem.path, problem.reason);
			// Keep every source diagnostic in the file while bounding live Console output.
			Diagnostics::appendConsoleLine(QtWarningMsg, kModule, message);
			++warningCount;
			if (warningCount <= kConsoleWarningLimit)
				logs.append({QtWarningMsg, kModule, message});
			else if (warningCount == kConsoleWarningLimit + 1)
			{
				const QString notice = QStringLiteral("Got to Help > Reveal Diagnostics for more info.");
				Diagnostics::appendConsoleLine(QtWarningMsg, kModule, notice);
				logs.append({QtWarningMsg, kModule, notice});
				flush();
			}
			if (logs.size() >= kConsoleWarningLimit)
				flush();
		};
		const auto locationCount = request.locations.size();
		const QString started = QStringLiteral("Scan starting: %1 %2.")
									.arg(locationCount)
									.arg(locationCount == 1 ? QStringLiteral("location") : QStringLiteral("locations"));
		Diagnostics::appendConsoleLine(QtInfoMsg, kModule, started);
		logs.append({QtInfoMsg, kModule, started});
		flush();

		qsizetype streamedProblems = 0;
		Bella::ScanCallbacks callbacks;
		callbacks.discovering = [&](const QString &path)
		{
			post([path](ScanManager &scanner)
				 { emit scanner.scanDiscovering(path); });
		};
		callbacks.stageProgress = [&](Bella::ScanStage next, qsizetype completed, qsizetype total, const QString &path)
		{
			post([name = stageName(next), completed, total, path](ScanManager &scanner)
				 { emit scanner.scanProgress(name, completed, total, path); });
		};
		callbacks.warning = [&](const Bella::ScanProblem &problem)
		{
			logProblem(problem);
			++streamedProblems;
		};
		callbacks.finalising = [&]
		{
			flush();
			post([](ScanManager &scanner)
				 { emit scanner.scanFinalising(); });
		};

		auto result = Bella::scan(request, control, callbacks);
		// Organiser failures can append problems without going through the callback.
		for (qsizetype index = streamedProblems; index < result.problems.size(); ++index)
			logProblem(result.problems.at(index));
		flush();
		if (result.outcome == Bella::ScanOutcome::Failed)
			return {{}, result.outcome, result.discoveryComplete, timer.elapsed(), QStringLiteral("The scan stopped. See the Console for details.")};

		QVector<MediaFileRecord> rows;
		rows.reserve(result.entries.size());
		QHash<QString, qsizetype> folderCounts;
		for (const auto &entry : std::as_const(result.entries))
		{
			rows.append(mediaFileRecordFromScanEntry(entry));
			if (!entry.omfEra)
				++folderCounts[QFileInfo(entry.path).absolutePath()];
		}
		for (auto it = folderCounts.cbegin(); it != folderCounts.cend(); ++it)
			if (it.value() > Conventions::kFolderWarn)
				logProblem({it.key(), QStringLiteral("contains %1 media files (folder warning threshold %2)")
										  .arg(it.value())
										  .arg(Conventions::kFolderWarn)});
		flush();
		return {std::move(rows), result.outcome, result.discoveryComplete, timer.elapsed(), {}};
	}
	catch (...)
	{
		return {{}, Bella::ScanOutcome::Failed, false, timer.elapsed(), exceptionMessage()};
	}
}

// MARK: - Lifecycle

ScanManager::ScanManager(QObject *parent) : QObject(parent)
{
}

ScanManager::~ScanManager()
{
	// A normal Close/Quit has already drained the scan. If an owner disappears
	// unexpectedly, the worker still owns its control and has a guarded delivery.
	if (m_run)
		m_run->control.cancel();
}

// MARK: - Scan locations

std::optional<ScanManager::Location> ScanManager::addedLocation(const QString &path)
{
	if (path.isEmpty())
		return std::nullopt;
	const QFileInfo requested(path);
	if (!requested.isDir() || !requested.isReadable())
		return std::nullopt;
	const QString folder = scannerFolderKey(path);
	if (AvidMediaLayout::isInsideUmeRoot(path) || AvidMediaLayout::isInsideUmeRoot(folder))
		return std::nullopt;
	const auto hasMxfTree = [](const QString &tree)
	{
		const QString root = childDirectory(tree, Conventions::kMxfDir);
		return !root.isEmpty() && AvidMediaLayout::isMxfRoot(scannerFolderKey(root));
	};
	if ((isAvidTree(folder) && hasMxfTree(folder)) || AvidMediaLayout::isOmfRoot(folder))
		return Location{Location::Kind::MediaTree, folder};
	const QStorageInfo storage(folder);
	if (!storage.isValid() || !storage.isReady() || storage.rootPath().isEmpty())
		return std::nullopt;
	const QString volumeRoot = scannerFolderKey(storage.rootPath());
#ifdef Q_OS_WIN
	if (volumeRoot.compare(folder, Qt::CaseInsensitive) != 0)
#else
	if (volumeRoot != folder)
#endif
		return std::nullopt;
	const QString avid = childDirectory(folder, Conventions::kAvidMediaFilesDir);
	const QString omf = childDirectory(folder, Conventions::kOmfMediaFilesDir);
	if ((!avid.isEmpty() && hasMxfTree(avid)) ||
		(!omf.isEmpty() && AvidMediaLayout::isOmfRoot(scannerFolderKey(omf))))
		return Location{Location::Kind::RequiredMediaBase, folder};
	return std::nullopt;
}

ScanManager::Location ScanManager::rescanLocation(const QString &path)
{
	QString folder = path.isEmpty() ? QString{} : QDir::cleanPath(QDir::fromNativeSeparators(path));
	if (AvidMediaLayout::isMxfRoot(folder))
		folder = QFileInfo(folder).absolutePath();
	const bool tree = isAvidTree(folder) || AvidMediaLayout::isOmfRoot(folder);
	return {tree ? Location::Kind::MediaTree : Location::Kind::RequiredMediaBase, folder};
}

// MARK: - Scan control

void ScanManager::startScan(const Bella::ScanRequest &request)
{
	if (isActive())
		return;

	const QPointer<ScanManager> owner(this);
	std::unique_ptr<QThread> thread;
	try
	{
		const auto run = std::make_shared<Run>();
		thread.reset(QThread::create([run, owner, request]
									 { run->completion = run->prepare(request, owner); }));
		auto *worker = thread.get();
		connect(worker, &QThread::finished, worker, [worker, owner]
				{ finishWhenReady(worker, owner); }, Qt::QueuedConnection);
		m_run = run;
	}
	catch (...)
	{
		publishFailure(exceptionMessage());
		return;
	}

	auto *worker = thread.release();
	m_worker = worker;
	worker->start();
	// Qt does not emit finished if the platform could not start the thread.
	if (!worker->isRunning() && worker->wait(0) && !m_run->completion)
		finishWhenReady(worker, owner);
}

bool ScanManager::cancelScan()
{
	if (!isActive() || m_publishing)
		return false;
	m_run->control.cancel();
	return true;
}

bool ScanManager::isActive() const noexcept
{
	return m_worker || m_publishing;
}

// MARK: - Completion and reporting

void ScanManager::finishWhenReady(QThread *worker, const QPointer<ScanManager> &owner)
{
	if (!worker->wait(0))
	{
		// A finished signal can precede Qt's thread cleanup. Retry only readiness.
		QTimer::singleShot(kReadinessRetryMs, worker, [worker, owner]
						   { finishWhenReady(worker, owner); });
		return;
	}
	worker->deleteLater();
	if (owner && owner->m_worker == worker)
		owner->publishCompletion();
}

void ScanManager::publishCompletion()
{
	m_publishing = true;
	const auto run = std::move(m_run);
	if (!run->completion)
	{
		publishFailure(QStringLiteral("The scan could not start a background worker."));
		return;
	}
	const auto completion = std::move(*run->completion);
	if (completion.outcome == Bella::ScanOutcome::Failed)
	{
		try
		{
			publishFailure(QStringLiteral("Scan failed: 0 files, %1 ms, %2 %3. %4")
							   .arg(completion.elapsedMs)
							   .arg(run->warningCount)
							   .arg(run->warningCount == 1 ? QStringLiteral("warning") : QStringLiteral("warnings"))
							   .arg(completion.failure));
		}
		catch (...)
		{
			// A memory failure must still reach the existing failure handoff.
			publishFailure(completion.failure);
		}
		return;
	}
	const bool cancelled = completion.outcome == Bella::ScanOutcome::Cancelled ||
						   run->control.cancelled();
	const QPointer<ScanManager> owner(this);
	try
	{
		QString status = QStringLiteral("finished with incomplete discovery");
		if (cancelled)
			status = QStringLiteral("cancelled");
		else if (completion.discoveryComplete)
			status = QStringLiteral("complete");
		const auto fileCount = completion.rows.size();
		QString summary = QStringLiteral("Scan %1: %2 %3, %4 ms, %5 %6")
							  .arg(status)
							  .arg(fileCount)
							  .arg(fileCount == 1 ? QStringLiteral("file") : QStringLiteral("files"))
							  .arg(completion.elapsedMs)
							  .arg(run->warningCount)
							  .arg(run->warningCount == 1 ? QStringLiteral("warning") : QStringLiteral("warnings"));
		if (cancelled)
			summary += QStringLiteral("; results are incomplete.");
		else
			summary += QLatin1Char('.');
		const auto level = !cancelled && (!completion.discoveryComplete || run->warningCount)
							   ? QtWarningMsg
							   : QtInfoMsg;
		publishLog({{level, kModule, summary}});
	}
	catch (...)
	{
		if (owner)
			publishFailure(exceptionMessage());
		return;
	}
	if (!owner)
		return;

	m_worker = nullptr;
	m_publishing = false;
	emit scanFinished(completion.rows, cancelled, completion.discoveryComplete);
}

void ScanManager::publishFailure(const QString &message)
{
	m_publishing = true;
	const QPointer<ScanManager> owner(this);
	try
	{
		publishLog({{QtCriticalMsg, kModule, message}});
	}
	catch (...)
	{
		// A failed diagnostic allocation must not strand the scan controls.
	}
	if (!owner)
		return;

	m_worker = nullptr;
	m_publishing = false;
	emit scanFailed(message);
}

void ScanManager::publishLog(const QVector<LogMessage> &batch)
{
	for (const auto &message : batch)
		Diagnostics::appendConsoleLine(message.level, message.module, message.message);
	emit scanLogBatch(batch);
}
