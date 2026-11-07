#include "naming.h"

#include "effectcatalogue.h"
#include "mdbparser.h"
#include "mxfparser.h"
#include "mediaengine/compressionnames_p.h"
#include "omfparser.h"

#include <QByteArrayView>
#include <QStringList>
#include <algorithm>
#include <array>
#include <utility>

namespace Bella
{
	namespace
	{
		using Facts = MediaEngine::Detail::CompressionFacts;

		template <std::size_t N>
		QByteArray bytes(const std::array<quint8, N> &value)
		{
			return QByteArray(reinterpret_cast<const char *>(value.data()), qsizetype(N));
		}

		QString descriptor(const std::optional<std::array<quint8, 4>> &value)
		{
			return value ? QString::fromLatin1(bytes(*value)) : QString{};
		}

		template <typename Rate>
		MediaRate legacyRate(const std::optional<Rate> &value)
		{
			return value ? MediaRate{value->words.numerator, value->words.denominator} : MediaRate{};
		}

		QByteArray knownDnxLabel(qint64 resolution)
		{
			// Only these established ResolutionIDs have the verified label mapping.
			constexpr std::array<qint64, 11> known{1235, 1237, 1238, 1241, 1242, 1243, 1244, 1250, 1251, 1252, 1253};
			if (std::find(known.begin(), known.end(), resolution) == known.end())
				return {};
			auto label = QByteArray::fromHex("060e2b340401010a0401020271000000");
			label[13] = char(resolution - 1234);
			if (resolution == 1244)
				label[7] = 0x0d;
			return label;
		}

		MediaRate dnxNamingRate(qint64 resolution, MediaRate recorded)
		{
			constexpr std::array<qint64, 7> ntsc{1235, 1237, 1238, 1241, 1242, 1243, 1253};
			constexpr std::array<qint64, 7> film{1235, 1237, 1238, 1250, 1251, 1252, 1253};
			if (recorded.sameRate({2997, 100}) && std::find(ntsc.begin(), ntsc.end(), resolution) != ntsc.end())
				return {30000, 1001};
			if (recorded.sameRate({23976, 1000}) && std::find(film.begin(), film.end(), resolution) != film.end())
				return {24000, 1001};
			return recorded;
		}

		template <typename File>
		Facts legacyFacts(const File &file)
		{
			Facts facts;
			facts.descriptorClass = descriptor(file.descriptorClass);
			const auto &coding = file.compression;
			using State = decltype(coding.codingState);
			facts.codingAbsent = coding.codingState == State::Absent;
			if (coding.codingState == State::Present)
				facts.codingLabel = bytes(coding.codingLabel);
			else if (facts.codingAbsent && coding.resolutionId)
				facts.codingLabel = knownDnxLabel(*coding.resolutionId);
			if (coding.legacyCompression)
				facts.legacyCompression = bytes(*coding.legacyCompression);
			facts.legacyResolution = coding.resolutionId;
			if (coding.namingGeometry)
				facts.geometry = {coding.namingGeometry->width, coding.namingGeometry->height};
			facts.rate = legacyRate(file.sampleRate);
			if (coding.resolutionId && facts.codingLabel == knownDnxLabel(*coding.resolutionId))
				facts.rate = dnxNamingRate(*coding.resolutionId, facts.rate);
			facts.layout = coding.frameLayout;
			facts.depth = file.bitDepth.rawComponentWidth;
			facts.horizontal = coding.horizontalSubsampling;
			facts.vertical = coding.verticalSubsampling;
			return facts;
		}

		struct NumericFormat
		{
			QString depth;
			QString representation;
		};

		NumericFormat numericFormat(const QByteArray &coding, qint64 raw, bool mxf)
		{
			static const auto standard = QByteArray::fromHex("060e2b340401010d0401020203070100");
			static const auto fixed = QByteArray::fromHex("060e2b340401010d0401020203070200");
			if (coding == standard)
			{
				if (raw == 253)
					return {QStringLiteral("16-bit"), QStringLiteral("Half float")};
				if (raw == 254)
					return {QStringLiteral("32-bit"), QStringLiteral("Float")};
				if (raw == 8 || raw == 10 || raw == 12 || raw == 16)
					return {QStringLiteral("%1-bit").arg(raw), QStringLiteral("Integer")};
			}
			else if (coding == fixed)
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
			if (facts.descriptorClass == QLatin1String("RGBA"))
				return;
			if (file.bitDepth.rawComponentWidth)
			{
				const auto format = numericFormat(facts.codingLabel, *file.bitDepth.rawComponentWidth, false);
				facts.bitDepth = format.depth;
				facts.sampleFormat = format.representation;
			}
		}

		bool oneValue(const MdbComponentArray &value, quint8 expected)
		{
			return (value.count == 1 || (value.count == 2 && value.values[1] == 0)) && value.values[0] == expected;
		}

		std::optional<qint64> legacyAlpha(const MdbFileRecord &file, const Facts &facts)
		{
			if (!facts.codingAbsent || facts.descriptorClass != QLatin1String("RGBA") ||
				facts.legacyCompression != "NONE")
				return std::nullopt;
			const auto &layout = file.bitDepth.pixelLayout;
			const auto &width = file.bitDepth.pixelStructure;
			if (layout && width && oneValue(*layout, 'A') && width->values[0] > 0 && width->values[0] <= 64 &&
				oneValue(*width, width->values[0]))
				return width->values[0];
			return std::nullopt;
		}

		QString mxfDescriptor(const std::optional<std::array<quint8, 16>> &value)
		{
			if (!value || ((*value)[5] != 0x13 && (*value)[5] != 0x53))
				return {};
			auto key = bytes(*value);
			key[5] = 0x53;
			key[7] = 0x01; // Semantic class matching admits the reader's registry-version variation.
			if (key == QByteArray::fromHex("060e2b34025301010d01010101012800"))
				return QStringLiteral("CDCIEssenceDescriptor");
			if (key == QByteArray::fromHex("060e2b34025301010d01010101012900"))
				return QStringLiteral("RGBAEssenceDescriptor");
			return {};
		}

		QString compact(const QStringList &components)
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
				const auto format = numericFormat(facts.codingLabel, *file.bitDepth.rawComponentDepth, true);
				facts.bitDepth = format.depth;
				facts.sampleFormat = format.representation;
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
				const auto format = numericFormat(facts.codingLabel, raw, true);
				if (!format.depth.isEmpty())
					depths.append(QStringLiteral("%1: %2").arg(QChar(code)).arg(format.depth));
				if (!format.representation.isEmpty())
					formats.append(QStringLiteral("%1: %2").arg(QChar(code)).arg(format.representation));
			}
			if (!depths.isEmpty())
				facts.bitDepth = compact(depths);
			if (!formats.isEmpty())
				facts.sampleFormat = compact(formats);
		}

		template <typename Encoding>
		QString audioName(Encoding encoding)
		{
			if (encoding == Encoding::Pcm)
				return QStringLiteral("PCM");
			if (encoding == Encoding::IeeeFloat)
				return QStringLiteral("IEEE float");
			return {};
		}

		CompressionNames resolvedNames(const Facts &facts, const QString &audio = {})
		{
			auto names = MediaEngine::Detail::compressionNames(facts);
			if (names.compression.isEmpty())
				names.compression = audio;
			else if (!audio.isEmpty() && names.compression != audio)
				names.compression = {}; // Equal-source contradictory claims do not choose a winner.
			return {std::move(names.compression), std::move(names.newDnx), std::move(names.oldDnx),
					std::move(names.reallyOldDnx)};
		}
	} // namespace

	CompressionNames compressionFor(const MdbFileRecord &file)
	{
		if (!file.ownDescriptorQualified)
			return {};
		auto facts = legacyFacts(file);
		if (file.sampleRate && file.sampleRate->encoding == MdbRateEncoding::ExactEditRate &&
			facts.codingLabel == QByteArray::fromHex("060e2b34040101070401020203010100"))
		{
			if (facts.rate.sameRate({2997, 100}))
				facts.rate = {30000, 1001};
			else if (facts.rate.sameRate({23976, 1000}))
				facts.rate = {24000, 1001};
		}
		legacyNumericFormat(facts, file);
		facts.alphaDepth = legacyAlpha(file, facts);
		return resolvedNames(facts, audioName(file.compression.audioEncoding));
	}

	CompressionNames compressionFor(const OmfFileInformation &file)
	{
		auto facts = legacyFacts(file);
		legacyNumericFormat(facts, file);
		if (facts.codingAbsent && facts.descriptorClass == QLatin1String("RGBA") && facts.legacyCompression == "NONE")
			facts.alphaDepth = file.compression.alphaDepth;
		return resolvedNames(facts, audioName(file.compression.audioEncoding));
	}

	CompressionNames compressionFor(const MxfFileInformation &file)
	{
		Facts facts;
		facts.descriptorClass = mxfDescriptor(file.descriptorClass);
		const auto &coding = file.compression;
		facts.codingAbsent = coding.codingState == MxfCodingState::Absent;
		if (coding.codingState == MxfCodingState::Present)
			facts.codingLabel = bytes(coding.codingLabel);
		if (coding.namingGeometry)
			facts.geometry = {coding.namingGeometry->width, coding.namingGeometry->height};
		if (file.sampleRate)
			facts.rate = {file.sampleRate->numerator, file.sampleRate->denominator};
		facts.layout = coding.frameLayout;
		facts.depth = file.bitDepth.rawComponentDepth;
		facts.horizontal = coding.horizontalSubsampling;
		facts.vertical = coding.verticalSubsampling;
		mxfNumericFormat(facts, file);
		auto names = resolvedNames(facts);
		if (names.compression.isEmpty() && facts.codingAbsent)
		{
			if (coding.namingFallback == MxfNamingFallback::Pcm)
				names.compression = QStringLiteral("PCM");
			else if (coding.namingFallback == MxfNamingFallback::UncompressedAlpha8)
				names.compression = QStringLiteral("1:1 Alpha 8bit");
		}
		return names;
	}

	CompressionNames compressionFor(const OmfNativeAudioInformation &file)
	{
		return {audioName(file.encoding), {}, {}, {}};
	}

	EffectNames effectNamesFor(bool confirmedPrecompute, const QString &selectedClipName)
	{
		if (!confirmedPrecompute)
			return {};
		auto hit = AvidEffects::lookup(selectedClipName);
		return {hit.name.isEmpty() ? QStringLiteral("unknown") : std::move(hit.name), std::move(hit.category)};
	}
} // namespace Bella
