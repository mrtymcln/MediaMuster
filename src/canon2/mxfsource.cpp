// Source storage changes here; MXF interpretation and selection remain Canon's.
// Interrupted reads keep their actual obtained graph rather than being replayed
// later as though the original scan had completed.

#include "mxfsource.h"
#include "canon/mxfreader.h"
#include "canon/sourcearchive.h"
#include <QFile>
#include <utility>

namespace Canon2
{
	namespace
	{
		using Outcome = Canon::ParsedSource::Outcome;

		struct ObjectReceipt
		{
			qsizetype index = 0;
			SourceSnapshotRef snapshot;
		};

		struct ObjectHandle
		{
			qsizetype index = 0;
			Canon::ObjectHandle handle = 0;
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

		void requireMxf(const Canon::SourceCandidate &candidate)
		{
			if (candidate.hint != Canon::SourceCandidate::ReaderHint::Mxf)
				throw MxfSourceError("Canon2 MXF storage requires an MXF candidate");
		}

		QSharedPointer<SourceSnapshot> inputReceipt(const Canon::SourceCandidate &candidate)
		{
			return QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
				MetadataSource::Mxf, candidate.path, candidate.modified, SourceReadState::NotRead});
		}

		Canon::StoredSource sourceFrame(const Canon::ParsedSource &source)
		{
			Canon::StoredSource frame;
			frame.outcome = source.outcome;
			frame.readReason = source.readReason;
			frame.snapshot = source.snapshot;
			frame.container = source.container;
			frame.diagnostics = source.diagnostics;
			return frame;
		}

		Canon::PreparedSource archive(Canon::ParsedSource source, Canon::Projection projection,
									 const Canon::Cancellation &cancellation)
		{
			return {std::move(projection), Canon::StoredSource::store(std::move(source), cancellation)};
		}

		std::optional<ReceiptLayout> collectReceipts(const Canon::ParsedSource &source,
													 const Canon::Cancellation &cancellation)
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
				if (object.handle != Canon::ObjectHandle(index) + 1)
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

		bool restoreReceipts(Canon::ParsedSource &source, const ReceiptLayout &layout,
							 const Canon::Cancellation &cancellation)
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
					? layout.handles[exception++].handle : Canon::ObjectHandle(index) + 1;
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
		Data(MxfImage image, Canon::StoredSource frame, ReceiptLayout receipts)
			: image(std::move(image)), frame(std::move(frame)), receipts(std::move(receipts)) {}
		MxfImage image;
		Canon::StoredSource frame;
		ReceiptLayout receipts;
	};

	MxfSource::MxfSource(QSharedPointer<const Data> data) : m_data(std::move(data)) {}

	const MxfImage &MxfSource::image() const { return m_data->image; }

	std::optional<Canon::ParsedSource> MxfSource::restore(const Canon::Cancellation &cancellation) const
	{
		if (cancellation.cancelled())
			return std::nullopt;
		MxfReplayDevice input(m_data->image);
		if (!input.isOpen())
			throw MxfSourceError("Cannot open the retained MXF image for reading");
		auto source = Canon::MxfReader{}.read(input, {m_data->frame.snapshot, cancellation});
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

	Canon::PreparedSource MxfSource::prepare(QIODevice &input, const Canon::SourceCandidate &candidate,
											const QString &readReason, const Canon::Cancellation &cancellation)
	{
		const auto snapshot = inputReceipt(candidate);
		if (cancellation.cancelled())
		{
			// Cancellation during file opening reaches the reader's own outcome
			// without an additional network query to initialize the capture extent.
			auto parsed = Canon::MxfReader{}.read(input, {snapshot, cancellation});
			parsed.readReason = readReason;
			return archive(std::move(parsed), {}, cancellation);
		}
		MxfCaptureDevice capture(input);
		auto parsed = Canon::MxfReader{}.read(capture, {snapshot, cancellation});
		parsed.readReason = readReason;
		auto image = capture.takeImage();
		Canon::Projection projection;
		if (!cancellation.cancelled())
			projection = Canon::projectMxf(parsed, cancellation);
		if (parsed.outcome != Outcome::Complete || !image.valid() || cancellation.cancelled())
			return archive(std::move(parsed), std::move(projection), cancellation);
		auto receipts = collectReceipts(parsed, cancellation);
		if (!receipts)
			return archive(std::move(parsed), std::move(projection), cancellation);
		Canon::PreparedSource prepared{std::move(projection), sourceFrame(parsed)};
		auto data = QSharedPointer<MxfSource::Data>::create(std::move(image), prepared.source, std::move(*receipts));
		prepared.source.storage = QSharedPointer<MxfSource>::create(std::move(data));
		return prepared;
	}

	Canon::PreparedSource prepareMxf(QIODevice &input, const Canon::SourceCandidate &candidate,
								 const QString &readReason, const Canon::Cancellation &cancellation)
	{
		requireMxf(candidate);
		if (cancellation.cancelled())
		{
			Canon::ParsedSource parsed;
			parsed.snapshot = inputReceipt(candidate);
			parsed.outcome = Outcome::Cancelled;
			parsed.readReason = readReason;
			return archive(std::move(parsed), {}, cancellation);
		}
		return MxfSource::prepare(input, candidate, readReason, cancellation);
	}

	Canon::PreparedSource prepareMxf(const Canon::SourceCandidate &candidate, const QString &readReason,
								 const Canon::Cancellation &cancellation)
	{
		requireMxf(candidate);
		QFile input(candidate.path);
		if (cancellation.cancelled())
			return prepareMxf(input, candidate, readReason, cancellation);
		if (input.open(QIODevice::ReadOnly))
			return MxfSource::prepare(input, candidate, readReason, cancellation);
		Canon::ParsedSource parsed;
		auto snapshot = inputReceipt(candidate);
		parsed.snapshot = snapshot;
		parsed.readReason = readReason;
		// As in Canon, cancellation during a failed open does not erase the
		// filesystem failure which this scan actually obtained.
		snapshot->readState = SourceReadState::Unreadable;
		parsed.outcome = Outcome::IoError;
		parsed.diagnostics.append(input.errorString());
		return archive(std::move(parsed), {}, cancellation);
	}
}
