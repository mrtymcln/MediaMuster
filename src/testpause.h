#pragma once

#include <QThread>

#include <atomic>

// Test-only pause at scan-folder boundaries. Disabled by default; the scanner
// cancellation tests enable it to observe an in-progress scan deterministically.

namespace TestPause
{
	// MARK: - Arm (tests only)

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

	/// Sleep `ms` when the seam is armed; a no-op otherwise. The caller names
	/// the real duration, so a reader can see the wait without arithmetic.
	inline void sleepMs(unsigned long ms)
	{
		if (!enabled())
			return;
		QThread::msleep(ms);
	}

	inline constexpr unsigned long kPerScannedFolderMs = 4000;
} // namespace TestPause
