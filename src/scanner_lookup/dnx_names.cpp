#include "dnx_names.h"

#include <algorithm>
#include <array>

namespace Bella::Lookup
{
	namespace
	{
		struct DnxProfile
		{
			const char *profileTier;
			bool isDnxHr;
			int rasterWidth;
			int rasterHeight;
			int frameLayout;
			QByteArray codingLabel;
		};

		// VC-3 profile labels from ST 2019-1:2016 and Avid's 2012/2026 white papers.
		// These identify the profile without relying on Avid's ResolutionID.
		// HD raster and layout constrain numbered aliases. Layout 0 is progressive; 1 is interlaced.
		static const std::array<DnxProfile, 16> kDnxProfiles{{
			// Level, HR, width, height, layout, coding label.
			// DNxHD
			{"444", false, 1920, 1080, 0, QByteArray::fromHex("060e2b340401010d0401020271160000")},
			{"HQ", false, 1920, 1080, 0, QByteArray::fromHex("060e2b340401010a0401020271040000")},
			{"HQ", false, 1920, 1080, 1, QByteArray::fromHex("060e2b340401010a0401020271090000")},
			{"HQ", false, 1280, 720, 0, QByteArray::fromHex("060e2b340401010a0401020271110000")},
			{"HQX", false, 1920, 1080, 0, QByteArray::fromHex("060e2b340401010a0401020271010000")},
			{"HQX", false, 1920, 1080, 1, QByteArray::fromHex("060e2b340401010a0401020271070000")},
			{"HQX", false, 1280, 720, 0, QByteArray::fromHex("060e2b340401010a0401020271100000")},
			{"LB", false, 1920, 1080, 0, QByteArray::fromHex("060e2b340401010a0401020271130000")},
			{"SQ", false, 1920, 1080, 0, QByteArray::fromHex("060e2b340401010a0401020271030000")},
			{"SQ", false, 1920, 1080, 1, QByteArray::fromHex("060e2b340401010a0401020271080000")},
			{"SQ", false, 1280, 720, 0, QByteArray::fromHex("060e2b340401010a0401020271120000")},

			// DNxHR uses the file's raster and layout, with no numbered HD alias.
			{"444", true, 0, 0, -1, QByteArray::fromHex("060e2b340401010d0401020271240000")},
			{"HQ", true, 0, 0, -1, QByteArray::fromHex("060e2b340401010d0401020271260000")},
			{"HQX", true, 0, 0, -1, QByteArray::fromHex("060e2b340401010d0401020271250000")},
			{"LB", true, 0, 0, -1, QByteArray::fromHex("060e2b340401010d0401020271280000")},
			{"SQ", true, 0, 0, -1, QByteArray::fromHex("060e2b340401010d0401020271270000")},
		}};

		struct DnxOperatingPoint
		{
			int rasterWidth;
			int rasterHeight;
			int frameLayout;
			MediaRate frameRate;
			const char *profile444Suffix;
			const char *hqSuffix;
			const char *hqxSuffix;
			const char *lbSuffix;
			const char *sqSuffix;
		};

		// Historical suffixes from Avid DNxHD Technology (2012), pp. 9–10, not measured bitrates.
		// An empty suffix means there's no numbered alias at that operating point.
		// Thin rasters don't get full-raster aliases. Interlaced 50/59.94 uses
		// frame rates of 25 and 30000/1001.
		constexpr std::array<DnxOperatingPoint, 14> kDnxOperatingPoints{{
			// Width, height, layout, frame rate, 444, HQ, HQX, LB, SQ.

			// 720-line progressive.
			{1280, 720, 0, {24000, 1001}, "", "90", "90x", "", "60"},
			{1280, 720, 0, {25, 1}, "", "90", "90x", "", "60"},
			{1280, 720, 0, {30000, 1001}, "", "110", "110x", "", "75"},
			{1280, 720, 0, {50, 1}, "", "175", "175x", "", "115"},
			{1280, 720, 0, {60000, 1001}, "", "220", "220x", "", "145"},

			// 1080-line progressive.
			{1920, 1080, 0, {24000, 1001}, "350x", "175", "175x", "36", "115"},
			{1920, 1080, 0, {24, 1}, "350x", "175", "175x", "36", "115"},
			{1920, 1080, 0, {25, 1}, "365x", "185", "185x", "36", "120"},
			{1920, 1080, 0, {30000, 1001}, "440x", "220", "220x", "45", "145"},
			{1920, 1080, 0, {50, 1}, "", "365", "365x", "75", "240"},
			{1920, 1080, 0, {60000, 1001}, "", "440", "440x", "90", "290"},
			{1920, 1080, 0, {60, 1}, "", "440", "440x", "90", "290"},

			// 1080-line interlaced.
			{1920, 1080, 1, {25, 1}, "", "185", "185x", "", "120"},
			{1920, 1080, 1, {30000, 1001}, "", "220", "220x", "", "145"},
		}};

		QString numberedDnxName(const DnxProfile &profile, const CompressionFacts &facts)
		{
			const auto level = QLatin1String(profile.profileTier);
			const int expectedDepth = level == QLatin1String("HQX") || level == QLatin1String("444") ? 10 : 8;
			const int expectedHorizontal = level == QLatin1String("444") ? 1 : 2;
			if (profile.isDnxHr || !facts.frameLayout || facts.rawComponentDepth != expectedDepth ||
				facts.horizontalSubsampling != expectedHorizontal || facts.verticalSubsampling != 1 ||
				profile.frameLayout != *facts.frameLayout || profile.rasterWidth != facts.rasterSize.first ||
				profile.rasterHeight != facts.rasterSize.second)
				return {};
			for (const auto &point : kDnxOperatingPoints)
			{
				if (facts.rasterSize.first != point.rasterWidth || facts.rasterSize.second != point.rasterHeight ||
					*facts.frameLayout != point.frameLayout || !facts.namingRate.sameRate(point.frameRate))
					continue;
				const char *reallyOldDnxSuffix = point.profile444Suffix;
				if (level == QLatin1String("LB"))
					reallyOldDnxSuffix = point.lbSuffix;
				else if (level == QLatin1String("SQ"))
					reallyOldDnxSuffix = point.sqSuffix;
				else if (level == QLatin1String("HQ"))
					reallyOldDnxSuffix = point.hqSuffix;
				else if (level == QLatin1String("HQX"))
					reallyOldDnxSuffix = point.hqxSuffix;
				return reallyOldDnxSuffix[0] ? QStringLiteral("DNxHD %1").arg(QLatin1String(reallyOldDnxSuffix)) : QString{};
			}
			return {};
		}

		struct DnxCompression
		{
			const char *displayName;
			QByteArray codingLabel;
		};

		// Additional exact names from Avid's codec definitions.
		static const std::array<DnxCompression, 7> kDnxCompression{{
			// Display name, coding label.
			{"DNx HQ", QByteArray::fromHex("060e2b34040101030e04410101050200")},
			{"DNx SQ", QByteArray::fromHex("060e2b34040101030e04410101050300")},
			{"DNxStitched AVC-I 4:2:2", QByteArray::fromHex("060e2b34040101030e04410101060100")},
			// These thin-raster DNx profiles don't have numbered aliases.
			{"DNx TR", QByteArray::fromHex("060e2b340401010d04010202710a0000")},
			{"DNx TR", QByteArray::fromHex("060e2b340401010d0401020271180000")},
			{"DNx TR", QByteArray::fromHex("060e2b340401010d0401020271190000")},
			{"DNx TR", QByteArray::fromHex("060e2b340401010d04010202711a0000")},
		}};

		QString dnxUncompressedName(const CompressionFacts &facts)
		{
			static const auto kDnxUncompressedCodingLabel = QByteArray::fromHex("060e2b340401010d0401020203070100");
			static const auto kDnxUncompressedFixedPointCodingLabel = QByteArray::fromHex("060e2b340401010d0401020203070200");
			static const auto kDnxUncompressedLegacyRleCodingLabel = QByteArray::fromHex("060e2b34040101030e04410101030000");
			if (facts.codingLabel == kDnxUncompressedLegacyRleCodingLabel)
				return QStringLiteral("DNxUncompressed");

			// RLE alpha has 8-bit and 16-bit variants. Keep the display name at the family level.
			if (facts.codingLabel == kDnxUncompressedCodingLabel && (facts.alphaChannelBitDepth == 8 || facts.alphaChannelBitDepth == 16))
				return QStringLiteral("DNxUncompressed");

			if (facts.codingLabel != kDnxUncompressedCodingLabel && facts.codingLabel != kDnxUncompressedFixedPointCodingLabel)
				return {};

			QString name = QStringLiteral("DNxUncompressed");
			if (!facts.bitDepthText.isEmpty() && !facts.sampleFormatText.isEmpty())
				name += QStringLiteral(" — %1 %2").arg(facts.bitDepthText, facts.sampleFormatText);
			return name;
		}
	}

	CompressionNames dnxNames(const CompressionFacts &facts)
	{
		CompressionNames result;
		for (const auto &profile : kDnxProfiles)
		{
			if (facts.codingLabel != profile.codingLabel)
				continue;
			result.newDnx = QStringLiteral("DNx %1").arg(QLatin1String(profile.profileTier));
			result.oldDnx = QStringLiteral("DNx%1 %2")
								.arg(profile.isDnxHr ? QLatin1String("HR") : QLatin1String("HD"), QLatin1String(profile.profileTier));
			result.reallyOldDnx = numberedDnxName(profile, facts);
			result.compressionName = result.newDnx;
			if (!result.reallyOldDnx.isEmpty())
				result.compressionName += QStringLiteral(" [%1]").arg(result.reallyOldDnx);
			return result;
		}
		result.compressionName = dnxUncompressedName(facts);
		if (!result.compressionName.isEmpty())
			return result;

		for (const auto &row : kDnxCompression)
		{
			if (facts.codingLabel != row.codingLabel)
				continue;
			result.compressionName = QString::fromLatin1(row.displayName);
			return result;
		}
		return result;
	}

	QByteArray codingLabelForDnxResolution(qint64 resolution)
	{
		struct ResolutionCodingLabel
		{
			qint64 resolutionId;
			QByteArray codingLabel;
		};

		// Only these ResolutionIDs have a verified VC-3 label mapping.
		static const std::array<ResolutionCodingLabel, 11> kDnxResolutionLabels{{
			// ResolutionID, coding label.
			{1235, QByteArray::fromHex("060e2b340401010a0401020271010000")},
			{1237, QByteArray::fromHex("060e2b340401010a0401020271030000")},
			{1238, QByteArray::fromHex("060e2b340401010a0401020271040000")},
			{1241, QByteArray::fromHex("060e2b340401010a0401020271070000")},
			{1242, QByteArray::fromHex("060e2b340401010a0401020271080000")},
			{1243, QByteArray::fromHex("060e2b340401010a0401020271090000")},
			{1244, QByteArray::fromHex("060e2b340401010d04010202710a0000")},
			{1250, QByteArray::fromHex("060e2b340401010a0401020271100000")},
			{1251, QByteArray::fromHex("060e2b340401010a0401020271110000")},
			{1252, QByteArray::fromHex("060e2b340401010a0401020271120000")},
			{1253, QByteArray::fromHex("060e2b340401010a0401020271130000")},
		}};
		for (const auto &row : kDnxResolutionLabels)
			if (row.resolutionId == resolution)
				return row.codingLabel;
		return {};
	}

	// Keep these decimal-rate aliases limited to their listed ResolutionIDs.
	MediaRate dnxRateForResolution(qint64 resolution, MediaRate recorded)
	{
		constexpr std::array<qint64, 7> kNtscResolutionIds{1235, 1237, 1238, 1241, 1242, 1243, 1253};
		constexpr std::array<qint64, 7> kFilmResolutionIds{1235, 1237, 1238, 1250, 1251, 1252, 1253};
		if (recorded.sameRate({2997, 100}) &&
			std::find(kNtscResolutionIds.begin(), kNtscResolutionIds.end(), resolution) != kNtscResolutionIds.end())
			return {30000, 1001};
		if (recorded.sameRate({23976, 1000}) &&
			std::find(kFilmResolutionIds.begin(), kFilmResolutionIds.end(), resolution) != kFilmResolutionIds.end())
			return {24000, 1001};
		return recorded;
	}
}
