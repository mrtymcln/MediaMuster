#pragma once

// Lets a scan restore its collected source records from alternative RAM storage.
// The stored source owns the implementation; restoration never chooses UI values.

#include "scanmodel.h"

namespace MediaEngine
{
	class SourceStore
	{
	public:
		SourceStore() = default;
		virtual ~SourceStore() = default;
		SourceStore(const SourceStore &) = delete;
		SourceStore &operator=(const SourceStore &) = delete;
		SourceStore(SourceStore &&) = delete;
		SourceStore &operator=(SourceStore &&) = delete;
		virtual std::optional<ParsedSource> restore(const Cancellation &cancellation) const = 0;
	};
}
