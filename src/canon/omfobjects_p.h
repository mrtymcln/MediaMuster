#pragma once

// The object interpretation shared by MDB databases and OMF media. The container
// reader decides which bytes to obtain; this layer preserves their meaning and
// uncertainty without choosing a physical file or a displayed value.

#include "bentoreader_p.h"
#include "sourcereader.h"

namespace Canon::Detail
{
	ParsedSource interpretOmfObjects(BentoReadResult bento, const ReaderContext &context, MetadataSource kind);
}
