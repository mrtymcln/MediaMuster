#pragma once

// Reads and projects MXF metadata. Optional replay storage keeps acquired bytes;
// ordinary scans retain supported observations and the source receipt instead.

#include "mxfimage.h"
#include "mediaengine/sourcepipeline.h"
#include "mediaengine/sourcestore.h"
#include <stdexcept>

namespace MediaEngine
{
	class MxfSourceError : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	class MxfSource final : public MediaEngine::SourceStore
	{
	public:
		const MxfImage &image() const;
		std::optional<MediaEngine::ParsedSource> restore(const MediaEngine::Cancellation &cancellation) const override;

	private:
		struct Data;
		explicit MxfSource(QSharedPointer<const Data> data);
		static MediaEngine::PreparedSource prepare(QIODevice &, const MediaEngine::SourceCandidate &, const QString &,
												   const MediaEngine::Cancellation &, SourceRetention);
		friend class QSharedPointer<MxfSource>;
		friend MediaEngine::PreparedSource prepareMxf(const MediaEngine::SourceCandidate &, const QString &,
													  const MediaEngine::Cancellation &, SourceRetention);
		friend MediaEngine::PreparedSource prepareMxf(QIODevice &, const MediaEngine::SourceCandidate &, const QString &,
													  const MediaEngine::Cancellation &, SourceRetention);
		QSharedPointer<const Data> m_data;
	};

	MediaEngine::PreparedSource prepareMxf(const MediaEngine::SourceCandidate &candidate, const QString &readReason,
										   const MediaEngine::Cancellation &cancellation,
										   SourceRetention retention = SourceRetention::MetadataOnly);
	// Borrows an already-open input. It never closes the caller's device; controlled
	// I/O tests use this same acquisition path without inventing MXF structures.
	MediaEngine::PreparedSource prepareMxf(QIODevice &input, const MediaEngine::SourceCandidate &candidate,
										   const QString &readReason, const MediaEngine::Cancellation &cancellation,
										   SourceRetention retention = SourceRetention::MetadataOnly);
}
