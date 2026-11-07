#include "bellaappscanner.h"

#include "bella/bellascanner.h"
#include "bellarowpresentation.h"
#include "conventions.h"
#include "diagnostics.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QThread>
#include <QTimer>
#include <exception>
#include <new>
#include <utility>

namespace
{
	const QString kModule = QStringLiteral("scanner");
	constexpr int kLogBatchSize = 50;
	constexpr qint64 kHeaderLogIntervalMs = 10000;
	constexpr int kReadinessRetryMs = 10;

	struct PreparedScan
	{
		QVector<MediaFile> rows;
		Bella::ScanOutcome outcome = Bella::ScanOutcome::Failed;
		bool discoveryComplete = false;
		qint64 elapsedMs = 0;
		QString failure;
	};

	Bella::ScanLocation::Kind bellaKind(OldScanner::Location::Kind kind)
	{
		switch (kind)
		{
		case OldScanner::Location::Kind::MediaBase:
			return Bella::ScanLocation::Kind::MediaBase;
		case OldScanner::Location::Kind::RequiredMediaBase:
			return Bella::ScanLocation::Kind::RequiredMediaBase;
		case OldScanner::Location::Kind::MediaTree:
			return Bella::ScanLocation::Kind::MediaTree;
		}
		Q_UNREACHABLE();
	}

	QString stageName(Bella::ScanStage stage)
	{
		switch (stage)
		{
		case Bella::ScanStage::ReadingDatabases:
			return BellaAppScanner::tr("Reading databases");
		case Bella::ScanStage::CheckingFiles:
			return BellaAppScanner::tr("Checking files");
		case Bella::ScanStage::ReadingHeaders:
			return BellaAppScanner::tr("Reading required headers");
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

struct BellaAppScanner::Run
{
	Bella::ScanControl control;
	// The organiser writes once. The facade reads only after the thread is ready.
	std::optional<PreparedScan> completion;

	PreparedScan prepare(const Bella::ScanRequest &request, const QPointer<BellaAppScanner> &owner);
};

PreparedScan BellaAppScanner::Run::prepare(const Bella::ScanRequest &request,
										   const QPointer<BellaAppScanner> &owner)
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
			for (const auto &message : std::as_const(logs))
				Diagnostics::appendConsoleLine(message.level, message.module, message.message);
			post([batch = std::exchange(logs, {})](BellaAppScanner &scanner)
				 { emit scanner.scanLogBatch(batch); });
		};
		const auto logProblem = [&](const Bella::ScanProblem &problem)
		{
			const auto message = problem.path.isEmpty() ? problem.reason
														: QStringLiteral("%1: %2").arg(problem.path, problem.reason);
			logs.append({QtWarningMsg, kModule, message});
			if (logs.size() >= kLogBatchSize)
				flush();
		};
		logs.append({QtInfoMsg, kModule, QStringLiteral("Scan starting: %1 location(s) with Bella").arg(request.locations.size())});
		flush();

		QElapsedTimer headerLog;
		headerLog.start();
		Bella::ScanStage stage = Bella::ScanStage::ReadingDatabases;
		qsizetype streamedProblems = 0;
		Bella::ScanCallbacks callbacks;
		callbacks.discovering = [&](const QString &path)
		{
			post([path](BellaAppScanner &scanner)
				 { emit scanner.scanDiscovering(path); });
		};
		callbacks.progress = [&](qsizetype, qsizetype, const QString &path)
		{
			const bool database = stage == Bella::ScanStage::ReadingDatabases;
			if (database || headerLog.hasExpired(kHeaderLogIntervalMs))
			{
				logs.append({QtInfoMsg, kModule, QStringLiteral("Reading %1: %2").arg(database ? QStringLiteral("database") : QStringLiteral("media header"), path)});
				flush();
				headerLog.restart();
			}
		};
		callbacks.stageProgress = [&](Bella::ScanStage next, qsizetype completed, qsizetype total, const QString &path)
		{
			stage = next;
			post([name = stageName(stage), completed, total, path](BellaAppScanner &scanner)
				 { emit scanner.scanProgress(name, completed, total, path); });
		};
		callbacks.warning = [&](const Bella::ScanProblem &problem)
		{
			logProblem(problem);
			++streamedProblems;
		};
		callbacks.finalising = [&]
		{
			logs.append({QtInfoMsg, kModule, QStringLiteral("Finalising scan: preparing media rows")});
			flush();
			post([](BellaAppScanner &scanner)
				 { emit scanner.scanFinalising(); });
		};

		auto result = Bella::scan(request, control, callbacks);
		// Organiser failures can append problems without going through the callback.
		for (qsizetype index = streamedProblems; index < result.problems.size(); ++index)
			logProblem(result.problems.at(index));
		flush();
		if (result.outcome == Bella::ScanOutcome::Failed)
			return {{}, result.outcome, result.discoveryComplete, timer.elapsed(), QStringLiteral("The scan stopped. See the Console for details.")};

		QVector<MediaFile> rows;
		rows.reserve(result.entries.size());
		QHash<QString, qsizetype> folderCounts;
		for (const auto &entry : std::as_const(result.entries))
		{
			rows.append(bellaMediaFile(entry));
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

BellaAppScanner::BellaAppScanner(QObject *parent) : QObject(parent)
{
}

BellaAppScanner::~BellaAppScanner()
{
	// A normal Close/Quit has already drained the scan. If an owner disappears
	// unexpectedly, the worker still owns its control and has a guarded delivery.
	if (m_run)
		m_run->control.cancel();
}

std::optional<BellaAppScanner::Location> BellaAppScanner::addedLocation(const QString &path)
{
	return OldScanner::addedLocation(path);
}

BellaAppScanner::Location BellaAppScanner::rescanLocation(const QString &path)
{
	return OldScanner::rescanLocation(path);
}

void BellaAppScanner::startScan(const Options &options)
{
	if (isActive())
		return;

	const QPointer<BellaAppScanner> owner(this);
	std::unique_ptr<QThread> thread;
	try
	{
		Bella::ScanRequest request;
		request.includeOmf = options.includeOmf;
		request.includePrecomputeDetails = options.includePrecomputeDetails;
		request.locations.reserve(options.locations.size());
		for (const auto &location : options.locations)
			request.locations.append({bellaKind(location.kind), location.path});

		const auto run = std::make_shared<Run>();
		thread.reset(QThread::create([run, owner, request = std::move(request)]
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

bool BellaAppScanner::cancelScan()
{
	if (!isActive() || m_publishing)
		return false;
	m_run->control.cancel();
	return true;
}

bool BellaAppScanner::isActive() const noexcept
{
	return m_worker || m_publishing;
}

void BellaAppScanner::finishWhenReady(QThread *worker, const QPointer<BellaAppScanner> &owner)
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

void BellaAppScanner::publishCompletion()
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
		publishFailure(completion.failure);
		return;
	}
	const bool cancelled = completion.outcome == Bella::ScanOutcome::Cancelled ||
						   run->control.cancelled();
	const QPointer<BellaAppScanner> owner(this);
	try
	{
		QString status = QStringLiteral("finished with incomplete discovery");
		if (cancelled)
			status = QStringLiteral("cancelled");
		else if (completion.discoveryComplete)
			status = QStringLiteral("complete");
		QVector<LogMessage> logs{
			{QtInfoMsg, kModule, QStringLiteral("Scan %1: %2 files found").arg(status).arg(completion.rows.size())},
			{QtInfoMsg, kModule, QStringLiteral("Scan total including row formatting: %1 ms").arg(completion.elapsedMs)}};
		if (cancelled)
			logs.append({QtInfoMsg, kModule, QStringLiteral("Scan cancelled by user; results are incomplete")});
		publishLog(logs);
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

void BellaAppScanner::publishFailure(const QString &message)
{
	m_publishing = true;
	const QPointer<BellaAppScanner> owner(this);
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

void BellaAppScanner::publishLog(const QVector<LogMessage> &batch)
{
	for (const auto &message : batch)
		Diagnostics::appendConsoleLine(message.level, message.module, message.message);
	emit scanLogBatch(batch);
}
