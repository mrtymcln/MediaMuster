// Reads each database into a temporary buffer and projects its supported facts.
// Scoped ownership releases the buffer and source records after preparation.

#include "databasesource.h"
#include "databaseimage.h"
#include "mdbreader.h"
#include "pmrreader.h"
#include <QBuffer>
#include <QFile>
#include <utility>

namespace MediaEngine
{
	namespace
	{
		using Outcome = ParsedSource::Outcome;

		MetadataSource sourceKind(SourceCandidate::ReaderHint hint)
		{
			switch (hint)
			{
			case SourceCandidate::ReaderHint::Pmr:
				return MetadataSource::Pmr;
			case SourceCandidate::ReaderHint::Mdb:
				return MetadataSource::Mdb;
			default:
				throw DatabaseSourceError("Database preparation requires a PMR or MDB candidate");
			}
		}

		SourceReadState readState(Outcome outcome)
		{
			if (outcome == Outcome::Complete)
				return SourceReadState::Complete;
			if (outcome == Outcome::IoError)
				return SourceReadState::Unreadable;
			if (outcome == Outcome::NotRead)
				return SourceReadState::NotRead;
			return SourceReadState::Incomplete;
		}

		ParsedSource readImage(const DatabaseImage &image, SourceCandidate::ReaderHint hint,
							   const SourceSnapshotRef &snapshot, const Cancellation &cancellation)
		{
			QBuffer input;
			// Qt shares the owned bytes; the reader receives a read-only device.
			input.setData(image.bytes());
			if (!input.open(QIODevice::ReadOnly))
				throw DatabaseSourceError("Cannot open the database buffer for reading");
			const ReaderContext context{snapshot, cancellation};
			switch (hint)
			{
			case SourceCandidate::ReaderHint::Pmr:
				return PmrReader{}.read(input, context);
			case SourceCandidate::ReaderHint::Mdb:
				return MdbReader{}.read(input, context);
			default:
				throw DatabaseSourceError("Cannot decode a non-database buffer as PMR or MDB");
			}
		}
	}

	PreparedSource prepareDatabase(const SourceCandidate &candidate, const QString &readReason,
								   const Cancellation &cancellation)
	{
		const auto kind = sourceKind(candidate.hint);
		auto snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			kind, candidate.path, candidate.modified, SourceReadState::NotRead});
		QFile input(candidate.path);
		const bool opened = !cancellation.cancelled() && input.open(QIODevice::ReadOnly);
		const auto image = DatabaseImage::read(input, cancellation);
		ParsedSource parsed;
		const bool interpreted = image.acquisitionComplete();
		if (interpreted)
			parsed = readImage(image, candidate.hint, snapshot, cancellation);
		else
		{
			parsed.outcome = image.outcome();
			parsed.diagnostics = image.diagnostics();
			if (!opened && !cancellation.cancelled())
				parsed.diagnostics.prepend(input.errorString());
			snapshot->readState = readState(parsed.outcome);
			parsed.snapshot = snapshot;
		}
		parsed.readReason = readReason;
		Projection projection;
		if (interpreted && !cancellation.cancelled())
			projection = candidate.hint == SourceCandidate::ReaderHint::Pmr
						 ? projectPmr(parsed, cancellation)
						 : projectMdb(parsed, cancellation);
		return {std::move(projection), SourceReceipt::fromParsed(std::move(parsed))};
	}
}
