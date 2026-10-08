#pragma once

// Reads databases first, then opens media headers only for unmatched files or
// missing/conflicting required table metadata. Unopened headers remain NotRead.
// The caller owns the worker thread and keeps the returned source graphs alive.

#include "scanmodel.h"
#include <functional>

namespace Canon
{
	struct ScanCallbacks
	{
		std::function<void(int, int, const QString &)> progress;
		std::function<void()> finalising;
		std::function<void(const QString &)> warning;
		std::function<void(const QString &)> discovering; ///< Folder access, before source totals are known.
	};
	class ScanEngine
	{
	public:
		ScanResult scan(const ScanRequest &request, const Cancellation &cancellation,
						const ScanCallbacks &callbacks = {}) const;
	};
	void selectMetadata(MediaEvidence &evidence);
}
