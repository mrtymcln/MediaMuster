#pragma once

#include "workerthread.h"

#include <QObject>
#include <QThread>
#include <atomic>
#include <utility>

// Single-worker background-thread manager. Each instance owns at most one
// running QThread; start(fn) cancels and joins any previous worker before
// spawning the new one, so two workers can never be in flight on the same
// BackgroundJob.
//
// The owner passes its own QObject pointer for context (so Qt can break
// queued-connection lambdas when the owner is destroyed) and a callable
// that runs on the new QThread. The callable should periodically poll
// isCancelled() and unwind cooperatively when it returns true.
//
// Not a QObject itself — it lives as a value member on the owner and
// relies on the owner's lifetime for connection context.

class BackgroundJob
{
public:
    explicit BackgroundJob(QObject *owner) : owner_(owner) {}

    ~BackgroundJob()
    {
        cancel();
        // Drop the identity-check lambda's link to owner_ before we join,
        // so a finished event firing during destruction can't dereference
        // this BackgroundJob after we return. The thread's own
        // deleteLater connection (thread → thread) is independent and
        // still cleans up the QThread itself.
        if (m_thread)
            QObject::disconnect(m_thread, &QThread::finished, owner_, nullptr);
        WorkerThread::joinOrTerminate(m_thread, 30000);
    }

    BackgroundJob(const BackgroundJob &) = delete;
    BackgroundJob &operator=(const BackgroundJob &) = delete;
    BackgroundJob(BackgroundJob &&) = delete;
    BackgroundJob &operator=(BackgroundJob &&) = delete;

    // Cancel any previous worker, then start a new one running fn. 5 s
    // grace before terminating the old worker — matches the previous
    // inline behaviour. The cancel flag is reset to false for the new
    // worker after the old one is joined.
    template <typename Fn>
    void start(Fn &&fn)
    {
        if (m_thread && m_thread->isRunning())
        {
            cancel();
            WorkerThread::joinOrTerminate(m_thread, 5000);
        }
        m_cancel.store(false, std::memory_order_relaxed);
        auto *thread = QThread::create(std::forward<Fn>(fn));
        m_thread = thread;
        QObject::connect(thread, &QThread::finished, thread,
                         &QThread::deleteLater);
        // Identity check guards against a stale finished-lambda nulling
        // out m_thread after a new job has already replaced it.
        QObject::connect(thread, &QThread::finished, owner_,
                         [this, thread]
                         { if (m_thread == thread) m_thread = nullptr; });
        thread->start();
    }

    void cancel() noexcept
    {
        m_cancel.store(true, std::memory_order_relaxed);
    }

    bool isCancelled() const noexcept
    {
        return m_cancel.load(std::memory_order_relaxed);
    }

    bool isRunning() const
    {
        return m_thread && m_thread->isRunning();
    }

private:
    QObject *owner_;
    QThread *m_thread = nullptr;
    std::atomic<bool> m_cancel{false};
};