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
} // namespace DebugSlowdown