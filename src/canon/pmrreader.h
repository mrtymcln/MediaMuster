#pragma once

// Reads the records in a PMR database, keeping their original evidence.
// Later stages associate those records with physical files and choose display values.

#include "sourcereader.h"

namespace Canon
{
	/// Borrows a finite, seekable binary device and returns a ParsedSource.
	class PmrReader final : public SourceReader
	{
	public:
		ParsedSource read(QIODevice &source, const ReaderContext &context) const override;
	};
}
