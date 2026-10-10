// Supplies native database and MXF storage to Canon's existing scan coordinator.

#include "scanengine.h"
#include "databasesource.h"
#include "mxfsource.h"

namespace Canon2
{
	namespace
	{
		class ImagePipeline final : public Canon::SourcePipeline
		{
		public:
			Canon::PreparedSource processDatabase(const Canon::SourceCandidate &candidate, const QString &readReason,
												 const Canon::Cancellation &cancellation) const override
			{
				return prepareDatabase(candidate, readReason, cancellation);
			}
			std::optional<Canon::PreparedSource> processMxf(const Canon::SourceCandidate &candidate,
															 const QString &readReason,
															 const Canon::Cancellation &cancellation) const override
			{
				return prepareMxf(candidate, readReason, cancellation);
			}
		};
	}

	Canon::ScanResult ScanEngine::scan(const Canon::ScanRequest &request, const Canon::Cancellation &cancellation,
									  const Canon::ScanCallbacks &callbacks) const
	{
		const ImagePipeline pipeline;
		return Canon::ScanEngine{}.scan(request, cancellation, callbacks, &pipeline);
	}
}
