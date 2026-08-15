#pragma once

#include "workerthread.h"

#include <QObject>
#include <QThread>
#include <atomic>
#include <utility>

/// Owns at most one live QThread. `start(fn)` cancels the previous
/// worker and joins it before starting the new one.
///
/// Owner passes its QObject (connection context) and a callable
/// that polls `isCancelled()` and returns when set. If the worker
/// doesn't exit within the grace window, `terminate()` is the last
/// resort.
///
/// Not a QObject. As a value member, `~BackgroundJob` runs before
/// the owner's `~QObject`, so we disconnect the `finished` lambda
/// explicitly; otherwise it could fire after the owner is gone.

class BackgroundJob
{
public:
	explicit BackgroundJob(QObject *owner)
		: m_owner(owner)
	{
	}

	// MARK: - Lifecycle

	~BackgroundJob() { shutdown(); }

	/// Cancel the current worker and block until it has exited (or been
	/// terminated). Idempotent.
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
		if (!m_thread)
			return;
		// Kill the identity check lambda's connection to m_owner before
		// joining. Otherwise a finished event already queued in the
		// event loop can fire after we return and dereference our
		// members on a dead object. The thread's own deleteLater
		// connection is independent and still cleans up the QThread.
		QObject::disconnect(m_thread, &QThread::finished, m_owner, nullptr);
		WorkerThread::joinOrTerminate(m_thread, WorkerThread::kWorkerShutdownTimeoutMs);
		m_thread = nullptr;
	}

	BackgroundJob(const BackgroundJob &) = delete;
	BackgroundJob &operator=(const BackgroundJob &) = delete;
	BackgroundJob(BackgroundJob &&) = delete;
	BackgroundJob &operator=(BackgroundJob &&) = delete;

	// MARK: - Job control

	/// Cancel any previous worker, then run `fn` on a new thread.
	/// `kWorkerCancelTimeoutMs` grace before terminating an old
	/// worker. Cancel flag resets to false for the new worker once
	/// the old one's joined.
	template <typename Fn>
	void start(Fn &&fn)
	{
		if (m_thread && m_thread->isRunning())
		{
			cancel();
			WorkerThread::joinOrTerminate(m_thread, WorkerThread::kWorkerCancelTimeoutMs);
		}
		m_cancel.store(false, std::memory_order_release);
		auto *thread = QThread::create(std::forward<Fn>(fn));
		m_thread = thread;
		QObject::connect(thread, &QThread::finished, thread, &QThread::deleteLater);
		// Identity check: two rapid start() calls would otherwise let
		// a stale finished-lambda null the live thread.
		QObject::connect(thread, &QThread::finished, m_owner,
						 [this, thread]
						 {
							 if (m_thread == thread)
								 m_thread = nullptr;
						 });
		thread->start();
	}

	/// Signal the worker to stop. Cooperative; the worker decides
	/// when to notice (it polls `isCancelled()` from inside its loop).
	/// `release` on store pairs with `acquire` on the worker's load to
	/// guarantee the flag becomes visible promptly on ARM as well as x86.
	void cancel() noexcept { m_cancel.store(true, std::memory_order_release); }

	// MARK: - State

	bool isCancelled() const noexcept { return m_cancel.load(std::memory_order_acquire); }

private:
	QObject *m_owner;
	QThread *m_thread = nullptr;
	std::atomic<bool> m_cancel{false};
};