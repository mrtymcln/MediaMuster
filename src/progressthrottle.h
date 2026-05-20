#pragma once

#include <QElapsedTimer>
#include <atomic>

/// Caps progress-signal emit frequency to ~one per `minIntervalMs`.
/// Stops MediaManager's copy loop and MediaScanner's folder walk /
/// MXF parser passes from flooding the event loop.
///
/// `shouldEmit()` is thread-safe: an atomic timestamp plus CAS means
/// that when pool threads race at the same instant, one wins and the
/// rest return false.

class ProgressThrottle
{
public:
	explicit ProgressThrottle(qint64 minIntervalMs = 33)
	    : minIntervalMs_(minIntervalMs)
	{
		timer_.start();
	}

	/// Returns true at most once per `minIntervalMs` window across
	/// all callers. The caller is responsible for the actual emit;
	/// this just answers 'has enough time passed since the last
	/// positive response?'.
	bool shouldEmit()
	{
		const qint64 nowMs = timer_.elapsed();
		qint64 lastMs = lastEmitMs_.load(std::memory_order_relaxed);
		if ((nowMs - lastMs) < minIntervalMs_)
			return false;
		return lastEmitMs_.compare_exchange_strong(
		    lastMs, nowMs, std::memory_order_relaxed);
	}

private:
	QElapsedTimer timer_;
	std::atomic<qint64> lastEmitMs_{0};
	qint64 minIntervalMs_ = 33;
};