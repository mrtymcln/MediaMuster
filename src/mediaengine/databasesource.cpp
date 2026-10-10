// Uses MediaEngine's database readers and projections without keeping their expanded
// graphs after a completed read. The original image remains available in RAM.

#include "databasesource.h"
#include "mediaengine/mdbreader.h"
#include "mediaengine/pmrreader.h"
#include <QBuffer>
#include <QFile>
#include <utility>

namespace MediaEngine
{
	namespace
	{
		using Outcome = MediaEngine::ParsedSource::Outcome;

		struct ObjectReceipt
		{
			qsizetype index = 0;
			SourceSnapshotRef snapshot;
		};

		struct ReceiptLayout
		{
			SourceSnapshotRef snapshot;
			qsizetype objectCount = 0;
			// Database objects normally share their source receipt. Keep only
			// exceptions, including null receipts, without one pointer per record.
			QVector<ObjectReceipt> overrides;
			QVector<ReceiptLayout> embedded;
		};

		MetadataSource sourceKind(MediaEngine::SourceCandidate::ReaderHint hint)
		{
			switch (hint)
			{
			case MediaEngine::SourceCandidate::ReaderHint::Pmr:
				return MetadataSource::Pmr;
			case MediaEngine::SourceCandidate::ReaderHint::Mdb:
				return MetadataSource::Mdb;
			default:
				throw DatabaseSourceError("MediaEngine database storage requires a PMR or MDB candidate");
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

		MediaEngine::ParsedSource readImage(const DatabaseImage &image, MediaEngine::SourceCandidate::ReaderHint hint,
											const SourceSnapshotRef &snapshot, const MediaEngine::Cancellation &cancellation)
		{
			QBuffer input;
			// QByteArray sharing keeps the captured image immutable and avoids a
			// second byte allocation. The reader receives only a read-only device.
			input.setData(image.bytes());
			if (!input.open(QIODevice::ReadOnly))
				throw DatabaseSourceError("Cannot open the retained database image for reading");
			const MediaEngine::ReaderContext context{snapshot, cancellation};
			switch (hint)
			{
			case MediaEngine::SourceCandidate::ReaderHint::Pmr:
				return MediaEngine::PmrReader{}.read(input, context);
			case MediaEngine::SourceCandidate::ReaderHint::Mdb:
				return MediaEngine::MdbReader{}.read(input, context);
			default:
				throw DatabaseSourceError("Cannot decode a non-database image as PMR or MDB");
			}
		}

		MediaEngine::StoredSource sourceFrame(const MediaEngine::ParsedSource &source)
		{
			MediaEngine::StoredSource frame;
			frame.outcome = source.outcome;
			frame.readReason = source.readReason;
			frame.snapshot = source.snapshot;
			frame.container = source.container;
			frame.diagnostics = source.diagnostics;
			return frame;
		}

		MediaEngine::ParsedSource frameSource(const MediaEngine::StoredSource &frame)
		{
			MediaEngine::ParsedSource source;
			source.outcome = frame.outcome;
			source.readReason = frame.readReason;
			source.snapshot = frame.snapshot;
			source.container = frame.container;
			source.diagnostics = frame.diagnostics;
			return source;
		}

		std::optional<ReceiptLayout> collectReceipts(const MediaEngine::ParsedSource &source,
													 const MediaEngine::Cancellation &cancellation)
		{
			if (cancellation.cancelled())
				return std::nullopt;
			ReceiptLayout layout;
			layout.snapshot = source.snapshot;
			layout.objectCount = source.objects.size();
			for (qsizetype index = 0; index < source.objects.size(); ++index)
			{
				if (cancellation.cancelled())
					return std::nullopt;
				const auto &snapshot = source.objects[index].snapshot;
				if (snapshot != source.snapshot)
					layout.overrides.append({index, snapshot});
			}
			for (const auto &embedded : source.embeddedSources)
			{
				auto child = collectReceipts(embedded, cancellation);
				if (!child)
					return std::nullopt;
				layout.embedded.append(std::move(*child));
			}
			return layout;
		}

		bool restoreReceipts(MediaEngine::ParsedSource &source, const ReceiptLayout &layout,
							 const MediaEngine::Cancellation &cancellation)
		{
			if (cancellation.cancelled())
				return false;
			if (source.objects.size() != layout.objectCount || source.embeddedSources.size() != layout.embedded.size())
				throw DatabaseSourceError("Retained database decoding changed its source receipt layout");
			source.snapshot = layout.snapshot;
			for (auto &object : source.objects)
			{
				if (cancellation.cancelled())
					return false;
				object.snapshot = layout.snapshot;
			}
			for (const auto &override : layout.overrides)
			{
				if (cancellation.cancelled())
					return false;
				source.objects[override.index].snapshot = override.snapshot;
			}
			for (qsizetype index = 0; index < source.embeddedSources.size(); ++index)
				if (!restoreReceipts(source.embeddedSources[index], layout.embedded[index], cancellation))
					return false;
			return !cancellation.cancelled();
		}
	}

	struct DatabaseSource::Data
	{
		explicit Data(DatabaseImage image) : image(std::move(image)) {}
		DatabaseImage image;
		MediaEngine::SourceCandidate::ReaderHint hint = MediaEngine::SourceCandidate::ReaderHint::Pmr;
		MediaEngine::StoredSource frame;
		ReceiptLayout receipts;
		bool interpreted = false;
		// A cancelled interpretation cannot be reproduced with a fresh token:
		// keep the exact facts already obtained, sharing the scanner's fallback.
		QSharedPointer<const MediaEngine::ParsedSource> unfinishedGraph;
	};

	DatabaseSource::DatabaseSource(QSharedPointer<const Data> data) : m_data(std::move(data)) {}

	const DatabaseImage &DatabaseSource::image() const { return m_data->image; }

	std::optional<MediaEngine::ParsedSource> DatabaseSource::restore(const MediaEngine::Cancellation &cancellation) const
	{
		if (cancellation.cancelled())
			return std::nullopt;
		if (m_data->unfinishedGraph)
		{
			MediaEngine::ParsedSource source = *m_data->unfinishedGraph;
			if (cancellation.cancelled())
				return std::nullopt;
			return source;
		}
		if (!m_data->interpreted)
		{
			// Acquisition failures retain their prefix and original outcome. A
			// later inspection does not reinterpret that prefix as a whole file.
			auto source = frameSource(m_data->frame);
			if (cancellation.cancelled())
				return std::nullopt;
			return source;
		}
		auto source = readImage(m_data->image, m_data->hint, m_data->frame.snapshot, cancellation);
		if (cancellation.cancelled())
			return std::nullopt;
		if (source.outcome != m_data->frame.outcome || source.container != m_data->frame.container ||
			source.diagnostics != m_data->frame.diagnostics)
			throw DatabaseSourceError("Retained database decoding changed its original interpretation outcome");
		source.readReason = m_data->frame.readReason;
		if (!restoreReceipts(source, m_data->receipts, cancellation))
			return std::nullopt;
		return source;
	}

	MediaEngine::PreparedSource prepareDatabase(const MediaEngine::SourceCandidate &candidate, const QString &readReason,
												const MediaEngine::Cancellation &cancellation)
	{
		const auto kind = sourceKind(candidate.hint);
		auto snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			kind, candidate.path, candidate.modified, SourceReadState::NotRead});
		QFile input(candidate.path);
		const bool opened = !cancellation.cancelled() && input.open(QIODevice::ReadOnly);
		auto image = DatabaseImage::read(input, cancellation);
		MediaEngine::ParsedSource parsed;
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
		MediaEngine::PreparedSource prepared;
		if (interpreted && !cancellation.cancelled())
			prepared.projection = candidate.hint == MediaEngine::SourceCandidate::ReaderHint::Pmr
									  ? MediaEngine::projectPmr(parsed, cancellation)
									  : MediaEngine::projectMdb(parsed, cancellation);
		auto data = QSharedPointer<DatabaseSource::Data>::create(std::move(image));
		data->hint = candidate.hint;
		data->frame = sourceFrame(parsed);
		if (interpreted)
		{
			auto receipts = collectReceipts(parsed, cancellation);
			if (receipts && parsed.outcome != Outcome::Cancelled && !cancellation.cancelled())
			{
				data->receipts = std::move(*receipts);
				data->interpreted = true;
			}
			else
				data->unfinishedGraph = QSharedPointer<const MediaEngine::ParsedSource>::create(std::move(parsed));
		}
		prepared.source = data->frame;
		prepared.source.unfinishedGraph = data->unfinishedGraph;
		prepared.source.storage = QSharedPointer<DatabaseSource>::create(std::move(data));
		return prepared;
	}
}
