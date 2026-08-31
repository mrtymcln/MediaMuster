#pragma once

#include <atomic>

/// Toggle for xxHash3 verification.

namespace OpVerify
{
	inline std::atomic<bool> &flag()
	{
		static std::atomic<bool> g{true};
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
} // namespace OpVerify