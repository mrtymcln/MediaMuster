// The app's scan entry point delegates scheduling and matching to the coordinator.

#include "scanengine.h"

namespace MediaEngine
{
	ScanResult ScanEngine::scan(const ScanRequest &request, const Cancellation &cancellation,
							   const ScanCallbacks &callbacks) const
	{
		return ScanCoordinator{}.scan(request, cancellation, callbacks);
	}
}
