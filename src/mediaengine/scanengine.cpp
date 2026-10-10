// Supplies original database images and the chosen media-header retention to the coordinator.

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
			explicit ImagePipeline(SourceRetention mediaRetention) : m_mediaRetention(mediaRetention) {}
			SourceRetention mediaRetention() const override { return m_mediaRetention; }
			MediaEngine::PreparedSource processDatabase(const MediaEngine::SourceCandidate &candidate, const QString &readReason,
														const MediaEngine::Cancellation &cancellation) const override
			{
				return prepareDatabase(candidate, readReason, cancellation);
			}
			std::optional<MediaEngine::PreparedSource> processMxf(const MediaEngine::SourceCandidate &candidate,
																  const QString &readReason,
																  const MediaEngine::Cancellation &cancellation) const override
			{
				return prepareMxf(candidate, readReason, cancellation, m_mediaRetention);
			}

		private:
			SourceRetention m_mediaRetention;
		};
	}

	MediaEngine::ScanResult ScanEngine::scan(const MediaEngine::ScanRequest &request, const MediaEngine::Cancellation &cancellation,
											 const MediaEngine::ScanCallbacks &callbacks) const
	{
		const ImagePipeline pipeline(m_mediaRetention);
		return MediaEngine::ScanCoordinator{}.scan(request, cancellation, callbacks, &pipeline);
	}
}
