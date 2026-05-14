#pragma once

#include <QDebug>
#include <QThread>

// Shared shutdown helper for our background worker threads. The three classes
// that own a QThread (MediaScanner, MediaManager, Rebalancer) all used to
// inline the same `quit / wait(5000) / terminate / wait(1000)` dance —
// terminate() on a thread mid-QFile-I/O is technically UB (RAII destructors
// don't run; mutexes leak), so the grace period before that fallback wants
// to be generous in destructor contexts and stay short in cancel-and-restart
// contexts. Each callsite passes its own graceMs accordingly.

namespace WorkerThread
{
    // Wait for the thread to finish cooperatively, falling back to terminate()
    // only after graceMs. Returns true on clean shutdown, false on terminate.
    // The caller signals cooperative cancel first (e.g. a std::atomic<bool>
    // flag the worker polls); this helper just bounds how long we wait for
    // the worker to notice and unwind.
    //
    // quit() is a no-op for threads created via QThread::create (no event
    // loop running), and necessary for moveToThread-style workers — call it
    // unconditionally so the helper works for both patterns.
    inline bool joinOrTerminate(QThread *t, int graceMs)
    {
        if (!t)
            return true;
        t->quit();
        if (t->wait(graceMs))
            return true;
        qWarning("WorkerThread: did not quit within %d ms; terminating.", graceMs);
        t->terminate();
        t->wait(1000);
        return false;
    }
}