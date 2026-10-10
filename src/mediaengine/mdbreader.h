#pragma once

// Reads an MDB into source objects, recorded properties and local references.
// It preserves competing observations; matching files and choosing table values
// belong to later stages of the scan.

#include "sourcereader.h"

namespace MediaEngine
{
	class MdbReader final : public SourceReader
	{
	public:
		ParsedSource read(QIODevice &source, const ReaderContext &context) const override;
	};
}
