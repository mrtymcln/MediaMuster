#pragma once
#include "sourcereader.h"

namespace Canon
{
	/// PMR framing verified against MC 26.8 and retained fixture bytes.
	/// No identity normalisation, codepage guessing, record filtering or selection.
	class PmrReader final : public SourceReader
	{
	public:
		ParsedSource read(QIODevice &source, const ReaderContext &context) const override;
	};
}
