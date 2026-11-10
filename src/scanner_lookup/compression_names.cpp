#include "compression_names.h"
#include "dnx_names.h"

#include <QHash>
#include <QPair>
#include <QString>
#include <algorithm>
#include <array>

namespace Bella::Lookup
{
	namespace
	{
		// MARK: - Compression identifiers

		struct Compression
		{
			const char *displayName;
			const char *codingLabelHex;
		};

		constexpr std::array<Compression, 99> kCompressionCodingNames{{
			// AC-3.
			{"AC-3", "060e2b34040101010402020203020100"},

			// ACES.
			{"ACES 2065 MXF", "060e2b340401010d0401020203040100"},
			{"ACES 2065 MXF", "060e2b340401010d0401020203040200"},

			// AES3.
			{"AES3", "060e2b340401010a0402020101000000"},

			// AJA Xena.
			{"AJA Xena v210", "060e2b340401010a0401020101020201"},

			// Apple Lossless.
			{"Apple Lossless", "060e2b34040101030e04411001010000"},

			// Apple ProRes.
			{"Apple ProRes 422", "060e2b34040101010e04020102110300"},
			{"Apple ProRes 422", "060e2b340401010d0401020203060300"},
			{"Apple ProRes 422 HQ", "060e2b34040101010e04020102110400"},
			{"Apple ProRes 422 HQ", "060e2b340401010d0401020203060400"},
			{"Apple ProRes 422 LT", "060e2b34040101010e04020102110200"},
			{"Apple ProRes 422 LT", "060e2b340401010d0401020203060200"},
			{"Apple ProRes 422 Proxy", "060e2b34040101010e04020102110100"},
			{"Apple ProRes 422 Proxy", "060e2b340401010d0401020203060100"},
			{"Apple ProRes 4444", "060e2b34040101010e04020102110500"},
			{"Apple ProRes 4444", "060e2b340401010d0401020203060500"},
			{"Apple ProRes 4444 XQ", "060e2b34040101010e04020102110600"},
			{"Apple ProRes 4444 XQ", "060e2b340401010d0401020203060600"},
			{"ProRes RAW", "060e2b34040101030e04410202010000"},
			{"ProRes RAW HQ", "060e2b34040101030e04410202020000"},

			// AVC Intra, XAVC, AVC Intra SD, AVC Long-GOP.
			// Intra SD needs bitrate & raster to determine a profile.
			{"AVC Intra", "060e2b340401010a0401020201323001"},
			{"AVC Intra", "060e2b340401010a0401020201323101"},
			{"AVC Intra", "060e2b340401010a0401020201323102"},
			{"AVC Intra", "060e2b340401010a0401020201323103"},
			{"AVC Intra", "060e2b340401010a0401020201323104"},
			{"AVC Intra", "060e2b340401010a0401020201323108"},
			{"AVC Intra", "060e2b340401010a0401020201323109"},
			{"AVC Intra", "060e2b340401010d0401020201323201"},
			{"AVC Intra", "060e2b340401010d0401020201323202"},
			{"AVC Intra", "060e2b340401010d0401020201323203"},
			{"AVC Intra", "060e2b340401010d0401020201323204"},
			{"AVC Intra", "060e2b340401010d0401020201325001"},
			{"AVC Intra SD", "060e2b34040101010e04020102030105"},
			{"AVC Long-GOP Baseline", "060e2b340401010d0401020201311001"},
			{"AVC Long-GOP Constrained Baseline", "060e2b340401010d0401020201311101"},
			{"AVC Long-GOP Extended", "060e2b340401010d0401020201313001"},
			{"AVC Long-GOP High", "060e2b340401010d0401020201314001"},
			{"AVC Long-GOP High10", "060e2b340401010d0401020201315001"},
			{"AVC Long-GOP High422", "060e2b340401010d0401020201316001"},
			{"AVC Long-GOP Main", "060e2b340401010d0401020201312001"},

			// DPX.
			{"DPX", "060e2b34040101030e04410106010000"},

			// DV.
			{"DV 720 50p", "060e2b34040101010401020202020800"},
			{"DV 720 60p", "060e2b34040101010401020202020700"},
			{"DV 1080 50i", "060e2b34040101010401020202020600"},
			{"DV 1080 60i", "060e2b34040101010401020202020500"},
			{"DV NTSC 25Mbps 4:1:1", "060e2b34040101010401020202020100"},
			{"DV NTSC 50Mbps 4:2:2", "060e2b34040101010401020202020300"},
			{"DV PAL 25Mbps 4:1:1", "060e2b34040101010401020202020200"},
			{"DV PAL 50Mbps 4:2:2", "060e2b34040101010401020202020400"},
			{"IEC-DV NTSC 25Mbps 4:1:1", "060e2b34040101010401020202010100"},
			{"IEC-DV NTSC 25Mbps 4:1:1", "060e2b34040101010401020202010300"},
			{"IEC-DV PAL 25Mbps 4:2:0", "060e2b34040101010401020202010200"},
			{"IEC-DV PAL 25Mbps 4:2:0", "060e2b34040101010401020202010400"},

			// H263.
			{"H.263", "060e2b34040101030e0441010a010100"},

			// H265.
			{"H.265/HEVC Main 4:4:4 10 Intra Profile", "060e2b340401010d0401020201462001"},
			{"H.265/HEVC Main 4:4:4 10 Profile", "060e2b340401010d0401020201432001"},
			{"H.265/HEVC Main 4:4:4 12 Intra Profile", "060e2b340401010d0401020201463001"},
			{"H.265/HEVC Main 4:4:4 12 Profile", "060e2b340401010d0401020201433001"},
			{"H.265/HEVC Main 4:4:4 Intra Profile", "060e2b340401010d0401020201461001"},
			{"H.265/HEVC Main 4:4:4 Profile", "060e2b340401010d0401020201431001"},
			{"H.265/HEVC Main 10 Profile", "060e2b340401010d0401020201412001"},
			{"H.265/HEVC Main 12 Intra Profile", "060e2b340401010d0401020201443001"},
			{"H.265/HEVC Main 12 Profile", "060e2b340401010d0401020201413001"},

			// JPEG. JFIF, Motion-JPEG, and JPEG2000 are distinct formats.
			{"JFIF 8:1m", "060e2b34040101010e04020102010501"},
			{"JFIF 8:1m", "060e2b34040101010e04020102010502"},
			{"JFIF 10:1m", "060e2b34040101010e04020102010401"},
			{"JFIF 10:1m", "060e2b34040101010e04020102010402"},
			{"JFIF 15:1s", "060e2b34040101010e04020102010201"},
			{"JFIF 15:1s", "060e2b34040101010e04020102010202"},
			{"JFIF 20:1", "060e2b34040101010e04020102010101"},
			{"JFIF 20:1", "060e2b34040101010e04020102010102"},
			{"JFIF 28:1", "060e2b34040101010e04020102010303"},
			{"JFIF 28:1", "060e2b34040101010e04020102010304"},
			{"JPEG2000", "060e2b34040101070401020203010100"},
			{"JPEG2000", "060e2b34040101090401020203010104"},
			{"JPEG2000", "060e2b340401010d040102020301020a"},
			{"JPEG2000", "060e2b340401010d0401020203010312"},
			{"JPEG Still Image", "060e2b34040101010e04020102050100"},
			{"Motion-JPEG", "060e2b34040101010e04020102050201"},

			// MPEG-2, D-10/IMX, and XDCAM.
			{"MP2", "060e2b34040101010402020203020500"},
			{"MP2", "060e2b340401010d0402020204010200"},
			{"MP3", "060e2b340401010d0402020204010300"},
			{"MP3", "060e2b340401010d0402020204020300"},
			{"MPEG-2 422P@HL I-Frame", "060e2b34040101030401020201040200"},
			{"SMPTE D-10/IMX 30Mbps 525x59.94I", "060e2b34040101010401020201020106"},
			{"SMPTE D-10/IMX 30Mbps 625x50I", "060e2b34040101010401020201020105"},
			{"SMPTE D-10/IMX 40Mbps 525x59.94I", "060e2b34040101010401020201020104"},
			{"SMPTE D-10/IMX 40Mbps 625x50I", "060e2b34040101010401020201020103"},
			{"SMPTE D-10/IMX 50Mbps 525x59.94I", "060e2b34040101010401020201020102"},
			{"SMPTE D-10/IMX 50Mbps 625x50I", "060e2b34040101010401020201020101"},
			{"XDCAM", "060e2b34040101030401020201030300"},
			{"XDCAM HD 50", "060e2b34040101030401020201040300"},

			// OpenEXR.
			{"OpenEXR", "060e2b34040101030e04410103010000"},

			// PNG.
			{"PNG", "060e2b34040101030e04410105010000"},

			// RGB.
			{"1:1 RGB 10bit", "060e2b34040101010e04020102100000"},

			// RLE Alpha.
			{"RLE Alpha 8bit", "060e2b34040101010e04020102090500"},
			{"RLE Alpha 10bit", "060e2b34040101010e04020102090600"},

			// TGA.
			{"TGA", "060e2b34040101030e04410107010000"},

			// Tiff.
			{"Tiff", "060e2b34040101030e04410104010000"},
		}};

		QString compressionNameFromLabel(const QByteArray &label)
		{
			static const QHash<QByteArray, QString> kCompressionNameByLabel = []
			{
				QHash<QByteArray, QString> result;
				result.reserve(qsizetype(kCompressionCodingNames.size()));
				for (const auto &row : kCompressionCodingNames)
					result.insert(QByteArray::fromHex(row.codingLabelHex), QString::fromLatin1(row.displayName));
				return result;
			}();
			return kCompressionNameByLabel.value(label);
		}

		bool isUncompressedCodingLabel(const QByteArray &label)
		{
			static const auto kCompressionLabelsNeedingDetails = std::array{
				QByteArray::fromHex("060e2b34040101010401020100000000"),
				QByteArray::fromHex("060e2b34040101010e04020101010000"),
				QByteArray::fromHex("060e2b34040101010e04020101020000"),
				QByteArray::fromHex("060e2b34040101010e04030101030100"),
				QByteArray::fromHex("060e2b34040101010e04030101030200")};
			return std::find(kCompressionLabelsNeedingDetails.begin(), kCompressionLabelsNeedingDetails.end(), label) != kCompressionLabelsNeedingDetails.end();
		}

		// MARK: - Legacy compression identifiers

		struct LegacyCompression
		{
			const char *fallbackDisplayName;
			const char *descriptorClass;
			const char *legacyCompressionIdentifier;
			qint64 legacyResolutionId;
			QByteArray firstAcceptedCodingLabel;
			QByteArray secondAcceptedCodingLabel;
		};
		// Fallback identifiers from OMF and MDB records.
		const std::array<LegacyCompression, 15> kLegacyCompression{{
			// Avid Packed.
			{"Avid Packed", "CDCI", "AUNC", 151, QByteArray::fromHex("060e2b34040101010401020100000000"), {}},
			{"Avid Packed", "CDCI", "AUNC", 152, QByteArray::fromHex("060e2b34040101010401020100000000"), {}},
			{"Avid Packed", "CDCI", "MXF1", 151, QByteArray::fromHex("060e2b34040101010401020100000000"), {}},
			{"Avid Packed", "CDCI", "MXF1", 152, QByteArray::fromHex("060e2b34040101010401020100000000"), {}},

			// DV.
			{"DV", "CDCI", "DV/C", 140, QByteArray::fromHex("060e2b34040101010401020202020100"), QByteArray::fromHex("060e2b34040101010401020202020200")},
			{"DV", "CDCI", "DV/C", 141, QByteArray::fromHex("060e2b34040101010401020202010200"), {}},
			{"DV", "CDCI", "DV/C", 142, QByteArray::fromHex("060e2b34040101010401020202020300"), QByteArray::fromHex("060e2b34040101010401020202020400")},
			{"DV", "CDCI", "DV/C", 143, QByteArray::fromHex("060e2b34040101010401020202020100"), {}},
			{"DV", "CDCI", "DV/C", 144, QByteArray::fromHex("060e2b34040101010401020202010200"), {}},

			// JPEG / JFIF.
			{"JFIF 8:1m", "JPED", "JFIF", 112, QByteArray::fromHex("060e2b34040101010e04020102010501"), QByteArray::fromHex("060e2b34040101010e04020102010502")},
			{"JFIF 10:1m", "JPED", "JFIF", 110, QByteArray::fromHex("060e2b34040101010e04020102010401"), QByteArray::fromHex("060e2b34040101010e04020102010402")},
			{"JFIF 15:1s", "JPED", "JFIF", 78, QByteArray::fromHex("060e2b34040101010e04020102010201"), QByteArray::fromHex("060e2b34040101010e04020102010202")},
			{"JFIF 20:1", "JPED", "JFIF", 82, QByteArray::fromHex("060e2b34040101010e04020102010101"), QByteArray::fromHex("060e2b34040101010e04020102010102")},
			{"JFIF 28:1", "JPED", "JFIF", 104, QByteArray::fromHex("060e2b34040101010e04020102010303"), QByteArray::fromHex("060e2b34040101010e04020102010304")},

			// MPEG / D-10.
			{"MPEG-2", "MPGI", "MPG2", 160, QByteArray::fromHex("060e2b34040101010401020201020101"), QByteArray::fromHex("060e2b34040101010401020201020102")},
		}};

		QString refinedLegacyCompressionName(const LegacyCompression &row, const CompressionFacts &facts)
		{
			switch (row.legacyResolutionId)
			{
			case 140:
			case 142:
			{
				// IDs 140 and 142 are shared by PAL and NTSC. Check the raster and
				// rate before adding either standard to the name.
				const bool pal = facts.rasterSize == QPair<qint64, qint64>{720, 576} && facts.namingRate.sameRate({25, 1});
				const bool ntsc = facts.rasterSize.first == 720 &&
								  (facts.rasterSize.second == 480 || facts.rasterSize.second == 486) &&
								  (facts.namingRate.sameRate({30000, 1001}) || facts.namingRate.sameRate({2997, 100}));
				if (pal || ntsc)
					return QStringLiteral("DV %1 %2Mbps %3")
						.arg(pal ? QLatin1String("PAL") : QLatin1String("NTSC"))
						.arg(row.legacyResolutionId == 140 ? 25 : 50)
						.arg(row.legacyResolutionId == 140 ? QLatin1String("4:1:1") : QLatin1String("4:2:2"));
				break;
			}
			case 141:
			case 144:
				if (facts.rasterSize == QPair<qint64, qint64>{720, 576} &&
					(facts.namingRate.sameRate({25, 1}) || (facts.frameLayout == 0 && facts.namingRate.sameRate({24, 1}))))
					return QStringLiteral("IEC-DV PAL 25Mbps 4:2:0");
				break;
			case 143:
				if (facts.frameLayout == 0 && facts.rasterSize.first == 720 &&
					(facts.rasterSize.second == 480 || facts.rasterSize.second == 486) &&
					(facts.namingRate.sameRate({24, 1}) || facts.namingRate.sameRate({24000, 1001})))
					return QStringLiteral("DV NTSC 25Mbps 4:1:1");
				break;
			case 151:
			case 152:
				if (facts.horizontalSubsampling == 2 && facts.verticalSubsampling == 1 && (facts.rawComponentDepth == 8 || facts.rawComponentDepth == 10))
					return QStringLiteral("1:1 YCbCr %1bit").arg(*facts.rawComponentDepth);
				break;
			case 160:
				if (facts.rasterSize.first == 720)
				{
					if (facts.rasterSize.second == 608 && facts.namingRate.sameRate({25, 1}))
						return QStringLiteral("SMPTE D-10/IMX 50Mbps 625x50I");
					if (facts.rasterSize.second == 512 &&
						(facts.namingRate.sameRate({30000, 1001}) || facts.namingRate.sameRate({2997, 100})))
						return QStringLiteral("SMPTE D-10/IMX 50Mbps 525x59.94I");
				}
				break;
			default:
				break;
			}
			return QString::fromLatin1(row.fallbackDisplayName);
		}

		QString legacyCompressionName(const CompressionFacts &facts)
		{
			for (const auto &row : kLegacyCompression)
			{
				if (facts.legacyResolutionId != row.legacyResolutionId || facts.legacyCompressionIdentifier != row.legacyCompressionIdentifier)
					continue;
				const bool matchesDescriptor = facts.descriptorClass == QLatin1String(row.descriptorClass) ||
											   (facts.descriptorClass == QLatin1String("CDCI") && QLatin1String(row.descriptorClass) == QLatin1String("JPED"));
				const bool matchesCoding = (facts.isCodingPropertyAbsent && facts.codingLabel.isEmpty()) ||
										   facts.codingLabel == row.firstAcceptedCodingLabel ||
										   (!row.secondAcceptedCodingLabel.isEmpty() && facts.codingLabel == row.secondAcceptedCodingLabel);
				if (matchesDescriptor && matchesCoding)
					return refinedLegacyCompressionName(row, facts);
			}
			return {};
		}

		// MARK: - Names from component details

		QString uncompressedName(const CompressionFacts &facts)
		{
			// A generic uncompressed label doesn't tell us the components.
			// Use the component facts the reader has already decoded.
			if (facts.alphaChannelBitDepth == 8 || facts.alphaChannelBitDepth == 10)
				return QStringLiteral("1:1 Alpha %1bit").arg(*facts.alphaChannelBitDepth);
			if ((facts.descriptorClass == QLatin1String("CDCI") ||
				 facts.descriptorClass == QLatin1String("CDCIEssenceDescriptor")) &&
				facts.horizontalSubsampling == 2 && facts.verticalSubsampling == 1 && (facts.rawComponentDepth == 8 || facts.rawComponentDepth == 10))
				return QStringLiteral("1:1 YCbCr %1bit").arg(*facts.rawComponentDepth);
			return {};
		}
	}

	// MARK: - Compression lookup

	CompressionNames compressionNames(const CompressionFacts &facts)
	{
		auto result = dnxNames(facts);
		if (!result.compressionName.isEmpty())
			return result;

		result.compressionName = compressionNameFromLabel(facts.codingLabel);
		if (!result.compressionName.isEmpty())
			return result;

		if (isUncompressedCodingLabel(facts.codingLabel))
		{
			result.compressionName = uncompressedName(facts);
			if (!result.compressionName.isEmpty())
				return result;
		}

		result.compressionName = legacyCompressionName(facts);
		if (!result.compressionName.isEmpty())
		{
			result.usedLegacyCompressionIdentifiers = true;
			return result;
		}
		if (!facts.isCodingPropertyAbsent || !facts.codingLabel.isEmpty())
			return result;

		if (facts.descriptorClass == QLatin1String("RGBA") &&
			facts.legacyCompressionIdentifier == "NONE" && facts.alphaChannelBitDepth.value_or(0) > 0)
		{
			result.compressionName = uncompressedName(facts);
			if (result.compressionName.isEmpty())
				result.compressionName = QStringLiteral("Uncompressed alpha");
			result.usedLegacyCompressionIdentifiers = true;
		}
		else if (facts.descriptorClass == QLatin1String("PCMA") || facts.descriptorClass == QLatin1String("WAVE"))
			result.compressionName = QStringLiteral("PCM");
		return result;
	}
}
