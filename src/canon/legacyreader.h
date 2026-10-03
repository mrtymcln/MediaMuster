#pragma once

// Reads media admitted by OmfScan: OMF containers and native WAV/AIFF files,
// including their embedded OMF metadata. Each source keeps its own observations;
// the reader does not merge media rows or select the table's metadata.

#include "sourcereader.h"

namespace Canon
{
	class LegacyReader final : public SourceReader
	{
	public:
		ParsedSource read(QIODevice &source, const ReaderContext &context) const override;
	};
}
