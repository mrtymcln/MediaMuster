// Reads MDB values and relationships through its own container and object decoders.

#include "mdbreader.h"
#include "mdbobjects_p.h"

namespace MediaEngine
{
	ParsedSource MdbReader::read(QIODevice &source, const ReaderContext &context) const
	{
		return MdbDetail::interpretMdbObjects(MdbDetail::readBento(source, context.cancellation), context);
	}
}
