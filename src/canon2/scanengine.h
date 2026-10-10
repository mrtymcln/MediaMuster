#pragma once

// Runs the shared scan with database and acquired MXF bytes retained by Canon2.
// Discovery, format interpretation, scheduling and selection stay in Canon.

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
