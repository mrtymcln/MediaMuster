#pragma once

// Finds media files and databases (if present) in the supported folders.
// Returns a file inventory and a list of sources for the readers to open next.

#include "scanmodel.h"

namespace Canon
{
	/// Runs synchronously on the caller's worker; it owns no UI or global state.
	class DiscoveryEngine
	{
	public:
		ScanResult discover(const ScanRequest &request, const Cancellation &cancellation) const;
	};
}
