#pragma once

#include <QElapsedTimer>
#include <atomic>

/// Limits progress updates across threads using one atomic timestamp.

class ProgressThrottle
{
public:
	explicit ProgressThrottle(qint64 minIntervalMs = 33)
		: m_minIntervalMs(minIntervalMs)
	{
		m_timer.start();
	}

	/// At most one caller succeeds per interval; the caller emits the update.
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
