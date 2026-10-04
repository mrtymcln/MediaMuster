#pragma once

// Reads MXF metadata as source evidence. Repeated headers, private properties
// and original tag mappings survive independently; choosing a clip, descriptor
// or displayed value belongs to the later metadata engine.

#include "sourcereader.h"

namespace Canon
{
	class MxfReader final : public SourceReader
	{
	public:
		ParsedSource read(QIODevice &source, const ReaderContext &context) const override;
	};
}
