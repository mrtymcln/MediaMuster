#pragma once
#include "scanmodel.h"

namespace Canon
{
	/// Fresh discovery stage: inventory and parser worklist, no metadata inference.
	/// Runs synchronously on its caller's worker; it owns no UI or global state.
	class DiscoveryEngine
	{
	public:
		ScanResult discover(const ScanRequest &request, const Cancellation &cancellation) const;
	};
}
