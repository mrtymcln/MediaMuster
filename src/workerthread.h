#pragma once

#include "logging.h"

#include <QDebug>
#include <QThread>

/// Shared helper for MediaScanner / MediaManager / Rebalancer worker.

namespace WorkerThread
{
	// MARK: - Timeouts

	/// Grace window before terminating a worker on shutdown.
	/// 10 s lets a stuttering Nexis/SMB share finish a mid-read
	/// folder without making Cmd-Q feel hung.
	inline constexpr int kWorkerShutdownTimeoutMs = 10000;

	/// Cancel grace when restarting a worker. Shorter than shutdown
	/// because the user is waiting on the new run; 5 s covers any
	/// genuine mid-read syscall.
	inline constexpr int kWorkerCancelTimeoutMs = 5000;

	// MARK: - Join helper

	/// Wait for the thread to finish, falling back to `terminate()` after
	/// `graceMs`. Caller signals cancel first (usually via a
	/// `std::atomic<bool>` the worker polls), and this just bounds how
	/// long we wait for it to notice.
	///
	/// `quit()` is a no-op for `QThread::create` workers but required
	/// for `moveToThread` ones; called unconditionally to cover both.
	///
	/// Returns true on clean shutdown, false on terminate.
	inline bool joinOrTerminate(QThread *t, int graceMs)
	{
		if (!t)
			return true;
		t->quit();
		if (t->wait(graceMs))
			return true;
		qCWarning(lcWorker, "did not quit within %d ms; terminating.", graceMs);
		t->terminate();
		t->wait(1000);
		return false;
	}
} // namespace WorkerThread