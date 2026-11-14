#pragma once

#include <QtGlobal>
#include <array>

namespace Bella::Detail
{
	struct AvidAvcProxyFlavour
	{
		qint64 resolutionId;
		qint64 storedWidth;
		qint64 storedHeight;
		qint64 sampledDisplayWidth;
		qint64 sampledDisplayHeight;
		qint64 frameLayout;
	};

	// The raster from each proxy flavour must both match these stored rasters.
	inline constexpr std::array<AvidAvcProxyFlavour, 7> kAvidAvcProxyFlavours{{
		{3472, 480, 270, 1920, 540, 2},
		{3484, 480, 270, 1920, 1080, 0},
		{3487, 480, 270, 1920, 1080, 0},
		{3488, 320, 180, 1280, 720, 0},
		{3470, 352, 240, 720, 240, 2},
		{3491, 352, 240, 720, 240, 2},
		{3483, 352, 288, 720, 576, 0},
	}};
}
