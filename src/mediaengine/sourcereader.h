#pragma once

// Retained media readers and the active AVB reader inspect already-open sources.
// Callers own file access; matching, operation checks and display selection live
// outside these readers. Bella's independent readers do not use this interface.

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
