#pragma once

// Keeps supported metadata and receipts from databases and media headers.
// The coordinator owns discovery, scheduling, matching and metadata selection.

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
