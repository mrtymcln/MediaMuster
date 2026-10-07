#pragma once

// Verified DNx profile names and exact historical operating points, shared by
// MXF and OMF/MDB projections. A missing match stays unnamed; never round a rate
// or substitute a nearby raster to manufacture a historical alias.

#include "mediaduration.h"
#include <QPair>
#include <QString>
#include <optional>

namespace Canon::Detail
{
	struct DnxProfile
	{
		const char *label;
		const char *level;
		bool hr;
		int width;
		int height;
		int layout;
	};

	// Exact registered VC-3 labels; profile identities are distinct from an
	// application's numeric ResolutionID. See the Canon DNx evidence document,
	// ST 2019-1:2016 Annex C/H and Avid's 2012 and 2026 white papers.
	inline constexpr DnxProfile dnxProfiles[] = {
		{"060e2b340401010a0401020271010000", "HQX", false, 1920, 1080, 0},
		{"060e2b340401010a0401020271070000", "HQX", false, 1920, 1080, 1},
		{"060e2b340401010a0401020271100000", "HQX", false, 1280, 720, 0},
		{"060e2b340401010a0401020271040000", "HQ", false, 1920, 1080, 0},
		{"060e2b340401010a0401020271090000", "HQ", false, 1920, 1080, 1},
		{"060e2b340401010a0401020271110000", "HQ", false, 1280, 720, 0},
		{"060e2b340401010a0401020271030000", "SQ", false, 1920, 1080, 0},
		{"060e2b340401010a0401020271080000", "SQ", false, 1920, 1080, 1},
		{"060e2b340401010a0401020271120000", "SQ", false, 1280, 720, 0},
		{"060e2b340401010a0401020271130000", "LB", false, 1920, 1080, 0},
		{"060e2b340401010d0401020271160000", "444", false, 1920, 1080, 0},
		{"060e2b340401010d0401020271250000", "HQX", true, 0, 0, -1},
		{"060e2b340401010d0401020271260000", "HQ", true, 0, 0, -1},
		{"060e2b340401010d0401020271270000", "SQ", true, 0, 0, -1},
		{"060e2b340401010d0401020271280000", "LB", true, 0, 0, -1},
		{"060e2b340401010d0401020271240000", "444", true, 0, 0, -1},
		{"060e2b34040101010d01030102060301", "LB", false, 0, 0, -1},
		{"060e2b34040101010d01030102060101", "SQ", false, 0, 0, -1},
		{"060e2b34040101010d01030102060201", "HQ", false, 0, 0, -1},
		{"060e2b34040101010d01030102060202", "HQX", false, 0, 0, -1},
		{"060e2b34040101010d01030102110101", "LB", true, 0, 0, -1},
		{"060e2b34040101010d01030102110201", "SQ", true, 0, 0, -1},
		{"060e2b34040101010d01030102110301", "HQ", true, 0, 0, -1},
		{"060e2b34040101010d01030102110401", "HQX", true, 0, 0, -1},
		{"060e2b34040101010d01030102110501", "444", true, 0, 0, -1}};

	struct DnxOperatingPoint
	{
		int width;
		int height;
		int layout;
		MediaRate rate;
		const char *lb;
		const char *sq;
		const char *hq;
		const char *hqx;
		const char *rgb444;
	};

	// Hand-transcribed and visually checked against the user-supplied 2012
	// Avid DNxHD Technology white paper, pp. 9–10, "family of mastering
	// resolutions". These are the Resolution names, not the Mbps values.
	// The footnoted 1440/960 thin-raster rows are deliberately not full-raster
	// aliases. 1080i/50 and /59.94 have frame clocks 25 and 30000/1001.
	inline constexpr DnxOperatingPoint dnxOperatingPoints[] = {
		{1920, 1080, 0, {60, 1}, "90", "290", "440", "440x", ""},
		{1920, 1080, 0, {60000, 1001}, "90", "290", "440", "440x", ""},
		{1920, 1080, 0, {50, 1}, "75", "240", "365", "365x", ""},
		{1920, 1080, 1, {30000, 1001}, "", "145", "220", "220x", ""},
		{1920, 1080, 1, {25, 1}, "", "120", "185", "185x", ""},
		{1920, 1080, 0, {25, 1}, "36", "120", "185", "185x", "365x"},
		{1920, 1080, 0, {24, 1}, "36", "115", "175", "175x", "350x"},
		{1920, 1080, 0, {24000, 1001}, "36", "115", "175", "175x", "350x"},
		{1920, 1080, 0, {30000, 1001}, "45", "145", "220", "220x", "440x"},
		{1280, 720, 0, {60000, 1001}, "", "145", "220", "220x", ""},
		{1280, 720, 0, {50, 1}, "", "115", "175", "175x", ""},
		{1280, 720, 0, {30000, 1001}, "", "75", "110", "110x", ""},
		{1280, 720, 0, {25, 1}, "", "60", "90", "90x", ""},
		{1280, 720, 0, {24000, 1001}, "", "60", "90", "90x", ""}};

	inline QString reallyOldDnx(const DnxProfile &profile, QPair<qint64, qint64> geometry,
								MediaRate rate, std::optional<qint64> layout, std::optional<qint64> depth,
								std::optional<qint64> horizontal, std::optional<qint64> vertical)
	{
		const auto level = QLatin1String(profile.level);
		const int expectedDepth = level == QLatin1String("HQX") || level == QLatin1String("444") ? 10 : 8;
		const int expectedHorizontal = level == QLatin1String("444") ? 1 : 2;
		if (profile.hr || !layout || depth != expectedDepth || horizontal != expectedHorizontal || vertical != 1 ||
			(profile.layout >= 0 && profile.layout != *layout) ||
			(profile.width != 0 && (profile.width != geometry.first || profile.height != geometry.second)))
			return {};
		for (const auto &point : dnxOperatingPoints)
		{
			if (geometry.first != point.width || geometry.second != point.height || *layout != point.layout || !rate.sameRate(point.rate))
				continue;
			const auto *name = level == QLatin1String("LB") ? point.lb : level == QLatin1String("SQ") ? point.sq
																	 : level == QLatin1String("HQ")	  ? point.hq
																	 : level == QLatin1String("HQX")  ? point.hqx
																									  : point.rgb444;
			return name[0] ? QStringLiteral("DNxHD %1").arg(QLatin1String(name)) : QString{};
		}
		return {};
	}

}
