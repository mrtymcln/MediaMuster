#pragma once

// Finds media files and databases (if present) in the supported folders.
// Returns a file inventory and a list of sources for the readers to open next.

#include "scanmodel.h"
#include <functional>

namespace MediaEngine
{
	/// Runs synchronously on the caller's worker; it owns no UI or global state.
	class DiscoveryEngine
	{
	public:
		// Folder discovery has no known total. Report the path before accessing it.
		ScanResult discover(const ScanRequest &request, const Cancellation &cancellation,
							const std::function<void(const QString &)> &discovering = {}) const;
	};
}
