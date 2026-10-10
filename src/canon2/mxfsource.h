#pragma once

// Keeps the MXF metadata bytes acquired by the verified reader in RAM. Table
// values stay ready to use; detailed records are rebuilt from these bytes only.

#include "mxfimage.h"
#include "canon/sourcepipeline.h"
#include "canon/sourcestore.h"
#include <stdexcept>

namespace Canon2
{
	class MxfSourceError : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	class MxfSource final : public Canon::SourceStore
	{
	public:
		const MxfImage &image() const;
		std::optional<Canon::ParsedSource> restore(const Canon::Cancellation &cancellation) const override;

	private:
		struct Data;
		explicit MxfSource(QSharedPointer<const Data> data);
		static Canon::PreparedSource prepare(QIODevice &, const Canon::SourceCandidate &, const QString &,
											const Canon::Cancellation &);
		friend class QSharedPointer<MxfSource>;
		friend Canon::PreparedSource prepareMxf(const Canon::SourceCandidate &, const QString &,
											   const Canon::Cancellation &);
		friend Canon::PreparedSource prepareMxf(QIODevice &, const Canon::SourceCandidate &, const QString &,
											   const Canon::Cancellation &);
		QSharedPointer<const Data> m_data;
	};

	Canon::PreparedSource prepareMxf(const Canon::SourceCandidate &candidate, const QString &readReason,
								 const Canon::Cancellation &cancellation);
	// Borrows an already-open input. It never closes the caller's device; controlled
	// I/O tests use this same acquisition path without inventing MXF structures.
	Canon::PreparedSource prepareMxf(QIODevice &input, const Canon::SourceCandidate &candidate,
								 const QString &readReason, const Canon::Cancellation &cancellation);
}
