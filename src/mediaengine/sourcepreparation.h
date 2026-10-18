#pragma once

// Projected facts and a source receipt outlive temporary reader records.

#include "projection.h"

namespace MediaEngine
{
	struct PreparedSource
	{
		Projection projection;
		SourceReceipt source;
	};
}
