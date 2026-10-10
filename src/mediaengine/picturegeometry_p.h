#pragma once

// Selects a visible raster without discarding the descriptor's other rectangles.
// Callers supply each format's defaults and coordinate system. A crop must fit
// inside its parent; an unexplained disagreement cannot manufacture a size.

#include <QByteArray>
#include <QtGlobal>
#include <limits>
#include <optional>

namespace MediaEngine::Detail
{
	struct PictureRectangle
	{
		std::optional<qint64> width;
		std::optional<qint64> height;
		std::optional<qint64> x;
		std::optional<qint64> y;
	};

	struct PictureGeometry
	{
		PictureRectangle stored;
		PictureRectangle sampled;
		PictureRectangle display;
		bool sampledRecorded = false;
		bool displayRecorded = false;
		bool displayRelativeToSampled = false;
		std::optional<qint64> layout;
		int heightMultiplier = 1;
		QByteArray coding;
		std::optional<qint64> resolutionId;
	};

	struct VisibleGeometry
	{
		enum class Origin
		{
			Unknown,
			Stored,
			Sampled,
			Display,
			VerifiedProxy
		};
		qint64 width = 0;
		qint64 height = 0;
		Origin origin = Origin::Unknown;
	};

	inline bool validRectangle(const PictureRectangle &rectangle)
	{
		return rectangle.width && rectangle.height && rectangle.x && rectangle.y &&
			   *rectangle.width > 0 && *rectangle.height > 0 && *rectangle.x >= 0 && *rectangle.y >= 0;
	}

	inline bool fits(const PictureRectangle &inner, const PictureRectangle &outer)
	{
		return validRectangle(inner) && validRectangle(outer) &&
			   *inner.width <= *outer.width && *inner.height <= *outer.height &&
			   *inner.x <= *outer.width - *inner.width && *inner.y <= *outer.height - *inner.height;
	}

	inline bool verifiedProxyGeometry(const PictureGeometry &geometry)
	{
		// Exact configurations recorded in the proxy-resolution evidence. The
		// profile label alone is H.264 and does not establish a proxy role.
		if (geometry.coding != QByteArray::fromHex("060e2b340401010d0401020201311101") ||
			!geometry.resolutionId || !geometry.layout || !validRectangle(geometry.sampled) ||
			!validRectangle(geometry.display) || *geometry.sampled.x != 0 || *geometry.sampled.y != 0 ||
			*geometry.display.x != 0 || *geometry.display.y != 0)
			return false;
		struct Profile
		{
			qint64 id, width, height, displayWidth, displayHeight, layout;
		};
		static constexpr Profile profiles[] = {
			{3472, 480, 270, 1920, 540, 2},
			{3484, 480, 270, 1920, 1080, 0},
			{3487, 480, 270, 1920, 1080, 0},
			{3488, 320, 180, 1280, 720, 0},
			{3470, 352, 240, 720, 240, 2},
			{3491, 352, 240, 720, 240, 2},
			{3483, 352, 288, 720, 576, 0}};
		for (const auto &profile : profiles)
			if (*geometry.resolutionId == profile.id && *geometry.layout == profile.layout &&
				geometry.stored.width == profile.width && geometry.stored.height == profile.height &&
				geometry.sampled.width == profile.displayWidth && geometry.sampled.height == profile.displayHeight &&
				geometry.display.width == profile.displayWidth && geometry.display.height == profile.displayHeight)
				return true;
		return false;
	}

	inline VisibleGeometry visibleGeometry(const PictureGeometry &geometry)
	{
		if (!validRectangle(geometry.stored) || !geometry.layout ||
			(geometry.heightMultiplier != 1 && geometry.heightMultiplier != 2))
			return {};
		const auto result = [&](const PictureRectangle &rectangle, VisibleGeometry::Origin origin)
		{
			if (*rectangle.height > std::numeric_limits<qint64>::max() / geometry.heightMultiplier)
				return VisibleGeometry{};
			return VisibleGeometry{*rectangle.width, *rectangle.height * geometry.heightMultiplier, origin};
		};
		const bool sampledValid = fits(geometry.sampled, geometry.stored);
		const bool displayValid = geometry.displayRelativeToSampled
									  ? sampledValid && fits(geometry.display, geometry.sampled)
									  : fits(geometry.display, geometry.stored);
		if (displayValid)
		{
			const auto origin = geometry.displayRecorded										? VisibleGeometry::Origin::Display
								: geometry.displayRelativeToSampled && geometry.sampledRecorded ? VisibleGeometry::Origin::Sampled
																								: VisibleGeometry::Origin::Stored;
			return result(geometry.display, origin);
		}
		if (verifiedProxyGeometry(geometry))
			return result(geometry.stored, VisibleGeometry::Origin::VerifiedProxy);
		return {};
	}
}
