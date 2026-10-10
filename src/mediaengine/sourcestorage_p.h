#pragma once

// Release growth space once a reader has finished, before its result is shared
// or indexed. Values, ordering and references stay intact. Leave shared text,
// byte buffers and native contexts alone so compaction does not duplicate them.

#include "scanmodel.h"

namespace MediaEngine::Detail
{
	inline void squeezeSourceStorage(ParsedSource &source, const Cancellation &cancellation)
	{
		if (cancellation.cancelled())
			return;
		for (auto &object : source.objects)
		{
			if (cancellation.cancelled())
				return; // Compaction is optional; keep all obtained evidence on cancel.
			object.properties.squeeze();
		}
		for (auto &set : source.recordSets)
			set.objects.squeeze();
		for (auto &embedded : source.embeddedSources)
			squeezeSourceStorage(embedded, cancellation);
		if (cancellation.cancelled())
			return;
		source.unownedProperties.squeeze();
		source.objects.squeeze();
		source.relationships.squeeze();
		source.recordSets.squeeze();
		source.embeddedSources.squeeze();
		source.diagnostics.squeeze();
	}
}
