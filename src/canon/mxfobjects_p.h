#pragma once

// Interpret retained MXF metadata using its recorded Primer mappings. This
// private stage names known properties and follows local references; it does
// not decide which package owns a file or which value the UI should show.

#include "scanmodel.h"

namespace Canon::Detail
{
	void interpretMxfObjects(ParsedSource &source, const Cancellation &cancellation);
}
