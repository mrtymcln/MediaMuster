#include "rebalancer.h"
#include "rebalanceplanner.h"
#include "formatutil.h"

// MARK: - Construction

Rebalancer::Rebalancer(QObject *parent) : QObject(parent)
{
	m_engine = new OpManager(this);

	// The Avid per-folder database reset itself lives in the ENGINE's
	// Rename machine (so an undo of a rebalance resets them too, and the
	// honest-absence ordering is enforced in one place — see
	// oprunner.cpp's touchFolder). This hook only counts the folders for
	// the summary line. (Runs on the engine's worker thread.)
	m_engine->renameFolderTouched = [this](const QString &)
	{ m_foldersReset.fetch_add(1, std::memory_order_relaxed); };

	// Signal adaptation, once: the engine speaks OpManager, the dialog
	// speaks Rebalancer, and RebalanceDialog's four connects stay
	// exactly as they were.
	connect(m_engine, &OpManager::operationProgress, this,
			[this](const QString &name, int current, int total, double)
			{ emit progress(current, total, name); });
	connect(m_engine, &OpManager::operationLog, this,
			[this](QtMsgType level, const QString &message) { emit log(level, message); });
	connect(m_engine, &OpManager::operationResult, this,
			[this](const OpResult &result)
			{
				if (result.state != OpResult::State::Completed)
					emit log(QtWarningMsg, result.name + ": " + result.message);
			});
	connect(m_engine, &OpManager::operationFinished, this,
			[this](int succeeded, int failed)
			{
				const int reset = m_foldersReset.load(std::memory_order_relaxed);
				if (reset > 0)
					emit log(QtInfoMsg, QStringLiteral("Avid databases reset in %1 folder(s); Avid "
													   "rebuilds them on next launch.")
											.arg(Format::count(reset)));
				emit finished(succeeded, failed, m_cancelRequested.load(std::memory_order_acquire));
			});
}

Rebalancer::~Rebalancer()
{
	// Both workers can access our members. QObject child destruction runs
	// after those members are gone, so join the engine here as well as the
	// pre-flight job. A blocked filesystem call can delay this safe shutdown.
	cancel();
	m_preflight.shutdown();
	delete m_engine;
	m_engine = nullptr;
}

// MARK: - Execution

void Rebalancer::executeAsync(const RebalancePlan &plan)
{
	m_cancelRequested.store(false, std::memory_order_release);
	m_foldersReset.store(0, std::memory_order_relaxed);

	// Build the grouped request off the GUI thread. The engine owns every
	// filesystem change, including folder creation and database retirement.
	m_preflight.start(
		[this, plan]
		{
			// MARK: Build the engine request, group-contiguously
			//
			// Relatives (one master clip's video + audio essence) are
			// contiguous in the item order and share a groupKey, and the
			// engine's Rename machine only honours cancel at group
			// boundaries — so cancellation does not split a group. I/O failure stops
			// the run with every completed move recorded for recovery.
			OpRequest req = RebalancePlanner::requestForPlan(plan);

			// A cancel that raced the pre-flight: stop before dispatch.
			if (m_preflight.isCancelled())
			{
				emit finished(0, 0, /*cancelled=*/true);
				return;
			}

			// Phase 2 must start from the GUI thread — BackgroundJob's
			// start() manages its worker from its owner's thread — so
			// hop back queued. This worker then exits.
			QMetaObject::invokeMethod(
				this, [this, req = std::move(req)]() mutable { startEngineRun(std::move(req)); },
				Qt::QueuedConnection);
		});
}

void Rebalancer::startEngineRun(OpRequest request)
{
	// The engine takes it from here: write-ahead journal, identity gates,
	// per-rename recovery coverage, undo candidacy. Its signals were
	// adapted onto ours in the constructor.
	m_engine->execute(std::move(request));
}
