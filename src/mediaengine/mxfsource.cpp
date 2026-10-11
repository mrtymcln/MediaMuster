// Prepares MXF facts without retaining a second copy of the reading materials.

#include "mxfsource.h"
#include "mxfreader.h"
#include <QFile>
#include <utility>

namespace MediaEngine
{
	namespace
	{
		void requireMxf(const SourceCandidate &candidate)
		{
			if (candidate.hint != SourceCandidate::ReaderHint::Mxf)
				throw MxfSourceError("MXF preparation requires an MXF candidate");
		}

		QSharedPointer<SourceSnapshot> inputReceipt(const SourceCandidate &candidate)
		{
			return QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
				MetadataSource::Mxf, candidate.path, candidate.modified, SourceReadState::NotRead});
		}

		PreparedSource readMxf(QIODevice &input, const SourceCandidate &candidate,
							   const QString &readReason, const Cancellation &cancellation)
		{
			auto parsed = MxfReader{}.read(input, {inputReceipt(candidate), cancellation});
			parsed.readReason = readReason;
			Projection projection;
			if (!cancellation.cancelled())
				projection = projectMxf(parsed, cancellation);
			return {std::move(projection), SourceReceipt::fromParsed(std::move(parsed))};
		}
	}

	PreparedSource prepareMxf(QIODevice &input, const SourceCandidate &candidate,
							  const QString &readReason, const Cancellation &cancellation)
	{
		requireMxf(candidate);
		if (cancellation.cancelled())
		{
			ParsedSource parsed;
			parsed.snapshot = inputReceipt(candidate);
			parsed.outcome = ParsedSource::Outcome::Cancelled;
			parsed.readReason = readReason;
			return {{}, SourceReceipt::fromParsed(std::move(parsed))};
		}
		return readMxf(input, candidate, readReason, cancellation);
	}

	PreparedSource prepareMxf(const SourceCandidate &candidate, const QString &readReason,
							  const Cancellation &cancellation)
	{
		requireMxf(candidate);
		QFile input(candidate.path);
		if (cancellation.cancelled())
			return prepareMxf(input, candidate, readReason, cancellation);
		if (input.open(QIODevice::ReadOnly))
			return readMxf(input, candidate, readReason, cancellation);
		ParsedSource parsed;
		auto snapshot = inputReceipt(candidate);
		parsed.snapshot = snapshot;
		parsed.readReason = readReason;
		// Cancellation during a failed open does not erase the filesystem failure
		// that this scan actually obtained.
		snapshot->readState = SourceReadState::Unreadable;
		parsed.outcome = ParsedSource::Outcome::IoError;
		parsed.diagnostics.append(input.errorString());
		return {{}, SourceReceipt::fromParsed(std::move(parsed))};
	}
}
