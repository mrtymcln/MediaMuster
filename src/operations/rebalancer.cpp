#include "rebalancer.h"
#include "rebalance_planner.h"

#include <QDir>

// MARK: - Construction

Rebalancer::Rebalancer(QObject *parent) : QObject(parent)
{
	m_engine = new OperationManager(this);

	// Adapt shared-engine progress and outcomes to the dialog's signals.
	connect(m_engine, &OperationManager::operationProgress, this,
			[this](const QString &name, int current, int total, double)
			{ emit progress(current, total, name); });
	connect(m_engine, &OperationManager::operationLog, this,
			[this](QtMsgType level, const QString &message)
			{ emit log(level, message); });
	connect(m_engine, &OperationManager::operationResult, this,
			[this](const OperationResult &result)
			{
				emit operationResult(result);
				if (result.state == OperationResult::State::Failed ||
					result.state == OperationResult::State::NeedsAttention ||
					result.state == OperationResult::State::SourceRetained ||
					(result.state == OperationResult::State::Completed && !result.message.isEmpty()))
					emit log(QtWarningMsg, result.name + ": " + result.message);
			});
	connect(m_engine, &OperationManager::operationFinished, this,
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
			QString refusal;
			for (const auto &move : plan.ops)
			{
				const auto &item = move.scannedItem;
				if (!item || !item->headerIdentity || item->headerIdentity->unavailableReason.isEmpty())
					continue;
				// Refuse before the runner can create folders or retire Avid databases.
				refusal = tr("Rebalance needs a fresh scan before it can start.\n\n%1\n%2")
							  .arg(QDir::toNativeSeparators(move.srcPath), item->headerIdentity->unavailableReason);
				break;
			}

			OperationRequest req;
			if (refusal.isEmpty())
			{
				req = RebalancePlanner::requestForPlan(plan);
				if (!plan.ops.isEmpty() && req.items.isEmpty())
					refusal = tr("The media files are unavailable. Rescan and try again.");
			}

			const bool cancelled = m_preflight.isCancelled();

			// Deliver preparation outcomes on the owner thread so a replacement
			// can invalidate both queued starts and queued failure messages.
			QMetaObject::invokeMethod(
				this, [this, requestId, cancelled, refusal = std::move(refusal), req = std::move(req)]() mutable
				{
					if (requestId != m_preparationRequestId)
						return;
					if (cancelled)
						emit finished(0, 0, /*cancelled=*/true);
					else if (!refusal.isEmpty())
						emit aborted(refusal);
					else
						startEngineRun(std::move(req)); },
				Qt::QueuedConnection);
		});
}

void Rebalancer::startEngineRun(OperationRequest request)
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
	m_engine->execute(std::move(request));
}
