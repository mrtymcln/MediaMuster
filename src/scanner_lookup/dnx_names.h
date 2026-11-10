#pragma once

#include "compression_names.h"

namespace Bella::Lookup
{
	// Leave unknown labels blank and require exact facts for numbered DNx aliases.
	[[nodiscard]] CompressionNames dnxNames(const CompressionFacts &facts);

	// Legacy readers can recover a missing label from these known ResolutionIDs.
	[[nodiscard]] QByteArray codingLabelForDnxResolution(qint64 resolution);

	// Call only after the recorded or recovered label matches the ResolutionID.
	[[nodiscard]] MediaRate dnxRateForResolution(qint64 resolution, MediaRate recorded);
}
