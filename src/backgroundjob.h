#pragma once

#include "logcategories.h"

#include <QDebug>
#include <QThread>
#include <atomic>
#include <utility>

/// Owns one worker thread. Start/shutdown belong to the owner's thread;
/// cancel is thread-safe and cooperative. Workers must finish their own child
/// tasks before returning. A blocked OS call can delay restart or shutdown.
class BackgroundJob
{
public:
	BackgroundJob() = default;

	// MARK: - Lifecycle

	~BackgroundJob() { shutdown(); }

	/// Cancel and join before destroying state the worker accesses. Owners
	/// whose member order does not guarantee this must call shutdown at the
	/// start of their destructor. Repeated calls are safe.
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

	/// Request a stop. The worker polls the flag between units of work;
	/// this cannot interrupt an OS call already in progress.
	void cancel() noexcept { m_cancel.store(true, std::memory_order_release); }

	// MARK: - State

	bool isCancelled() const noexcept { return m_cancel.load(std::memory_order_acquire); }

	/// Read-only flag for operations that do not hold the BackgroundJob.
	/// Only start() resets it, after the previous worker has joined.
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
