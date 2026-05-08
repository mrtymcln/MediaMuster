#pragma once

#include <atomic>

// xxHash3 verification of every copied file is on by default. The Debug
// menu exposes the toggle so a verified copy can be timed against an
// unverified one without a rebuild.
namespace MediaManagerVerify
{
    inline std::atomic<bool> &flag()
    {
        static std::atomic<bool> g{true};
        return g;
    }

    inline void setEnabled(bool on) { flag().store(on, std::memory_order_relaxed); }
    inline bool enabled() { return flag().load(std::memory_order_relaxed); }
}