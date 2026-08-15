#pragma once

#include <atomic>
#include <QThread>

/// Artificial slow-down for testing behaviours that
/// only show up under heavy load. Off by default.

namespace DebugSlowdown
{
	// MARK: - Toggle
	inline std::atomic<bool> &flag()
	{
		static std::atomic<bool> g{false};
		return g;
	}
	inline void setEnabled(bool on)
	{
		flag().store(on, std::memory_order_relaxed);
	}
	inline bool enabled()
	{
		return flag().load(std::memory_order_relaxed);
	}

	// MARK: - Insertion point
	inline void pauseForMs(int baseMs)
	{
		if (!enabled())
			return;
		QThread::msleep(static_cast<unsigned long>(baseMs * 50));
	}

	// MARK: - Copy-loop tick

	/// Ticks once per copy-loop iteration (see copyFileWithProgress), so a
	/// test can wait for hard evidence that the byte loop is underway —
	/// source size captured, first buffer written — instead of guessing
	/// with a wall-clock wait. The guess lost on Windows CI: the worker
	/// hadn't reached the loop when the wait expired, so a mid-copy
	/// mutation landed before the size capture it was meant to trip.
	inline std::atomic<quint64> &copyLoopTicks()
	{
		static std::atomic<quint64> g{0};
		return g;
	}
} // namespace DebugSlowdown