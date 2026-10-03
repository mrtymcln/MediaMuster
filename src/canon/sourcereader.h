#pragma once
#include "scanmodel.h"
#include <QIODevice>

namespace Canon
{
	struct ReaderContext
	{
		SourceSnapshotRef snapshot;
		const Cancellation &cancellation;
	};

	/// New decoders consume an already-open source and return observations/graphs.
	/// Ownership, matching, freshness and UI selection are coordinator decisions.
	/// No legacy MediaMetadata aggregate or selected MediaFile is an input/output.
	class SourceReader
	{
	public:
		SourceReader(const SourceReader &) = delete;
		SourceReader &operator=(const SourceReader &) = delete;
		virtual ~SourceReader() = default;
		virtual ParsedSource read(QIODevice &source, const ReaderContext &context) const = 0;
	protected:
		SourceReader() = default;
	};
}
