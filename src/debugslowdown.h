#pragma once

#include <atomic>
#include <QThread>

// Throttles scan/copy/move/delete loops so progress is visible when ops finish in ms.
namespace DebugSlowdown
{
    inline std::atomic<bool> &flag()
    {
        static std::atomic<bool> g{false};
        return g;
    }

    inline void setEnabled(bool on) { flag().store(on, std::memory_order_relaxed); }
    inline bool enabled() { return flag().load(std::memory_order_relaxed); }

    // 50× multiplier when enabled; otherwise a single branch.
    inline void pauseForMs(int baseMs)
    {
        if (!enabled())
            return;
        QThread::msleep(static_cast<unsigned long>(baseMs * 50));
    }
}