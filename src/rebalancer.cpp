#include "rebalancer.h"
#include "rebalanceplanner.h"

// MARK: - Construction

Rebalancer::Rebalancer(QObject *parent) : QObject(parent)
{
	m_engine = new OpManager(this);

	// Adapt shared-engine progress and outcomes to the dialog's signals.
	connect(m_engine, &OpManager::operationProgress, this,
			[this](const QString &name, int current, int total, double)
			{ emit progress(current, total, name); });
	connect(m_engine, &OpManager::operationLog, this,
			[this](QtMsgType level, const QString &message)
			{ emit log(level, message); });
	connect(m_engine, &OpManager::operationResult, this,
			[this](const OpResult &result)
			{
				emit operationResult(result);
				if (result.state != OpResult::State::Completed)
					emit log(QtWarningMsg, result.name + ": " + result.message);
			});
	connect(m_engine, &OpManager::operationFinished, this,
			[this](int succeeded, int failed)
			{
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
	const auto requestId = ++m_preparationRequestId;
	m_cancelRequested.store(false, std::memory_order_release);

	// Build the grouped request off the GUI thread. The engine owns every
	// filesystem change, including folder creation and database retirement.
	m_preflight.start(
		[this, plan, requestId]
		{
			// MARK: Build the engine request, group-contiguously
			//
			// Relatives (one master clip's video + audio essence) are
			// contiguous in the item order and share a groupKey, and the
			// engine's Rename machine only honours cancel at group
			// boundaries — so cancellation does not split a group. I/O failure stops
			// the run with every completed move recorded for recovery.
			OpRequest req = RebalancePlanner::requestForPlan(plan);

			const bool cancelled = m_preflight.isCancelled();
			const bool unavailable = !plan.ops.isEmpty() && req.items.isEmpty();

			// Deliver preparation outcomes on the owner thread so a replacement
			// can invalidate both queued starts and queued failure messages.
			QMetaObject::invokeMethod(
				this, [this, requestId, cancelled, unavailable, req = std::move(req)]() mutable
				{
					if (requestId != m_preparationRequestId)
						return;
					if (cancelled)
						emit finished(0, 0, /*cancelled=*/true);
					else if (unavailable)
						emit aborted(tr("The media files are unavailable. Rescan and try again."));
					else
						startEngineRun(std::move(req)); },
				Qt::QueuedConnection);
		});
}

void Rebalancer::startEngineRun(OpRequest request)
{
	// Cancel may arrive after preparation queued this handoff.
	if (m_cancelRequested.load(std::memory_order_acquire))
	{
		emit finished(0, 0, /*cancelled=*/true);
		return;
	}
	// The engine takes it from here: write-ahead journal, identity gates,
	// per-rename recovery coverage, undo candidacy. Its signals were
	// adapted onto ours in the constructor.
	emit log(QtInfoMsg, tr("Rebalance started."));
	m_engine->execute(std::move(request));
}
