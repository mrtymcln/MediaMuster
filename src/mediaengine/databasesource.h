#pragma once

// Projects a database through the established reader. Ordinary scans keep the
// supported facts and receipt; optional replay storage keeps its original bytes.

#include "databaseimage.h"
#include "mediaengine/sourcepipeline.h"
#include "mediaengine/sourcestore.h"
#include <stdexcept>

namespace MediaEngine
{
	class DatabaseSourceError : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	class DatabaseSource final : public MediaEngine::SourceStore
	{
	public:
		const DatabaseImage &image() const;
		std::optional<MediaEngine::ParsedSource> restore(const MediaEngine::Cancellation &cancellation) const override;

	private:
		struct Data;
		explicit DatabaseSource(QSharedPointer<const Data> data);
		friend class QSharedPointer<DatabaseSource>;
		friend MediaEngine::PreparedSource prepareDatabase(const MediaEngine::SourceCandidate &, const QString &,
														   const MediaEngine::Cancellation &, SourceRetention);
		QSharedPointer<const Data> m_data;
	};

	MediaEngine::PreparedSource prepareDatabase(const MediaEngine::SourceCandidate &candidate, const QString &readReason,
												const MediaEngine::Cancellation &cancellation,
												SourceRetention retention = SourceRetention::MetadataOnly);
}
