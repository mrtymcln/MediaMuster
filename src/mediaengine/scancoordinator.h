#pragma once

// Reads databases first, then opens media headers only for unmatched files or
// missing/conflicting required table metadata. Unopened headers remain NotRead.
// The caller owns the worker thread. Source records stay in the chosen RAM
// storage; matching uses their immediately available receipts and file facts.

#include "scanmodel.h"
#include <functional>

namespace MediaEngine
{
	class SourcePipeline;
	struct ScanCallbacks
	{
		std::function<void(int, int, const QString &)> progress;
		std::function<void()> finalising;
		std::function<void(const QString &)> warning;
		std::function<void(const QString &)> discovering;	  ///< Folder access, before source totals are known.
		std::function<void(const SourceCandidate &)> reading; ///< Source receipt, immediately before opening its reader.
	};
	class ScanCoordinator
	{
	public:
		ScanResult scan(const ScanRequest &request, const Cancellation &cancellation,
						const ScanCallbacks &callbacks = {}, const SourcePipeline *pipeline = nullptr) const;
		// pipeline is borrowed for this call and only processes PMR/MDB candidates.
	};
	void selectMetadata(MediaEvidence &evidence);
}
