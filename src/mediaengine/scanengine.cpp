// Supplies the chosen retention policy for database and media sources to the coordinator.

#include "scanengine.h"
#include "databasesource.h"
#include "mxfsource.h"

namespace MediaEngine
{
	namespace
	{
		class ReadingPipeline final : public MediaEngine::SourcePipeline
		{
		public:
			explicit ReadingPipeline(SourceRetention sourceRetention) : m_sourceRetention(sourceRetention) {}
			SourceRetention sourceRetention() const override { return m_sourceRetention; }
			MediaEngine::PreparedSource processDatabase(const MediaEngine::SourceCandidate &candidate, const QString &readReason,
														const MediaEngine::Cancellation &cancellation) const override
			{
				return prepareDatabase(candidate, readReason, cancellation, m_sourceRetention);
			}
			std::optional<MediaEngine::PreparedSource> processMxf(const MediaEngine::SourceCandidate &candidate,
																  const QString &readReason,
																  const MediaEngine::Cancellation &cancellation) const override
			{
				return prepareMxf(candidate, readReason, cancellation, m_sourceRetention);
			}

		private:
			SourceRetention m_sourceRetention;
		};
	}

	MediaEngine::ScanResult ScanEngine::scan(const MediaEngine::ScanRequest &request, const MediaEngine::Cancellation &cancellation,
											 const MediaEngine::ScanCallbacks &callbacks) const
	{
		const ReadingPipeline pipeline(m_sourceRetention);
		return MediaEngine::ScanCoordinator{}.scan(request, cancellation, callbacks, &pipeline);
	}
}
