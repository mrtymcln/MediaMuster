#include "media_labels.h"

#include "mdb_parser.h"
#include "mxf_parser.h"
#include "omf_parser.h"
#include "scanner_lookup/compression_names.h"
#include "scanner_lookup/dnx_names.h"
#include "scanner_lookup/effect_catalogue.h"

#include <array>
#include <utility>

namespace Bella
{
	namespace
	{
		// MARK: - Compression naming helpers

		using Facts = Bella::Lookup::CompressionFacts;

		template <std::size_t N>
		QByteArray toByteArray(const std::array<quint8, N> &value)
		{
			return QByteArray(reinterpret_cast<const char *>(value.data()), qsizetype(N));
		}

		QString legacyDescriptorClassName(const std::optional<std::array<quint8, 4>> &value)
		{
			return value ? QString::fromLatin1(toByteArray(*value)) : QString{};
		}

		template <typename Rate>
		MediaRate legacyRate(const std::optional<Rate> &value)
		{
			return value ? MediaRate{value->words.numerator, value->words.denominator} : MediaRate{};
		}

		template <typename File>
		Facts legacyCompressionFacts(const File &file)
		{
			Facts facts;
			facts.descriptorClass = legacyDescriptorClassName(file.descriptorClass);
			const auto &coding = file.compression;
			using CompressionCodingState = decltype(coding.codingState);
			facts.isCodingPropertyAbsent = coding.codingState == CompressionCodingState::Absent;
			if (coding.codingState == CompressionCodingState::Present)
				facts.codingLabel = toByteArray(coding.codingLabel);
			else if (facts.isCodingPropertyAbsent && coding.resolutionId)
				facts.codingLabel = Lookup::codingLabelForDnxResolution(*coding.resolutionId);
			if (coding.legacyCompression)
				facts.legacyCompressionIdentifier = toByteArray(*coding.legacyCompression);
			facts.legacyResolutionId = coding.resolutionId;
			if (coding.namingGeometry)
				facts.rasterSize = {coding.namingGeometry->width, coding.namingGeometry->height};
			facts.namingRate = legacyRate(file.sampleRate);
			if (coding.resolutionId && facts.codingLabel == Lookup::codingLabelForDnxResolution(*coding.resolutionId))
				facts.namingRate = Lookup::dnxRateForResolution(*coding.resolutionId, facts.namingRate);
			facts.frameLayout = coding.frameLayout;
			facts.rawComponentDepth = file.bitDepth.rawComponentWidth;
			facts.horizontalSubsampling = coding.horizontalSubsampling;
			facts.verticalSubsampling = coding.verticalSubsampling;
			return facts;
		}

		bool hasSingleComponentValue(const MdbComponentArray &value, quint8 expected)
		{
			return (value.count == 1 || (value.count == 2 && value.values[1] == 0)) && value.values[0] == expected;
		}

		std::optional<qint64> mdbAlphaDepth(const MdbFileRecord &file, const Facts &facts)
		{
			if (!facts.isCodingPropertyAbsent || facts.descriptorClass != QLatin1String("RGBA") ||
				facts.legacyCompressionIdentifier != "NONE")
				return std::nullopt;
			const auto &layout = file.bitDepth.pixelLayout;
			const auto &width = file.bitDepth.pixelStructure;
			if (layout && width && hasSingleComponentValue(*layout, 'A') && width->values[0] > 0 && width->values[0] <= 64 &&
				hasSingleComponentValue(*width, width->values[0]))
				return width->values[0];
			return std::nullopt;
		}

		QString mxfDescriptorClassName(const std::optional<std::array<quint8, 16>> &value)
		{
			if (!value || ((*value)[5] != 0x13 && (*value)[5] != 0x53))
				return {};
			auto key = toByteArray(*value);
			key[5] = 0x53;
			key[7] = 0x01; // Semantic class matching admits the reader's registry-version variation.
			if (key == QByteArray::fromHex("060e2b34025301010d01010101012800"))
				return QStringLiteral("CDCIEssenceDescriptor");
			if (key == QByteArray::fromHex("060e2b34025301010d01010101012900"))
				return QStringLiteral("RGBAEssenceDescriptor");
			return {};
		}

		template <typename Encoding>
		QString audioCompressionName(Encoding encoding)
		{
			if (encoding == Encoding::Pcm)
				return QStringLiteral("PCM");
			if (encoding == Encoding::IeeeFloat)
				return QStringLiteral("IEEE float");
			return {};
		}

		CompressionNames resolvedCompressionNames(const Facts &facts, const QString &audio = {})
		{
			auto names = Bella::Lookup::compressionNames(facts);
			if (names.compressionName.isEmpty())
				names.compressionName = audio;
			else if (!audio.isEmpty() && names.compressionName != audio)
				names.compressionName = {}; // Equal-source contradictory claims do not choose a winner.
			return {std::move(names.compressionName), std::move(names.newDnx), std::move(names.oldDnx),
					std::move(names.reallyOldDnx)};
		}
	} // namespace

	// MARK: - Compression names

	CompressionNames compressionNamesFor(const MdbFileRecord &file)
	{
		if (!file.ownDescriptorQualified)
			return {};
		auto facts = legacyCompressionFacts(file);
		facts.alphaChannelBitDepth = mdbAlphaDepth(file, facts);
		return resolvedCompressionNames(facts, audioCompressionName(file.compression.audioEncoding));
	}

	CompressionNames compressionNamesFor(const OmfFileInformation &file)
	{
		auto facts = legacyCompressionFacts(file);
		if (facts.isCodingPropertyAbsent && facts.descriptorClass == QLatin1String("RGBA") && facts.legacyCompressionIdentifier == "NONE")
			facts.alphaChannelBitDepth = file.compression.alphaDepth;
		return resolvedCompressionNames(facts, audioCompressionName(file.compression.audioEncoding));
	}

	CompressionNames compressionNamesFor(const MxfFileInformation &file)
	{
		Facts facts;
		facts.descriptorClass = mxfDescriptorClassName(file.descriptorClass);
		const auto &coding = file.compression;
		facts.isCodingPropertyAbsent = coding.codingState == MxfCodingState::Absent;
		if (coding.codingState == MxfCodingState::Present)
			facts.codingLabel = toByteArray(coding.codingLabel);
		if (coding.namingGeometry)
			facts.rasterSize = {coding.namingGeometry->width, coding.namingGeometry->height};
		if (file.sampleRate)
			facts.namingRate = {file.sampleRate->numerator, file.sampleRate->denominator};
		facts.frameLayout = coding.frameLayout;
		facts.rawComponentDepth = file.bitDepth.rawComponentDepth;
		facts.horizontalSubsampling = coding.horizontalSubsampling;
		facts.verticalSubsampling = coding.verticalSubsampling;
		auto names = resolvedCompressionNames(facts);
		if (names.compressionName.isEmpty() && facts.isCodingPropertyAbsent)
		{
			if (coding.namingFallback == MxfNamingFallback::Pcm)
				names.compressionName = QStringLiteral("PCM");
			else if (coding.namingFallback == MxfNamingFallback::UncompressedAlpha8)
				names.compressionName = QStringLiteral("1:1 Alpha 8bit");
		}
		return names;
	}

	CompressionNames compressionNamesFor(const OmfNativeAudioInformation &file)
	{
		return {audioCompressionName(file.encoding), {}, {}, {}};
	}

	// MARK: - Effect names

	EffectNames effectNamesFor(bool confirmedPrecompute, const QString &selectedClipName)
	{
		if (!confirmedPrecompute)
			return {};
		auto hit = AvidEffects::lookup(selectedClipName);
		return {hit.name.isEmpty() ? QStringLiteral("unknown") : std::move(hit.name), std::move(hit.category)};
	}
} // namespace Bella
