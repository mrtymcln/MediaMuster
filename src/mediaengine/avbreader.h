#pragma once

// Reads an Avid bin into a temporary source graph for Whole Bin resolution.

#include "sourcereader.h"

namespace MediaEngine
{
	class AvbReader final : public SourceReader
	{
	public:
		ParsedSource read(QIODevice &source, const ReaderContext &context) const override;
	};
}
