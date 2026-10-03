// Reads the complete MDB value graph through the shared OMF object interpreter.
// Media payload handling belongs to the separate legacy-media reader.

#include "mdbreader.h"
#include "omfobjects_p.h"

namespace Canon
{
	ParsedSource MdbReader::read(QIODevice &source, const ReaderContext &context) const
	{
		return Detail::interpretOmfObjects(Detail::readBento(source, context.cancellation), context, MetadataSource::Mdb);
	}
}
