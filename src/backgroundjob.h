#pragma once

#include "logcategories.h"

#include <QDebug>
#include <QObject>
#include <QThread>
#include <atomic>
#include <utility>

/// Owns at most one live QThread. `start(fn)` cancels the previous
/// worker and joins it before starting the new one.
///
/// The callable polls `isCancelled()` and returns when set. A slow or
/// blocked OS I/O call can delay restart/shutdown: the owner stays alive
/// until the worker and any pool work it waits for have actually finished.
/// Workers must join their own child tasks before returning.
///
/// Lifecycle calls belong to the owner's thread; cancel() is thread-safe.
/// A completed QThread remains owned until the next start()/shutdown(),
/// avoiding queued cleanup callbacks that could outlive this value member.

class BackgroundJob
{
public:
	// Retain the context argument for the existing public API. Thread
	// ownership is now explicit and requires no callback into that context.
	explicit BackgroundJob(QObject *) {}

	// MARK: - Lifecycle

	~BackgroundJob() { shutdown(); }

	/// Cancel the current worker and block until it has fully exited.
	/// Idempotent; a blocked OS call can postpone completion.
	///
	/// Call this at the *top* of the owner's destructor when the worker
	/// touches other members of the owner: a plain value `BackgroundJob`
	/// is destroyed in reverse declaration order, so unless it's the last
	/// member declared, ~BackgroundJob would otherwise join only after the
	/// state the worker still reads has already been torn down. Doing the
	/// join in the destructor body — which runs before any member dies —
	/// sidesteps that ordering trap entirely.
	void shutdown()
	{
		cancel();
		joinAndDelete(kWorkerShutdownWarningMs);
	}

	BackgroundJob(const BackgroundJob &) = delete;
	BackgroundJob &operator=(const BackgroundJob &) = delete;
	BackgroundJob(BackgroundJob &&) = delete;
	BackgroundJob &operator=(BackgroundJob &&) = delete;

	// MARK: - Job control

	/// Cancel any previous worker, then run `fn` on a new thread.
	/// Cancellation resets only after the previous worker has joined,
	/// so no old callback can accidentally observe the new run's flag.
	template <typename Fn>
	void start(Fn &&fn)
	{
		cancel();
		joinAndDelete(kWorkerCancelWarningMs);
		m_cancel.store(false, std::memory_order_release);
		m_thread = QThread::create(std::forward<Fn>(fn));
		m_thread->start();
	}

	/// Signal the worker to stop. Cooperative; the worker decides
	/// when to notice (it polls `isCancelled()` from inside its loop).
	/// `release` on store pairs with `acquire` on the worker's load to
	/// guarantee the flag becomes visible promptly on ARM as well as x86.
	void cancel() noexcept { m_cancel.store(true, std::memory_order_release); }

	// MARK: - State

	bool isCancelled() const noexcept { return m_cancel.load(std::memory_order_acquire); }

	/// The raw flag, for handing to code that polls cancellation without
	/// holding a BackgroundJob (the ops engine's runner and copier take a
	/// `const std::atomic<bool> &` so tests can drive them with a plain
	/// flag). Read-only; start() still owns the reset.
	const std::atomic<bool> &cancelFlag() const noexcept { return m_cancel; }

private:
	// MARK: - Slow-join diagnostics

	/// Warn after these intervals, then continue waiting. Force-killing
	/// the orchestrator could strand pool callbacks holding its owner's data.
	static constexpr int kWorkerShutdownWarningMs = 10000;

	static constexpr int kWorkerCancelWarningMs = 5000;

	// MARK: - Join helper

	/// Even isRunning()==false/finished() does not replace wait(): native
	/// thread-local cleanup can still be in flight. No event-loop cleanup or
	/// queued lambda may clear our handle before this join has completed.
	void joinAndDelete(int warningMs)
	{
		if (!m_thread)
			return;
		m_thread->quit();
		if (!m_thread->wait(warningMs))
		{
			qCWarning(lcWorker, "worker still running after %d ms; waiting for cooperative shutdown.", warningMs);
			// A lifecycle call from the worker itself is a programming error;
			// never continue into owner destruction without a successful join.
			if (!m_thread->wait())
				qFatal("BackgroundJob cannot join its own worker thread");
		}
		delete m_thread;
		m_thread = nullptr;
	}

	QThread *m_thread = nullptr;
	std::atomic<bool> m_cancel{false};
};
