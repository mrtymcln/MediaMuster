#pragma once

// Canonical identities for 12-byte OMF UIDs and legacy Avid PMR IDs.
// Avid's prefix-42 wrapper and the general OMF namespace are handled below.

#include "mobid.h"

#include <QByteArrayView>
#include <QString>
#include <QtEndian>
#include <array>
#include <cstring>

/// Prefix-42 OMF UIDs share bytes [4:12] with legacy PMR IDs. Use Avid's
/// 16-byte prefix and 8-byte suffix to match their Unicode PMR and AVB forms.
/// Other OMF UIDs retain all three words in an omf: namespace without claiming a PMR join.
/// Format evidence: docs/parser-compatibility.md.
namespace OmfUid
{
	// MARK: - Widths

	/// The omfi:UID as stored in OMF files and OMF-era MDBs.
	inline constexpr int kUidSize = 12;

	/// Where the 8 identity bytes sit inside the 12-byte UID.
	inline constexpr int kUidCoreOffset = 4;

	/// The MOB field of a version-2 msmFMID.pmr record.
	inline constexpr int kPmrSize = 8;

	// MARK: - Avid's wrapper

	/// Avid's 16-byte wrapper prefix. Modern identities can share it;
	/// only the prefix and suffix together establish the prefix-42 family.
	inline constexpr unsigned char kPrefix[16] = {0x06, 0x0a, 0x2b, 0x34, 0x01, 0x01, 0x01, 0x01,
												  0x01, 0x01, 0x0f, 0x00, 0x13, 0x00, 0x00, 0x00};

	/// AAF's prefix-42 material marker (7f7f2a80 is 42 padded).
	/// This identity family also occurs in MXF; it does not establish container or byte order.
	inline constexpr unsigned char kSuffix[8] = {0x06, 0x0e, 0x2b, 0x34, 0x7f, 0x7f, 0x2a, 0x80};

	/// Wrap kPmrSize readable bytes already in database order, without swapping.
	/// MXF material numeric fields must be converted from wire order before this call.
	inline std::array<unsigned char, MobId::kRawSize> toMobIdBytes(const unsigned char *eight)
	{
		std::array<unsigned char, MobId::kRawSize> out;
		std::memcpy(out.data(), kPrefix, sizeof kPrefix);
		std::memcpy(out.data() + sizeof kPrefix, eight, kPmrSize);
		std::memcpy(out.data() + sizeof kPrefix + kPmrSize, kSuffix, sizeof kSuffix);
		return out;
	}

	// MARK: - Canonical hex

	/// The canonical dotted hex of a v2 PMR record's 8-byte MOB.
	inline QString toMobIdText(const unsigned char *eight)
	{
		return MobId::format(toMobIdBytes(eight).data());
	}

	/// Format 12-byte OMF UIDs or 32-byte MobIds; unsupported widths return empty.
	inline QString toIdText(QByteArrayView uid)
	{
		const auto *raw = reinterpret_cast<const unsigned char *>(uid.data());
		if (uid.size() == kUidSize)
		{
			bool nullUid = true;
			for (int i = 0; i < kUidSize; ++i)
				nullUid &= raw[i] == 0;
			if (nullUid)
				return {}; // OMF's 0-0-0 original-source sentinel has no mob identity
			// Only Avid's prefix-42 IDs have the measured 8-byte PMR bridge.
			// Other OMF producers may use every word of the 12-byte UID.
			// Keep a separate full-UID namespace rather than collapsing distinct
			// sources or inventing an Avid UMID. Input words are little-endian;
			// the file reader normalizes big-endian OMF before calling this.
			if (qFromLittleEndian<quint32>(raw) == 42)
				return toMobIdText(raw + kUidCoreOffset);
			return QStringLiteral("omf:") + QString::fromLatin1(uid.toByteArray().toHex());
		}
		if (uid.size() == MobId::kRawSize)
			return MobId::format(raw);
		return {};
	}

	/// Recognizes the prefix-42 identity family: Avid's prefix, any core and
	/// suffix. This cannot establish whether a value came from OMF or MXF,
	/// or whether its numeric fields have already been converted.
	inline bool isWrappedOmfId(const QString &canonicalHex)
	{
		static const QString zeroWrap = []
		{
			const unsigned char zeros[kPmrSize] = {};
			return toMobIdText(zeros);
		}();
		// Include the dots so comparisons use the same layout as the formatter.
		constexpr int kPrefixChars = 2 * static_cast<int>(sizeof kPrefix) + 2;
		constexpr int kSuffixChars = 2 * static_cast<int>(sizeof kSuffix) + 1;
		return canonicalHex.size() == zeroWrap.size() &&
			   canonicalHex.startsWith(QStringView(zeroWrap).left(kPrefixChars)) &&
			   canonicalHex.endsWith(QStringView(zeroWrap).right(kSuffixChars));
	}
} // namespace OmfUid
