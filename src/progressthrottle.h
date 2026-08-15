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
		: m_minIntervalMs(minIntervalMs)
	{
		m_timer.start();
	}

	/// Returns true at most once per `minIntervalMs` window across
	/// all callers. The caller is responsible for the actual emit;
	/// this just answers 'has enough time passed since the last
	/// positive response?'.
	bool shouldEmit()
	{
		const qint64 nowMs = m_timer.elapsed();
		qint64 lastMs = m_lastEmitMs.load(std::memory_order_relaxed);
		if ((nowMs - lastMs) < m_minIntervalMs)
			return false;
		return m_lastEmitMs.compare_exchange_strong(
			lastMs, nowMs, std::memory_order_relaxed);
	}

private:
	QElapsedTimer m_timer;
	std::atomic<qint64> m_lastEmitMs{0};
	qint64 m_minIntervalMs;
};