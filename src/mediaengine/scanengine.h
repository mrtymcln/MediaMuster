#pragma once

// Selects native source storage for the scan. The coordinator owns discovery,
// database-first scheduling, matching and metadata selection.

#include "mediaengine/scancoordinator.h"

namespace MediaEngine
{
	class ScanEngine
	{
	public:
		MediaEngine::ScanResult scan(const MediaEngine::ScanRequest &request, const MediaEngine::Cancellation &cancellation,
							   const MediaEngine::ScanCallbacks &callbacks = {}) const;
	};
}
