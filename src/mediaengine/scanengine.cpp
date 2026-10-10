// Supplies native database and MXF storage to MediaEngine's existing scan coordinator.

#include "scanengine.h"
#include "databasesource.h"
#include "mxfsource.h"

namespace MediaEngine
{
	namespace
	{
		class ImagePipeline final : public MediaEngine::SourcePipeline
		{
		public:
			MediaEngine::PreparedSource processDatabase(const MediaEngine::SourceCandidate &candidate, const QString &readReason,
												 const MediaEngine::Cancellation &cancellation) const override
			{
				return prepareDatabase(candidate, readReason, cancellation);
			}
			std::optional<MediaEngine::PreparedSource> processMxf(const MediaEngine::SourceCandidate &candidate,
															 const QString &readReason,
															 const MediaEngine::Cancellation &cancellation) const override
			{
				return prepareMxf(candidate, readReason, cancellation);
			}
		};
	}

	MediaEngine::ScanResult ScanEngine::scan(const MediaEngine::ScanRequest &request, const MediaEngine::Cancellation &cancellation,
									  const MediaEngine::ScanCallbacks &callbacks) const
	{
		const ImagePipeline pipeline;
		return MediaEngine::ScanCoordinator{}.scan(request, cancellation, callbacks, &pipeline);
	}
}
