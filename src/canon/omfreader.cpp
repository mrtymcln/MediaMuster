// Legacy media may contain native audio headers and an OMF object graph in the
// same physical file. Keep each source context, inspect bytes rather than names,
// and leave audio/video samples on disk whilst reading their metadata.

#include "omfreader.h"
#include "audioreader_p.h"
#include "omfobjects_p.h"
#include "sourcestorage_p.h"

#include <QSet>
#include <array>
#include <algorithm>

namespace Canon
{
	namespace
	{
		using Outcome = ParsedSource::Outcome;
		constexpr qint64 labelSize = 24;
		const QByteArray bentoMagic = QByteArray::fromHex("a4434da5486472d7");

		struct Failure
		{
			Outcome outcome;
			QString message;
		};

		void checkCancellation(const ReaderContext &context)
		{
			if (context.cancellation.cancelled())
				throw Failure{Outcome::Cancelled, QStringLiteral("Legacy media read cancelled.")};
		}

		RawProperty probe(QIODevice &source, const ReaderContext &context, qint64 offset,
						  qint64 length, const QString &name)
		{
			RawProperty property;
			property.locator.name = name;
			property.locator.ranges.append({offset, 0});
			property.state = PropertyReadState::Unreadable;
			if (!source.seek(offset))
			{
				property.interpretation = QStringLiteral("Cannot seek to container signature: %1").arg(source.errorString());
				return property;
			}
			std::array<char, labelSize> bytes{};
			while (property.encoding.size() < length)
			{
				// Returning the observation preserves any bytes already obtained.
				if (context.cancellation.cancelled())
					return property;
				const qint64 got = source.read(bytes.data(), std::min<qint64>(length - property.encoding.size(), bytes.size()));
				if (got <= 0)
				{
					property.interpretation = QStringLiteral("Cannot read container signature: %1").arg(source.errorString());
					return property;
				}
				property.encoding.append(bytes.data(), got);
				property.locator.ranges[0].length += got;
			}
			property.state = PropertyReadState::Present;
			return property;
		}

		void receipt(ParsedSource &result, const ReaderContext &context)
		{
			auto snapshot = QSharedPointer<SourceSnapshot>::create(context.snapshot ? *context.snapshot : SourceSnapshot{});
			snapshot->source = MetadataSource::Omf;
			snapshot->readState = result.outcome == Outcome::Complete  ? SourceReadState::Complete
								  : result.outcome == Outcome::IoError ? SourceReadState::Unreadable
																	   : SourceReadState::Incomplete;
			result.snapshot = snapshot;
			for (auto &object : result.objects)
				object.snapshot = snapshot;
		}

		ParsedSource omfAt(QIODevice &source, const ReaderContext &context, qint64 labelOffset)
		{
			Detail::BentoReadOptions options;
			options.metadataOnly = true;
			options.labelOffset = labelOffset;
			return Detail::interpretOmfObjects(Detail::readBento(source, context.cancellation, options), context);
		}

		void appendEmbedded(ParsedSource &parent, ParsedSource child, PropertyLocator embedding)
		{
			child.embedding = std::move(embedding);
			if (child.outcome != Outcome::Complete)
			{
				if (parent.outcome == Outcome::Complete)
					parent.outcome = Outcome::Incomplete;
				parent.diagnostics.append(QStringLiteral("Embedded OMF metadata was not completely interpreted; its source evidence and diagnostics are retained separately."));
			}
			parent.embeddedSources.append(std::move(child));
		}
	}

	ParsedSource OmfReader::read(QIODevice &source, const ReaderContext &context) const
	{
		ParsedSource result;
		try
		{
			checkCancellation(context);
			if (!source.isOpen() || !source.isReadable() || source.isSequential() || source.isTextModeEnabled())
				throw Failure{Outcome::IoError, QStringLiteral("Legacy reader requires an already-open, readable, seekable binary device.")};
			const qint64 extent = source.size();
			if (extent < 0)
				throw Failure{Outcome::IoError, QStringLiteral("Cannot determine legacy source length.")};
			auto signature = probe(source, context, 0, std::min<qint64>(12, extent), QStringLiteral("Media.Signature"));
			if (signature.state != PropertyReadState::Present)
			{
				result.unownedProperties.append(std::move(signature));
				checkCancellation(context);
				throw Failure{Outcome::IoError, QStringLiteral("Could not read the legacy media signature.")};
			}
			const QByteArray prefix = signature.encoding.first(std::min<qsizetype>(4, signature.encoding.size()));
			const bool audio = prefix == "RIFF" || prefix == "RF64" || prefix == "FORM";
			bool mayHaveTrailer = !audio;
			QSet<qint64> inspectedLabels;
			if (audio)
			{
				auto native = Detail::readChunkedAudio(source, context);
				mayHaveTrailer = native.trailingBytes.length >= labelSize;
				result = std::move(native.source);
				for (const auto &chunk : native.omfiChunks)
				{
					checkCancellation(context);
					PropertyLocator embedding;
					embedding.name = QStringLiteral("omfi");
					embedding.key = QByteArrayLiteral("omfi");
					embedding.ranges.append(chunk);
					if (chunk.length < labelSize)
					{
						ParsedSource child;
						child.outcome = Outcome::Incomplete;
						child.diagnostics.append(QStringLiteral("omfi chunk is too short to hold a Bento label."));
						receipt(child, context);
						appendEmbedded(result, std::move(child), std::move(embedding));
						continue;
					}
					const qint64 labelOffset = chunk.offset + chunk.length - labelSize;
					inspectedLabels.insert(labelOffset);
					appendEmbedded(result, omfAt(source, context, labelOffset), std::move(embedding));
				}
			}

			checkCancellation(context);
			if (mayHaveTrailer && extent >= labelSize && !inspectedLabels.contains(extent - labelSize))
			{
				auto tail = probe(source, context, extent - labelSize, labelSize, QStringLiteral("Media.TailSignature"));
				if (tail.state != PropertyReadState::Present)
				{
					result.unownedProperties.append(std::move(tail));
					checkCancellation(context);
					throw Failure{Outcome::IoError, QStringLiteral("Could not inspect the legacy media trailer.")};
				}
				if (tail.encoding.startsWith(bentoMagic))
				{
					auto graph = omfAt(source, context, extent - labelSize);
					if (!audio)
						result = std::move(graph);
					else
					{
						PropertyLocator embedding;
						embedding.name = QStringLiteral("Bento.Trailer");
						embedding.ranges.append({extent - labelSize, labelSize});
						appendEmbedded(result, std::move(graph), std::move(embedding));
					}
				}
				else if (!audio)
					result.unownedProperties.append(std::move(tail));
			}
			if (!audio && result.outcome == Outcome::NotRead)
			{
				result.unownedProperties.prepend(std::move(signature));
				throw Failure{extent < 12 ? Outcome::Incomplete : Outcome::Unsupported,
							  QStringLiteral("No supported OMF, RIFF/WAVE or FORM/AIFF container was established from these bytes.")};
			}
			checkCancellation(context);
			if (source.size() != extent)
			{
				if (result.outcome == Outcome::Complete)
					result.outcome = Outcome::Incomplete;
				result.diagnostics.append(QStringLiteral("Legacy source length changed during reading; check this source again."));
			}
		}
		catch (const Failure &failure)
		{
			result.outcome = failure.outcome;
			result.diagnostics.append(failure.message);
		}
		receipt(result, context);
		Detail::squeezeSourceStorage(result, context.cancellation);
		return result;
	}
}
