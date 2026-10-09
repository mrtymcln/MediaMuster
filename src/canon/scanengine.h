#pragma once

// Reads databases first, then opens media headers only for unmatched files or
// missing/conflicting required table metadata. Unopened headers remain NotRead.
// The caller owns the worker thread. Complete original source graphs stay in
// lossless RAM archives; matching uses their immediately available receipts.

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
		std::function<void(const SourceCandidate &)> reading; ///< Source receipt, immediately before opening its reader.
	};
	class ScanEngine
	{
	public:
		ScanResult scan(const ScanRequest &request, const Cancellation &cancellation,
						const ScanCallbacks &callbacks = {}) const;
	};
	void selectMetadata(MediaEvidence &evidence);
}
