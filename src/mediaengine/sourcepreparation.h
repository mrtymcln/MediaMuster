#pragma once

// Reading materials live only until their supported facts have been projected.
// The scanner receives those facts and a small receipt of what was read.

#include "projection.h"

namespace MediaEngine
{
	struct PreparedSource
	{
		Projection projection;
		SourceReceipt source;
	};
}
