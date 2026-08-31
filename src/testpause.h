#pragma once

#include <QThread>

#include <atomic>

// MARK: - TestPause
//
// A TEST SEAM, not a user feature. Off in every shipped build, reachable
// only from the test suite — no menu item, no setting, nothing a user can
// switch on.
//
// It exists because the engine's most important promises can only be
// checked WHILE a copy is running: that the plan reaches disk before the
// first byte moves, that a mid-copy failure puts the original back, that
// Cancel restores rather than abandons, that a source swapped underneath
// us is caught. An 8 MB copy finishes in milliseconds, leaving a test
// nothing to interrupt; these pauses hold the door open long enough to
// act.
//
// Nothing is slowed proportionally. The real work takes exactly as long
// as it always does and a fixed wait is added at three points, each named
// in real milliseconds at its call site.
//
// The waits are load-bearing — seven tests are timed to act inside these
// windows — so change one only with its tests in view.
//
// (Was DebugSlowdown, surfaced as Debug ▸ Slow mode with a ×50 multiplier
// over hand-picked numbers. The menu item and the multiplier went on
// 2026-08-31; the seam stayed, because the tests need it.)

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

	/// The three waits in use, named so the call sites read as intent and
	/// so the relationship between them is visible in one place: a chunk
	/// pause has to be short enough that a file still completes, an item
	/// pause long enough to click Cancel, a folder pause long enough to
	/// watch a scan advance folder by folder.
	inline constexpr unsigned long kPerCopyChunkMs = 250;  ///< per 4 MB written
	inline constexpr unsigned long kPerItemMs = 2000;	   ///< per file in a run
	inline constexpr unsigned long kPerScannedFolderMs = 4000; ///< per media folder
} // namespace TestPause
