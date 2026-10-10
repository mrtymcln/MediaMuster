// Source storage changes here; MXF interpretation and selection remain MediaEngine's.
// Interrupted reads keep their actual obtained graph rather than being replayed
// later as though the original scan had completed.

#include "mxfsource.h"
#include "mediaengine/mxfreader.h"
#include "mediaengine/sourcearchive.h"
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

		struct ObjectHandle
		{
			qsizetype index = 0;
			MediaEngine::ObjectHandle handle = 0;
		};

		struct ReceiptLayout
		{
			SourceSnapshotRef snapshot;
			qsizetype objectCount = 0;
			// Ordinal handles and the shared source receipt need no per-object
			// allocation. Preserve exceptions without flattening distinct receipts.
			QVector<ObjectReceipt> receipts;
			QVector<ObjectHandle> handles;
			QVector<ReceiptLayout> embedded;
		};

		void requireMxf(const MediaEngine::SourceCandidate &candidate)
		{
			if (candidate.hint != MediaEngine::SourceCandidate::ReaderHint::Mxf)
				throw MxfSourceError("MediaEngine MXF storage requires an MXF candidate");
		}

		QSharedPointer<SourceSnapshot> inputReceipt(const MediaEngine::SourceCandidate &candidate)
		{
			return QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
				MetadataSource::Mxf, candidate.path, candidate.modified, SourceReadState::NotRead});
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

		MediaEngine::PreparedSource archive(MediaEngine::ParsedSource source, MediaEngine::Projection projection,
									 const MediaEngine::Cancellation &cancellation)
		{
			return {std::move(projection), MediaEngine::StoredSource::store(std::move(source), cancellation)};
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
				const auto &object = source.objects[index];
				if (object.snapshot != source.snapshot)
					layout.receipts.append({index, object.snapshot});
				if (object.handle != MediaEngine::ObjectHandle(index) + 1)
					layout.handles.append({index, object.handle});
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
				throw MxfSourceError("Retained MXF decoding changed its source receipt layout");
			source.snapshot = layout.snapshot;
			qsizetype exception = 0;
			for (qsizetype index = 0; index < source.objects.size(); ++index)
			{
				if (cancellation.cancelled())
					return false;
				auto &object = source.objects[index];
				const auto expected = exception < layout.handles.size() && layout.handles[exception].index == index
					? layout.handles[exception++].handle : MediaEngine::ObjectHandle(index) + 1;
				if (object.handle != expected)
					throw MxfSourceError("Retained MXF decoding changed its original object handles");
				object.snapshot = layout.snapshot;
			}
			for (const auto &receipt : layout.receipts)
			{
				if (cancellation.cancelled())
					return false;
				source.objects[receipt.index].snapshot = receipt.snapshot;
			}
			for (qsizetype index = 0; index < source.embeddedSources.size(); ++index)
				if (!restoreReceipts(source.embeddedSources[index], layout.embedded[index], cancellation))
					return false;
			return !cancellation.cancelled();
		}
	}

	struct MxfSource::Data
	{
		Data(MxfImage image, MediaEngine::StoredSource frame, ReceiptLayout receipts)
			: image(std::move(image)), frame(std::move(frame)), receipts(std::move(receipts)) {}
		MxfImage image;
		MediaEngine::StoredSource frame;
		ReceiptLayout receipts;
	};

	MxfSource::MxfSource(QSharedPointer<const Data> data) : m_data(std::move(data)) {}

	const MxfImage &MxfSource::image() const { return m_data->image; }

	std::optional<MediaEngine::ParsedSource> MxfSource::restore(const MediaEngine::Cancellation &cancellation) const
	{
		if (cancellation.cancelled())
			return std::nullopt;
		MxfReplayDevice input(m_data->image);
		if (!input.isOpen())
			throw MxfSourceError("Cannot open the retained MXF image for reading");
		auto source = MediaEngine::MxfReader{}.read(input, {m_data->frame.snapshot, cancellation});
		if (cancellation.cancelled())
			return std::nullopt;
		if (source.outcome != m_data->frame.outcome || source.container != m_data->frame.container ||
			source.diagnostics != m_data->frame.diagnostics)
			throw MxfSourceError("Retained MXF decoding changed its original interpretation outcome");
		source.readReason = m_data->frame.readReason;
		if (!restoreReceipts(source, m_data->receipts, cancellation))
			return std::nullopt;
		return source;
	}

	MediaEngine::PreparedSource MxfSource::prepare(QIODevice &input, const MediaEngine::SourceCandidate &candidate,
											const QString &readReason, const MediaEngine::Cancellation &cancellation)
	{
		const auto snapshot = inputReceipt(candidate);
		if (cancellation.cancelled())
		{
			// Cancellation during file opening reaches the reader's own outcome
			// without an additional network query to initialize the capture extent.
			auto parsed = MediaEngine::MxfReader{}.read(input, {snapshot, cancellation});
			parsed.readReason = readReason;
			return archive(std::move(parsed), {}, cancellation);
		}
		MxfCaptureDevice capture(input);
		auto parsed = MediaEngine::MxfReader{}.read(capture, {snapshot, cancellation});
		parsed.readReason = readReason;
		auto image = capture.takeImage();
		MediaEngine::Projection projection;
		if (!cancellation.cancelled())
			projection = MediaEngine::projectMxf(parsed, cancellation);
		if (parsed.outcome != Outcome::Complete || !image.valid() || cancellation.cancelled())
			return archive(std::move(parsed), std::move(projection), cancellation);
		auto receipts = collectReceipts(parsed, cancellation);
		if (!receipts)
			return archive(std::move(parsed), std::move(projection), cancellation);
		MediaEngine::PreparedSource prepared{std::move(projection), sourceFrame(parsed)};
		auto data = QSharedPointer<MxfSource::Data>::create(std::move(image), prepared.source, std::move(*receipts));
		prepared.source.storage = QSharedPointer<MxfSource>::create(std::move(data));
		return prepared;
	}

	MediaEngine::PreparedSource prepareMxf(QIODevice &input, const MediaEngine::SourceCandidate &candidate,
								 const QString &readReason, const MediaEngine::Cancellation &cancellation)
	{
		requireMxf(candidate);
		if (cancellation.cancelled())
		{
			MediaEngine::ParsedSource parsed;
			parsed.snapshot = inputReceipt(candidate);
			parsed.outcome = Outcome::Cancelled;
			parsed.readReason = readReason;
			return archive(std::move(parsed), {}, cancellation);
		}
		return MxfSource::prepare(input, candidate, readReason, cancellation);
	}

	MediaEngine::PreparedSource prepareMxf(const MediaEngine::SourceCandidate &candidate, const QString &readReason,
								 const MediaEngine::Cancellation &cancellation)
	{
		requireMxf(candidate);
		QFile input(candidate.path);
		if (cancellation.cancelled())
			return prepareMxf(input, candidate, readReason, cancellation);
		if (input.open(QIODevice::ReadOnly))
			return MxfSource::prepare(input, candidate, readReason, cancellation);
		MediaEngine::ParsedSource parsed;
		auto snapshot = inputReceipt(candidate);
		parsed.snapshot = snapshot;
		parsed.readReason = readReason;
		// As in MediaEngine, cancellation during a failed open does not erase the
		// filesystem failure which this scan actually obtained.
		snapshot->readState = SourceReadState::Unreadable;
		parsed.outcome = Outcome::IoError;
		parsed.diagnostics.append(input.errorString());
		return archive(std::move(parsed), {}, cancellation);
	}
}
