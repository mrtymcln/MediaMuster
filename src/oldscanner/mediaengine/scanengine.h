#pragma once

// Returns working metadata and source receipts; retains no scan state.
// The coordinator handles discovery, scheduling, matching and selection.

#include "oldscanner/mediaengine/scancoordinator.h"

namespace MediaEngine
{
	class ScanEngine
	{
	public:
		MediaEngine::ScanResult scan(const MediaEngine::ScanRequest &request, const MediaEngine::Cancellation &cancellation,
									 const MediaEngine::ScanCallbacks &callbacks = {}) const;
	};
}
