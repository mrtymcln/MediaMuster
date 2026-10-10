#include "compressionnames_p.h"

#include "dnxnames_p.h"

#include <QHash>
#include <array>

namespace MediaEngine::Detail
{
	namespace
	{
		struct NameRow
		{
			const char *label;
			const char *name;
		};

		// Descriptive names transcribed from Media Composer 26.8's installed
		// SupportingFiles/CodecToolkit_Config/dfm/*.lua. The relevant module is
		// named above each group. ParamExtender contains shorter UI overrides;
		// those overrides deliberately do not replace these descriptions.
		// Only exact identifiers are admitted: no prefix/family guesses.
		constexpr auto kNames = std::array{
			// dv.lua: the coding labels distinguish DV-based and IEC-DV forms.
			NameRow{"060e2b34040101010401020202020100", "DV NTSC 25Mbps 4:1:1"},
			NameRow{"060e2b34040101010401020202020200", "DV PAL 25Mbps 4:1:1"},
			NameRow{"060e2b34040101010401020202020300", "DV NTSC 50Mbps 4:2:2"},
			NameRow{"060e2b34040101010401020202020400", "DV PAL 50Mbps 4:2:2"},
			NameRow{"060e2b34040101010401020202010100", "IEC-DV NTSC 25Mbps 4:1:1"},
			NameRow{"060e2b34040101010401020202010300", "IEC-DV NTSC 25Mbps 4:1:1"},
			NameRow{"060e2b34040101010401020202010200", "IEC-DV PAL 25Mbps 4:2:0"},
			NameRow{"060e2b34040101010401020202010400", "IEC-DV PAL 25Mbps 4:2:0"},
			NameRow{"060e2b34040101010401020202020500", "DV 1080 60i"},
			NameRow{"060e2b34040101010401020202020600", "DV 1080 50i"},
			NameRow{"060e2b34040101010401020202020700", "DV 720 60p"},
			NameRow{"060e2b34040101010401020202020800", "DV 720 50p"},

			// prores.lua: both registered and explicitly supported Avid labels.
			NameRow{"060e2b340401010d0401020203060100", "Apple ProRes 422 Proxy"},
			NameRow{"060e2b340401010d0401020203060200", "Apple ProRes 422 LT"},
			NameRow{"060e2b340401010d0401020203060300", "Apple ProRes 422"},
			NameRow{"060e2b340401010d0401020203060400", "Apple ProRes 422 HQ"},
			NameRow{"060e2b340401010d0401020203060500", "Apple ProRes 4444"},
			NameRow{"060e2b340401010d0401020203060600", "Apple ProRes 4444 XQ"},
			NameRow{"060e2b34040101010e04020102110100", "Apple ProRes 422 Proxy"},
			NameRow{"060e2b34040101010e04020102110200", "Apple ProRes 422 LT"},
			NameRow{"060e2b34040101010e04020102110300", "Apple ProRes 422"},
			NameRow{"060e2b34040101010e04020102110400", "Apple ProRes 422 HQ"},
			NameRow{"060e2b34040101010e04020102110500", "Apple ProRes 4444"},
			NameRow{"060e2b34040101010e04020102110600", "Apple ProRes 4444 XQ"},
			// prores_raw.lua.
			NameRow{"060e2b34040101030e04410202010000", "ProRes RAW"},
			NameRow{"060e2b34040101030e04410202020000", "ProRes RAW HQ"},

			// mjpeg.lua: ratio-bearing labels are already specific; the generic
			// Motion-JPEG label does not establish a JPEG quality setting.
			NameRow{"060e2b34040101010e04020102010101", "JFIF 20:1"},
			NameRow{"060e2b34040101010e04020102010102", "JFIF 20:1"},
			NameRow{"060e2b34040101010e04020102010201", "JFIF 15:1s"},
			NameRow{"060e2b34040101010e04020102010202", "JFIF 15:1s"},
			NameRow{"060e2b34040101010e04020102010303", "JFIF 28:1"},
			NameRow{"060e2b34040101010e04020102010304", "JFIF 28:1"},
			NameRow{"060e2b34040101010e04020102010401", "JFIF 10:1m"},
			NameRow{"060e2b34040101010e04020102010402", "JFIF 10:1m"},
			NameRow{"060e2b34040101010e04020102010501", "JFIF 8:1m"},
			NameRow{"060e2b34040101010e04020102010502", "JFIF 8:1m"},
			NameRow{"060e2b34040101010e04020102050100", "JPEG Still Image"},
			NameRow{"060e2b34040101010e04020102050201", "Motion-JPEG"},
			// packer.lua: these specific labels carry their component identity.
			NameRow{"060e2b34040101010e04020102090500", "RLE Alpha 8bit"},
			NameRow{"060e2b34040101010e04020102090600", "RLE Alpha 10bit"},
			NameRow{"060e2b34040101010e04020102100000", "1:1 RGB 10bit"},

			// m2v.lua: D-10 identifies both bitrate and television standard.
			NameRow{"060e2b34040101010401020201020101", "SMPTE D-10/IMX 50Mbps 625x50I"},
			NameRow{"060e2b34040101010401020201020102", "SMPTE D-10/IMX 50Mbps 525x59.94I"},
			NameRow{"060e2b34040101010401020201020103", "SMPTE D-10/IMX 40Mbps 625x50I"},
			NameRow{"060e2b34040101010401020201020104", "SMPTE D-10/IMX 40Mbps 525x59.94I"},
			NameRow{"060e2b34040101010401020201020105", "SMPTE D-10/IMX 30Mbps 625x50I"},
			NameRow{"060e2b34040101010401020201020106", "SMPTE D-10/IMX 30Mbps 525x59.94I"},
			// A label shared by XDCAM operating points cannot establish a bitrate.
			NameRow{"060e2b34040101030401020201030300", "XDCAM"},
			NameRow{"060e2b34040101030401020201040200", "MPEG-2 422P@HL I-Frame"},
			NameRow{"060e2b34040101030401020201040300", "XDCAM HD 50"},

			// avci.lua: AVC-Intra and XAVC can share these labels. Without the
			// extra codec parameters, only Avid's common family is established.
			NameRow{"060e2b340401010a0401020201323101", "AVC Intra"},
			NameRow{"060e2b340401010a0401020201323102", "AVC Intra"},
			NameRow{"060e2b340401010a0401020201323103", "AVC Intra"},
			NameRow{"060e2b340401010a0401020201323104", "AVC Intra"},
			NameRow{"060e2b340401010a0401020201323108", "AVC Intra"},
			NameRow{"060e2b340401010a0401020201323109", "AVC Intra"},
			NameRow{"060e2b340401010d0401020201323201", "AVC Intra"},
			NameRow{"060e2b340401010d0401020201323202", "AVC Intra"},
			NameRow{"060e2b340401010d0401020201323203", "AVC Intra"},
			NameRow{"060e2b340401010d0401020201323204", "AVC Intra"},
			NameRow{"060e2b340401010a0401020201323001", "AVC Intra"},
			NameRow{"060e2b340401010d0401020201325001", "AVC Intra"},
			// avci_sd.lua likewise needs bitrate and raster to name the profile.
			NameRow{"060e2b34040101010e04020102030105", "AVCI-SD"},
			// avc.lua generic profiles, rather than unproved proxy/bitrate names.
			NameRow{"060e2b340401010d0401020201311001", "AVC Long-GOP Baseline"},
			NameRow{"060e2b340401010d0401020201311101", "AVC Long-GOP Constrained Baseline"},
			NameRow{"060e2b340401010d0401020201312001", "AVC Long-GOP Main"},
			NameRow{"060e2b340401010d0401020201313001", "AVC Long-GOP Extended"},
			NameRow{"060e2b340401010d0401020201314001", "AVC Long-GOP High"},
			NameRow{"060e2b340401010d0401020201315001", "AVC Long-GOP High10"},
			NameRow{"060e2b340401010d0401020201316001", "AVC Long-GOP High422"},

			// hevc.lua.
			NameRow{"060e2b340401010d0401020201412001", "H.265/HEVC Main 10 Profile"},
			NameRow{"060e2b340401010d0401020201413001", "H.265/HEVC Main 12 Profile"},
			NameRow{"060e2b340401010d0401020201431001", "H.265/HEVC Main 4:4:4 Profile"},
			NameRow{"060e2b340401010d0401020201432001", "H.265/HEVC Main 4:4:4 10 Profile"},
			NameRow{"060e2b340401010d0401020201433001", "H.265/HEVC Main 4:4:4 12 Profile"},
			NameRow{"060e2b340401010d0401020201443001", "H.265/HEVC Main 12 Intra Profile"},
			NameRow{"060e2b340401010d0401020201461001", "H.265/HEVC Main 4:4:4 Intra Profile"},
			NameRow{"060e2b340401010d0401020201462001", "H.265/HEVC Main 4:4:4 10 Intra Profile"},
			NameRow{"060e2b340401010d0401020201463001", "H.265/HEVC Main 4:4:4 12 Intra Profile"},

			// Other exact Avid codec definitions. Colour/component variants need
			// additional evidence, so these entries retain the supported family.
			NameRow{"060e2b34040101030e04410103010000", "OpenEXR"},
			NameRow{"060e2b34040101030e04410104010000", "Tiff"},
			NameRow{"060e2b34040101030e04410105010000", "PNG"},
			NameRow{"060e2b34040101030e04410106010000", "DPX"},
			NameRow{"060e2b34040101030e04410107010000", "TGA"},
			NameRow{"060e2b34040101030e0441010a010100", "H.263"},
			NameRow{"060e2b340401010d0401020203040100", "ACES 2065 MXF"},
			NameRow{"060e2b340401010d0401020203040200", "ACES 2065 MXF"},
			NameRow{"060e2b340401010a0401020101020201", "AJA Xena v210"},
			// Audio family names; channel/rate/bitrate details stay separate.
			NameRow{"060e2b34040101010402020203020100", "AC-3"},
			NameRow{"060e2b34040101010402020203020500", "MP2"},
			NameRow{"060e2b340401010d0402020204010200", "MP2"},
			NameRow{"060e2b340401010d0402020204010300", "MP3"},
			NameRow{"060e2b340401010d0402020204020300", "MP3"},
			NameRow{"060e2b340401010a0402020101000000", "AES3"},
			NameRow{"060e2b34040101030e04411001010000", "Apple Lossless"},
			// DNx thin-raster profiles are unchanged and have no numbered alias.
			NameRow{"060e2b340401010d04010202710a0000", "Avid DNx TR"},
			NameRow{"060e2b340401010d0401020271180000", "Avid DNx TR"},
			NameRow{"060e2b340401010d0401020271190000", "Avid DNx TR"},
			NameRow{"060e2b340401010d04010202711a0000", "Avid DNx TR"},
			// Existing DNx compatibility spellings remain unchanged in this
			// naming-only work; dnx_uncompressed.lua records these identifiers.
			NameRow{"060e2b34040101030e04410101030000", "DNxRLE Alpha"},
			NameRow{"060e2b34040101030e04410101050200", "Avid DNx HQ"},
			NameRow{"060e2b34040101030e04410101050300", "Avid DNx SQ"},
			NameRow{"060e2b34040101030e04410101060100", "Avid DNxStitched AVC-I 4:2:2"},
		};

		QString labelName(const QByteArray &label)
		{
			static const QHash<QByteArray, QString> names = []
			{
				QHash<QByteArray, QString> result;
				result.reserve(qsizetype(kNames.size()));
				for (const auto &row : kNames)
					result.insert(QByteArray::fromHex(row.label), QString::fromLatin1(row.name));
				return result;
			}();
			return names.value(label);
		}

		bool isLabel(const QByteArray &label, const char *hex)
		{
			return label == QByteArray::fromHex(hex);
		}

		QString legacyName(const CompressionFacts &facts)
		{
			struct Row
			{
				const char *descriptor;
				const char *compression;
				qint64 resolution;
				const char *name;
				const char *coding1;
				const char *coding2;
			};
			// Established by Avid's supplied OMF slates, resolution resources and
			// regenerated MDB. A present unrelated label never permits fallback.
			constexpr auto rows = std::array{
				Row{"JPED", "JFIF", 78, "JFIF 15:1s", "060e2b34040101010e04020102010201", "060e2b34040101010e04020102010202"},
				Row{"JPED", "JFIF", 82, "JFIF 20:1", "060e2b34040101010e04020102010101", "060e2b34040101010e04020102010102"},
				Row{"JPED", "JFIF", 104, "JFIF 28:1", "060e2b34040101010e04020102010303", "060e2b34040101010e04020102010304"},
				Row{"JPED", "JFIF", 110, "JFIF 10:1m", "060e2b34040101010e04020102010401", "060e2b34040101010e04020102010402"},
				Row{"JPED", "JFIF", 112, "JFIF 8:1m", "060e2b34040101010e04020102010501", "060e2b34040101010e04020102010502"},
				Row{"CDCI", "DV/C", 140, "DV", "060e2b34040101010401020202020100", "060e2b34040101010401020202020200"},
				Row{"CDCI", "DV/C", 141, "DV", "060e2b34040101010401020202010200", ""},
				Row{"CDCI", "DV/C", 142, "DV", "060e2b34040101010401020202020300", "060e2b34040101010401020202020400"},
				Row{"CDCI", "DV/C", 143, "DV", "060e2b34040101010401020202020100", ""},
				Row{"CDCI", "DV/C", 144, "DV", "060e2b34040101010401020202010200", ""},
				Row{"CDCI", "AUNC", 151, "Avid Packed", "060e2b34040101010401020100000000", ""},
				Row{"CDCI", "MXF1", 151, "Avid Packed", "060e2b34040101010401020100000000", ""},
				Row{"CDCI", "AUNC", 152, "Avid Packed", "060e2b34040101010401020100000000", ""},
				Row{"CDCI", "MXF1", 152, "Avid Packed", "060e2b34040101010401020100000000", ""},
				Row{"MPGI", "MPG2", 160, "MPEG-2", "060e2b34040101010401020201020101", "060e2b34040101010401020201020102"},
			};
			for (const auto &row : rows)
			{
				const bool descriptor = facts.descriptorClass == QLatin1String(row.descriptor) ||
					(facts.descriptorClass == QLatin1String("CDCI") && QLatin1String(row.descriptor) == QLatin1String("JPED"));
				const bool coding = (facts.codingAbsent && facts.codingLabel.isEmpty()) ||
					isLabel(facts.codingLabel, row.coding1) || (row.coding2[0] && isLabel(facts.codingLabel, row.coding2));
				if (!descriptor || !coding || facts.legacyCompression != row.compression || facts.legacyResolution != row.resolution)
					continue;

				// ID 140/142 alone does not distinguish PAL and NTSC. Only the
				// checked legacy raster/clock combination supplies that extra name.
				if (row.resolution == 140 || row.resolution == 142)
				{
					const bool pal = facts.geometry == QPair<qint64, qint64>{720, 576} && facts.rate.sameRate({25, 1});
					const bool ntsc = facts.geometry.first == 720 &&
						(facts.geometry.second == 480 || facts.geometry.second == 486) &&
						(facts.rate.sameRate({30000, 1001}) || facts.rate.sameRate({2997, 100}));
					if (pal || ntsc)
						return QStringLiteral("DV %1 %2Mbps %3").arg(pal ? QLatin1String("PAL") : QLatin1String("NTSC"))
							.arg(row.resolution == 140 ? 25 : 50)
							.arg(row.resolution == 140 ? QLatin1String("4:1:1") : QLatin1String("4:2:2"));
				}
				if ((row.resolution == 141 || row.resolution == 144) &&
					facts.geometry == QPair<qint64, qint64>{720, 576} &&
					(facts.rate.sameRate({25, 1}) || (facts.layout == 0 && facts.rate.sameRate({24, 1}))))
					return QStringLiteral("IEC-DV PAL 25Mbps 4:2:0");
				if (row.resolution == 143 && facts.layout == 0 && facts.geometry.first == 720 &&
					(facts.geometry.second == 480 || facts.geometry.second == 486) &&
					(facts.rate.sameRate({24, 1}) || facts.rate.sameRate({24000, 1001})))
					return QStringLiteral("DV NTSC 25Mbps 4:1:1");
				if ((row.resolution == 151 || row.resolution == 152) &&
					facts.horizontal == 2 && facts.vertical == 1 && (facts.depth == 8 || facts.depth == 10))
					return QStringLiteral("1:1 YCbCr %1bit").arg(*facts.depth);
				if (row.resolution == 160 && facts.geometry.first == 720)
				{
					if (facts.geometry.second == 608 && facts.rate.sameRate({25, 1}))
						return QStringLiteral("SMPTE D-10/IMX 50Mbps 625x50I");
					if (facts.geometry.second == 512 &&
						(facts.rate.sameRate({30000, 1001}) || facts.rate.sameRate({2997, 100})))
						return QStringLiteral("SMPTE D-10/IMX 50Mbps 525x59.94I");
				}
				return QString::fromLatin1(row.name);
			}
			return {};
		}

		QString uncompressedName(const CompressionFacts &facts)
		{
			// Generic uncompressed labels do not establish RGB, YCbCr or alpha
			// by themselves. Only already decoded component facts permit a name.
			if (facts.alphaDepth == 8 || facts.alphaDepth == 10)
				return QStringLiteral("1:1 Alpha %1bit").arg(*facts.alphaDepth);
			if ((facts.descriptorClass == QLatin1String("CDCI") ||
				 facts.descriptorClass == QLatin1String("CDCIEssenceDescriptor")) &&
				facts.horizontal == 2 && facts.vertical == 1 && (facts.depth == 8 || facts.depth == 10))
				return QStringLiteral("1:1 YCbCr %1bit").arg(*facts.depth);
			return {};
		}

		QString jpeg2000Name(const CompressionFacts &facts)
		{
			// j2k.lua uses this one generic coding label for both SD and HD.
			// Only its checked HD raster/component/layout/clock combinations
			// permit the more specific name; the label alone supplies a family.
			if (isLabel(facts.codingLabel, "060e2b34040101070401020203010100") &&
				facts.depth == 10 && facts.horizontal == 2 && facts.vertical == 1)
			{
				constexpr auto progressiveRates = std::array{MediaRate{25, 1}, MediaRate{24, 1}, MediaRate{30000, 1001}, MediaRate{24000, 1001}};
				constexpr auto interlacedRates = std::array{MediaRate{25, 1}, MediaRate{30000, 1001}};
				constexpr auto hd720Rates = std::array{MediaRate{60000, 1001}, MediaRate{50, 1}, MediaRate{30000, 1001}, MediaRate{25, 1}, MediaRate{24000, 1001}};
				if (facts.geometry == QPair<qint64, qint64>{1920, 1080})
				{
					if (facts.layout == 0)
						for (const auto &rate : progressiveRates)
							if (facts.rate.sameRate(rate))
								return QStringLiteral("J2K HD");
					if (facts.layout == 1)
						for (const auto &rate : interlacedRates)
							if (facts.rate.sameRate(rate))
								return QStringLiteral("J2K HD");
				}
				if (facts.geometry == QPair<qint64, qint64>{1280, 720} && facts.layout == 0)
					for (const auto &rate : hd720Rates)
						if (facts.rate.sameRate(rate))
							return QStringLiteral("J2K HD");
			}
			return QStringLiteral("JPEG2000"); // j2k.lua codec_family.
		}
	}

	CompressionNames compressionNames(const CompressionFacts &facts)
	{
		CompressionNames result;
		for (const auto &profile : dnxProfiles)
		{
			if (!isLabel(facts.codingLabel, profile.label))
				continue;
			result.newDnx = QStringLiteral("Avid DNx %1").arg(QLatin1String(profile.level));
			result.oldDnx = QStringLiteral("DNx%1 %2").arg(profile.hr ? QLatin1String("HR") : QLatin1String("HD"), QLatin1String(profile.level));
			result.reallyOldDnx = reallyOldDnx(profile, facts.geometry, facts.rate, facts.layout, facts.depth, facts.horizontal, facts.vertical);
			result.compression = result.newDnx;
			if (!result.reallyOldDnx.isEmpty())
				result.compression += QStringLiteral(" [%1]").arg(result.reallyOldDnx);
			return result;
		}
		if (isLabel(facts.codingLabel, "060e2b340401010d0401020203070100") ||
			isLabel(facts.codingLabel, "060e2b340401010d0401020203070200"))
		{
			result.compression = QStringLiteral("Avid DNxUncompressed");
			if (!facts.bitDepth.isEmpty() && !facts.sampleFormat.isEmpty())
				result.compression += QStringLiteral(" — %1 %2").arg(facts.bitDepth, facts.sampleFormat);
			return result;
		}

		result.compression = labelName(facts.codingLabel);
		if (!result.compression.isEmpty())
			return result;

		if (isLabel(facts.codingLabel, "060e2b34040101010401020100000000") ||
			isLabel(facts.codingLabel, "060e2b34040101010e04020101010000") ||
			isLabel(facts.codingLabel, "060e2b34040101010e04020101020000") ||
			isLabel(facts.codingLabel, "060e2b34040101010e04030101030100") ||
			isLabel(facts.codingLabel, "060e2b34040101010e04030101030200"))
		{
			result.compression = uncompressedName(facts);
			if (!result.compression.isEmpty())
				return result;
		}

		// The generic J2K label is shared by SD and HD. A family name remains
		// useful when the component/raster facts do not establish a preset.
		if (isLabel(facts.codingLabel, "060e2b34040101070401020203010100") ||
			isLabel(facts.codingLabel, "060e2b340401010d040102020301020a") ||
			isLabel(facts.codingLabel, "060e2b340401010d0401020203010312") ||
			isLabel(facts.codingLabel, "060e2b34040101090401020203010104"))
		{
			result.compression = jpeg2000Name(facts);
			return result;
		}

		result.compression = legacyName(facts);
		if (!result.compression.isEmpty())
		{
			result.legacyIdentifiersUsed = true;
			return result;
		}
		if (facts.codingAbsent && facts.codingLabel.isEmpty())
		{
			if (facts.descriptorClass == QLatin1String("RGBA") && facts.legacyCompression == "NONE" && facts.alphaDepth)
			{
				result.compression = uncompressedName(facts);
				if (result.compression.isEmpty() && *facts.alphaDepth > 0)
					result.compression = QStringLiteral("Uncompressed alpha"); // Explicit NONE, not inferred from alpha alone.
				result.legacyIdentifiersUsed = !result.compression.isEmpty();
			}
			else if (facts.descriptorClass == QLatin1String("PCMA") || facts.descriptorClass == QLatin1String("WAVE"))
				result.compression = QStringLiteral("PCM");
		}
		return result;
	}
}
