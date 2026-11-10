#pragma once

// Reads an Avid bin into a temporary source graph for Whole Bin resolution.

#include "source_reader.h"

namespace MediaEngine
{
	// MARK: - AVB reader
	class AvbReader final : public SourceReader
	{
	public:
		ParsedSource read(QIODevice &source, const ReaderContext &context) const override;
	};
}
