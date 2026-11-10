#include "media_labels.h"

#include "mdb_parser.h"
#include "mxf_parser.h"
#include "omf_parser.h"
#include "scanner_lookup/compression_names.h"
#include "scanner_lookup/dnx_names.h"
#include "scanner_lookup/effect_catalogue.h"

#include <QByteArrayView>
#include <QStringList>
#include <algorithm>
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

		struct NumericFormatText
		{
			QString bitDepthText;
			QString sampleFormatText;
		};

		NumericFormatText numericFormatText(const QByteArray &coding, qint64 raw, bool mxf)
		{
			static const auto kDnxUncompressedCodingLabel = QByteArray::fromHex("060e2b340401010d0401020203070100");
			static const auto kDnxUncompressedFixedPointCodingLabel = QByteArray::fromHex("060e2b340401010d0401020203070200");
			if (coding == kDnxUncompressedCodingLabel)
			{
				if (raw == 253)
					return {QStringLiteral("16-bit"), QStringLiteral("Half float")};
				if (raw == 254)
					return {QStringLiteral("32-bit"), QStringLiteral("Float")};
				if (raw == 8 || raw == 10 || raw == 12 || raw == 16)
					return {QStringLiteral("%1-bit").arg(raw), QStringLiteral("Integer")};
			}
			else if (coding == kDnxUncompressedFixedPointCodingLabel)
			{
				if (raw == 254)
					return {QStringLiteral("16-bit"), QStringLiteral("S2.14 fixed point")};
				if (raw == 10 || raw == 12)
					return {QStringLiteral("16-bit"),
							raw == 10 ? QStringLiteral("10.6 fixed point") : QStringLiteral("12.4 fixed point")};
			}
			else if (mxf)
			{
				if (raw == 253)
					return {QStringLiteral("16-bit"), QStringLiteral("Half float")};
				if (raw == 254)
					return {QStringLiteral("32-bit"), QStringLiteral("Float")};
				if (raw == 255)
					return {QStringLiteral("64-bit"), QStringLiteral("64-bit float")};
				if (raw > 0 && raw <= 32)
					return {QStringLiteral("%1-bit").arg(raw), QStringLiteral("Integer")};
			}
			// Legacy numeric widths can survive without a supported representation.
			return {!mxf && raw > 0 && raw <= 64 ? QStringLiteral("%1-bit").arg(raw) : QString{}, {}};
		}

		template <typename File>
		void legacyNumericFormat(Facts &facts, const File &file)
		{
			// Legacy RGBA supplies a common width, not a SampleFormat description.
			if (facts.descriptorClass == QLatin1String("RGBA") || !file.bitDepth.rawComponentWidth)
				return;
			const auto format = numericFormatText(facts.codingLabel, *file.bitDepth.rawComponentWidth, false);
			facts.bitDepthText = format.bitDepthText;
			facts.sampleFormatText = format.sampleFormatText;
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

		std::optional<qint64> mxfDnxAlphaDepth(const MxfFileInformation &file, const Facts &facts)
		{
			static const auto kDnxUncompressedCodingLabel = QByteArray::fromHex("060e2b340401010d0401020203070100");
			if (facts.codingLabel != kDnxUncompressedCodingLabel ||
				facts.descriptorClass != QLatin1String("RGBAEssenceDescriptor") || !file.bitDepth.pixelLayout)
				return std::nullopt;
			std::optional<qint64> alphaDepth;
			bool ended = false;
			// Recheck the layout because the reader also retains rejected layouts.
			for (std::size_t i = 0; i < 16; i += 2)
			{
				const auto code = (*file.bitDepth.pixelLayout)[i], raw = (*file.bitDepth.pixelLayout)[i + 1];
				if ((code == 0 || code == '0') && raw == 0)
				{
					ended = true;
					continue;
				}
				if (ended)
					return std::nullopt;
				if (code == 'F')
					continue;
				if (code != 'A' || alphaDepth || (raw != 8 && raw != 16))
					return std::nullopt;
				alphaDepth = raw;
			}
			return alphaDepth;
		}

		QString compactComponentText(const QStringList &components)
		{
			if (components.isEmpty())
				return {};
			const auto value = [](const QString &component)
			{
				return component.mid(component.indexOf(QLatin1String(": ")) + 2);
			};
			const auto first = value(components.first());
			return std::all_of(components.begin(), components.end(),
							   [&](const QString &component)
							   { return value(component) == first; })
					   ? first
					   : components.join(QStringLiteral("; "));
		}

		void mxfNumericFormat(Facts &facts, const MxfFileInformation &file)
		{
			if (file.bitDepth.rawComponentDepth)
			{
				const auto format = numericFormatText(facts.codingLabel, *file.bitDepth.rawComponentDepth, true);
				facts.bitDepthText = format.bitDepthText;
				facts.sampleFormatText = format.sampleFormatText;
			}
			if (!file.bitDepth.pixelLayout)
				return;
			QStringList depths, formats;
			bool ended = false;
			for (std::size_t i = 0; i < 16; i += 2)
			{
				const auto code = (*file.bitDepth.pixelLayout)[i], raw = (*file.bitDepth.pixelLayout)[i + 1];
				if (code == 0 && raw == 0)
				{
					ended = true;
					continue;
				}
				if (ended || code == 0 || raw == 0 || code == 'P' ||
					(!QByteArrayView("RGBArgbaFUVWXYZuvwxyz").contains(char(code)) && code != 0xd8 && code != 0xd9 &&
					 code != 0xda))
					return;
				if (code == 'F')
					continue;
				const auto format = numericFormatText(facts.codingLabel, raw, true);
				if (!format.bitDepthText.isEmpty())
					depths.append(QStringLiteral("%1: %2").arg(QChar(code)).arg(format.bitDepthText));
				if (!format.sampleFormatText.isEmpty())
					formats.append(QStringLiteral("%1: %2").arg(QChar(code)).arg(format.sampleFormatText));
			}
			if (!depths.isEmpty())
				facts.bitDepthText = compactComponentText(depths);
			if (!formats.isEmpty())
				facts.sampleFormatText = compactComponentText(formats);
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
		legacyNumericFormat(facts, file);
		facts.alphaChannelBitDepth = mdbAlphaDepth(file, facts);
		return resolvedCompressionNames(facts, audioCompressionName(file.compression.audioEncoding));
	}

	CompressionNames compressionNamesFor(const OmfFileInformation &file)
	{
		auto facts = legacyCompressionFacts(file);
		legacyNumericFormat(facts, file);
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
		mxfNumericFormat(facts, file);
		facts.alphaChannelBitDepth = mxfDnxAlphaDepth(file, facts);
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
