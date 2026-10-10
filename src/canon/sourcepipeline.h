#pragma once

// An alternative database reader supplies the same facts to the shared scanner.
// Discovery, database-first scheduling, matching and display rules stay together.

#include "projection.h"

namespace Canon
{
	struct PreparedSource
	{
		Projection projection;
		StoredSource source;
	};

	class SourcePipeline
	{
	public:
		SourcePipeline() = default;
		virtual ~SourcePipeline() = default;
		SourcePipeline(const SourcePipeline &) = delete;
		SourcePipeline &operator=(const SourcePipeline &) = delete;
		SourcePipeline(SourcePipeline &&) = delete;
		SourcePipeline &operator=(SourcePipeline &&) = delete;
		virtual PreparedSource processDatabase(const SourceCandidate &candidate,
											  const QString &readReason, const Cancellation &cancellation) const = 0;
	};
}
