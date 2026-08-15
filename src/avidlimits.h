#pragma once

// MARK: - AvidLimits
/// Limits for each 'Avid MediaFiles' folder are defined here.

namespace AvidLimits
{
	// A folder should never reach this.
	inline constexpr int kFolderMax = 5000;

	// What the Rebalancer packs to.
	inline constexpr int kFolderRecommend = kFolderMax - 1; // 4999

	// What triggers a red warning.
	inline constexpr int kFolderCritical = 4800;

	// What triggers an amber warning.
	inline constexpr int kFolderWarn = 4500;
} // namespace AvidLimits