#pragma once

namespace FeatureFlags
{
	inline constexpr bool kDebugMenuEnabled = true;
	inline constexpr bool OmfScan = true;
	inline constexpr bool kOmfEnabled = OmfScan; // Compatibility for existing callers.
	inline constexpr bool kPrecomputesEnabled = true;
	inline constexpr bool kClipDurationEnabled = true;
	inline constexpr bool kUndoEnabled = true;
}
