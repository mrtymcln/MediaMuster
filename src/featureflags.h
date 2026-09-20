#pragma once

namespace FeatureFlags
{
	// Set false and rebuild for a public release; true includes the Debug menu.
	// The features behind its toggles still start disabled on every launch.
	inline constexpr bool kDebugMenuEnabled = true;
}
