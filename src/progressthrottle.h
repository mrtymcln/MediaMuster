#pragma once

#include <QElapsedTimer>
#include <atomic>

// Caps progress-signal emit frequency to roughly one per minIntervalMs.
// Used by long-running work that would otherwise flood the UI event loop
// with hundreds of progress updates per second: MediaManager's copy loop,
// MediaScanner's concurrent folder walk, MediaScanner's MXF header parse.
//
// shouldEmit() is safe to call from multiple threads concurrently. The
// atomic last-emit timestamp + CAS means that when several QtConcurrent
// pool threads race to publish progress at the same instant, exactly one
// of them gets the green light and the others return false — preventing
// near-duplicate emits without any explicit synchronisation at the call
// site.
class ProgressThrottle
{
public:
    explicit ProgressThrottle(qint64 minIntervalMs = 33)
        : minIntervalMs_(minIntervalMs)
    {
        timer_.start();
    }

    // Returns true at most once per minIntervalMs window. Caller is
    // responsible for the actual emit; this just decides "is enough time
    // since the last positive answer?".
    bool shouldEmit()
    {
        const qint64 nowMs = timer_.elapsed();
        qint64 lastMs = lastEmitMs_.load(std::memory_order_relaxed);
        if ((nowMs - lastMs) < minIntervalMs_)
            return false;
        return lastEmitMs_.compare_exchange_strong(
            lastMs, nowMs, std::memory_order_relaxed);
    }

private:
    QElapsedTimer timer_;
    std::atomic<qint64> lastEmitMs_{0};
    qint64 minIntervalMs_;
};
