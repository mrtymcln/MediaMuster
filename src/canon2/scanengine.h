#pragma once

// Runs the shared Canon scan with database bytes retained by Canon2. Discovery,
// media readers, matching, scheduling and metadata selection stay in Canon.

#include "canon/scanengine.h"

namespace Canon2
{
	class ScanEngine
	{
	public:
		Canon::ScanResult scan(const Canon::ScanRequest &request, const Canon::Cancellation &cancellation,
							   const Canon::ScanCallbacks &callbacks = {}) const;
	};
}
