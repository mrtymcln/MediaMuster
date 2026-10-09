#pragma once

namespace FeatureFlags
{
	inline constexpr bool kDebugMenu = true;
	inline constexpr bool kMonospaceTable = false; /// always in Debug menu regardless
	inline constexpr bool kOmfScan = true;
	inline constexpr bool kPrecomputeFilter = true;
	inline constexpr bool kSequenceFilter = false; /// for the future sequence selector
	inline constexpr bool kClipDuration = true;
	inline constexpr bool kUndo = true;
}