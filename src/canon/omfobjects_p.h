#pragma once

// Object interpretation owned by the OMF media reader. The container
// reader decides which bytes to obtain; this layer preserves their meaning and
// uncertainty without choosing a physical file or a displayed value.

#include "omfbentoreader_p.h"
#include "sourcereader.h"

namespace Canon::Detail
{
	ParsedSource interpretOmfObjects(BentoReadResult bento, const ReaderContext &context);
}
