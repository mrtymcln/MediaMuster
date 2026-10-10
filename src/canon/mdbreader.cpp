// Reads MDB database values and relationships using its own container and
// object decoders. Removing OMF media support cannot remove this implementation.

#include "mdbreader.h"
#include "mdbobjects_p.h"

namespace Canon
{
	ParsedSource MdbReader::read(QIODevice &source, const ReaderContext &context) const
	{
		return MdbDetail::interpretMdbObjects(MdbDetail::readBento(source, context.cancellation), context);
	}
}
