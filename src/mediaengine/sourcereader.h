#pragma once

// Readers inspect already-open sources. The coordinator handles file access,
// matching between sources and display selection.

#include "scanmodel.h"
#include <QIODevice>

namespace MediaEngine
{
	struct ReaderContext
	{
		SourceSnapshotRef snapshot;
		const Cancellation &cancellation;
	};

	class SourceReader
	{
	public:
		SourceReader(const SourceReader &) = delete;
		SourceReader &operator=(const SourceReader &) = delete;
		virtual ~SourceReader() = default;
		// Caller keeps the device and cancellation alive. Readers may seek/change
		// position but do not close the device. Expected input failures return an
		// outcome; allocation failures may propagate. Use the returned snapshot
		// when building object references: the reader may replace the input receipt.
		virtual ParsedSource read(QIODevice &source, const ReaderContext &context) const = 0;

	protected:
		SourceReader() = default;
	};
}
