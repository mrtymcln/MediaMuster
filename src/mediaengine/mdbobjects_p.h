#pragma once

// MDB-only interpretation of the database's recorded dictionaries, objects and
// references. The OMFI wire names are part of the database format; they do not
// create a dependency on MediaMuster's OMF media reader.

#include "mdbbentoreader_p.h"
#include "sourcereader.h"

namespace MediaEngine::MdbDetail
{
	ParsedSource interpretMdbObjects(BentoReadResult bento, const ReaderContext &context);
}
