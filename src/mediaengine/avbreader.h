#pragma once

// Reads an explicitly loaded Avid bin into MediaEngine's source graph. Bin entries,
// sequence links and original property bytes survive independently of filtering.

#include "sourcereader.h"

namespace MediaEngine
{
	class AvbReader final : public SourceReader
	{
	public:
		ParsedSource read(QIODevice &source, const ReaderContext &context) const override;
	};
}
