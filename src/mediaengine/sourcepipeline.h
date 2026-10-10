#pragma once

// Alternative source storage supplies the same facts to the shared scanner.
// Discovery, database-first scheduling, matching and display rules stay together.

#include "projection.h"

namespace MediaEngine
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
		// An absent alternative keeps the scanner's established MXF reader/archive
		// path. This hook changes storage after the scheduler has chosen a header.
		virtual std::optional<PreparedSource> processMxf(const SourceCandidate &,
														 const QString &, const Cancellation &) const
		{
			return std::nullopt;
		}
	};
}
