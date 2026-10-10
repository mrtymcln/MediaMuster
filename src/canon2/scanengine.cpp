// Supplies the database storage path to Canon's existing scan coordinator.

#include "scanengine.h"
#include "databasesource.h"

namespace Canon2
{
	namespace
	{
		class DatabasePipeline final : public Canon::SourcePipeline
		{
		public:
			Canon::PreparedSource processDatabase(const Canon::SourceCandidate &candidate, const QString &readReason,
												 const Canon::Cancellation &cancellation) const override
			{
				return prepareDatabase(candidate, readReason, cancellation);
			}
		};
	}

	Canon::ScanResult ScanEngine::scan(const Canon::ScanRequest &request, const Canon::Cancellation &cancellation,
									  const Canon::ScanCallbacks &callbacks) const
	{
		const DatabasePipeline pipeline;
		return Canon::ScanEngine{}.scan(request, cancellation, callbacks, &pipeline);
	}
}
