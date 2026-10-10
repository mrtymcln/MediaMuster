#pragma once

// Keeps original database images and the supported metadata from media headers.
// The coordinator owns discovery, scheduling, matching and metadata selection.

#include "mediaengine/scancoordinator.h"

namespace MediaEngine
{
	class ScanEngine
	{
	public:
		ScanEngine() = default;
		explicit ScanEngine(SourceRetention mediaRetention) : m_mediaRetention(mediaRetention) {}
		MediaEngine::ScanResult scan(const MediaEngine::ScanRequest &request, const MediaEngine::Cancellation &cancellation,
									 const MediaEngine::ScanCallbacks &callbacks = {}) const;

	private:
		SourceRetention m_mediaRetention = SourceRetention::MetadataOnly;
	};
}
