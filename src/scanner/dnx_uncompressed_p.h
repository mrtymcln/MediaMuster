#pragma once

#include <QByteArray>
#include <QByteArrayView>

namespace Bella::Detail
{
	enum class DnxUncompressedCoding
	{
		Unknown,
		Standard,
		FixedPoint
	};

	inline DnxUncompressedCoding dnxUncompressedCoding(QByteArrayView label)
	{
		using Coding = DnxUncompressedCoding;
		// Avid's dnx_uncompressed.lua maps the older picture coding labels to their RDD 50 equivalents.
		static const struct
		{
			QByteArray label;
			Coding coding;
		} labels[]{
			{QByteArray::fromHex("060e2b340401010d0401020203070100"), Coding::Standard},
			{QByteArray::fromHex("060e2b340401010d0401020203070200"), Coding::FixedPoint},
			{QByteArray::fromHex("060e2b34040101030e04410101010000"), Coding::Standard},
			{QByteArray::fromHex("060e2b34040101030e04410101020000"), Coding::FixedPoint},
			{QByteArray::fromHex("060e2b34040101030e04410101030000"), Coding::Standard},
			{QByteArray::fromHex("060e2b34040101030e04410101040000"), Coding::Standard},
		};
		for (const auto &entry : labels)
			if (label == entry.label)
				return entry.coding;
		return Coding::Unknown;
	}

	template <typename Compression>
	DnxUncompressedCoding recordedDnxUncompressedCoding(const Compression &compression)
	{
		if (compression.codingState != decltype(compression.codingState)::Present)
			return DnxUncompressedCoding::Unknown;
		return dnxUncompressedCoding(compression.codingLabel);
	}
}
