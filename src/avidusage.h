#pragma once

#include <QByteArrayView>
#include <QtGlobal>
#include <array>

// Avid's integer application code is distinct from AAF/MXF's UsageCode UID.
// Public OMF toolkit include/omDefs.h defines these integer values. MC 26.8
// PreparePrecomputeMob (arm64 0x8bc0f0/0x8bc178) writes master 1 / file 9.
namespace AvidUsage
{
	enum class Code : qint32
	{
		NoSpecialUsage = 0, // Context matters: a file mob or a composition can use 0.
		PrecomputeMaster = 1,
		Subclip = 2,
		EffectHolder = 3,
		Group = 4,
		GroupBackup = 5, // Toolkit name: UC_GROUPOOFTER.
		Motion = 6,
		MasterMob = 7,
		PrecomputeFile = 9
	};
	inline constexpr qint32 kMissing = -1;
	inline constexpr qint32 kInvalidOrConflicting = -2;
	// Negative payload values are unknown, not our internal "missing" sentinel.
	constexpr qint32 integerCode(quint32 raw)
	{
		return raw <= 0x7fffffffu ? qint32(raw) : kInvalidOrConflicting;
	}
	inline constexpr quint16 kPrivateMxfTag = 0xf003; // Internal canonical tag; requires a Primer mapping.
	inline constexpr char kPrivateMxfPropertyHex[] = "a022006094eb75cb96c469924f6211d3";

	enum class Classification { Unknown, Media, Precompute };
	constexpr Classification masterClassification(qint32 code)
	{
		return code == qint32(Code::PrecomputeMaster) ? Classification::Precompute :
			code == qint32(Code::MasterMob) ? Classification::Media : Classification::Unknown;
	}
	constexpr bool isMasterCode(qint32 code) { return masterClassification(code) != Classification::Unknown; }
	constexpr qint32 merge(qint32 current, qint32 next)
	{
		return current == kMissing ? next : next == kMissing || current == next ? current : kInvalidOrConflicting;
	}

	enum class StandardUsage { Absent, Subclip, AdjustedClip, TopLevel, LowerLevel, Template, Unknown };
	inline StandardUsage standardUsage(QByteArrayView value)
	{
		if (value.isEmpty()) return StandardUsage::Absent;
		if (value.size() != 16) return StandardUsage::Unknown;
		// Both serializations occur in Avid MXF: SMPTE UL and exchanged 8-byte halves.
		std::array<unsigned char, 16> normalized{};
		const bool swapped = static_cast<unsigned char>(value[0]) == 0x0d;
		for (int n = 0; n < 16; ++n)
			normalized[n] = static_cast<unsigned char>(value[(n + (swapped ? 8 : 0)) % 16]);
		const unsigned char kind = normalized[14];
		normalized[14] = 0;
		constexpr std::array<unsigned char, 16> family = {
			0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x01,0x0d,0x01,0x01,0x02,0x01,0x01,0x00,0x00};
		if (normalized != family) return StandardUsage::Unknown;
		switch (kind)
		{
		case 5: return StandardUsage::Subclip;
		case 6: return StandardUsage::AdjustedClip;
		case 7: return StandardUsage::TopLevel;
		case 8: return StandardUsage::LowerLevel;
		case 9: return StandardUsage::Template;
		default: return StandardUsage::Unknown;
		}
	}

	// Only call for an identified MaterialPackage in a successfully read media
	// header. No usage properties is the ordinary-media form in the Avid corpus.
	// A positive special/unknown usage must never be converted into that default.
	constexpr Classification materialClassification(qint32 code, StandardUsage standard)
	{
		if (code == kMissing)
			return standard == StandardUsage::Absent ? Classification::Media : Classification::Unknown;
		if (code == qint32(Code::PrecomputeMaster))
			return standard == StandardUsage::Absent || standard == StandardUsage::LowerLevel ?
				Classification::Precompute : Classification::Unknown;
		if (code == qint32(Code::MasterMob))
			return standard == StandardUsage::Absent || standard == StandardUsage::AdjustedClip ?
				Classification::Media : Classification::Unknown;
		return Classification::Unknown;
	}
}
