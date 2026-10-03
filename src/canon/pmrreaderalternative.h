#pragma once

#include "sourcereader.h"

namespace Canon
{
	/// Independent PMR implementation for comparison with PmrReader.
	/// Borrows a finite, seekable binary device; returns the same evidence model.
	/// No source matching, display selection or legacy parser dependencies.
	class PmrReaderAlternative final : public SourceReader
	{
	public:
		ParsedSource read(QIODevice &source, const ReaderContext &context) const override;
	};
}
