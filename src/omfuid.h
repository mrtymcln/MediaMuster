#pragma once

// OMF-era (legacy Avid media, pre-MXF). An OMF essence file is an Apple
// Bento container with the essence first and the object table of contents
// at the tail; it lives flat in "OMFI MediaFiles" beside a version-2
// msmFMID.pmr (8-byte MOBs) and a msmMMOB.mdb whose mobs carry 12-byte
// omfi:UIDs instead of the 32-byte UMIDs every MXF-era source writes.
// This header is the ID bridge between those widths. MXF-era handling
// (MobId, MxfParser, PmrParser's version-8 path) lives elsewhere and is
// unaffected.

#include "mobid.h"

#include <QByteArrayView>
#include <QString>
#include <QtEndian>
#include <array>
#include <cstring>

/// Avid's prefix-42 OMF IDs use bytes [4:12] of the 12-byte omfi:UID
/// in the legacy PMR. Avid already chose how to widen those IDs: its PMR Unicode
/// set and its .avb bins wrap those 8 bytes in a constant 16-byte prefix
/// and 8-byte suffix. Reusing that wrap, byte for byte, means an OMF row's
/// ID is the same string Avid writes into its bins, and nothing downstream
/// has to learn a second form for Avid media. General OMF IDs preserve
/// all three words in an explicit omf: namespace, without claiming a PMR join.
///
/// Byte-verified against tests/fixtures/omf/mc2026_audio/msmFMID.pmr and
/// avid_supporting/msmFMID.pmr (every Unicode-set MOB is kPrefix + the
/// pair's 8 bytes + kSuffix) and against the two OMF bins.
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

	/// [AVID — DO NOT CHANGE] The 16 bytes Avid puts before the 8-byte core
	/// when it writes a 32-byte form. NOT a discriminator on its own: a
	/// SMPTE UMID has 0x05 at byte 7 and 0x0f10 at bytes 10-11, but Avid's
	/// own MXF-era writer also mints MobIDs with exactly these 16 bytes
	/// (four in the corpus MDB msmMMOB_round3.mdb, four in a live MC 2026
	/// MXF folder, 2026-09-03) — they differ only in the suffix below.
	inline constexpr unsigned char kPrefix[16] = {0x06, 0x0a, 0x2b, 0x34, 0x01, 0x01, 0x01, 0x01,
												  0x01, 0x01, 0x0f, 0x00, 0x13, 0x00, 0x00, 0x00};

	/// [AVID — DO NOT CHANGE] The 8 bytes Avid puts after the core — the
	/// AAF SDK's "prefix 42" material marker (7f7f2a80 is 42 padded). This
	/// is what keeps the two forms apart: an MXF-era MobID carries a per-host
	/// random here (8e40da9649f531f4 in every specimen above), never this.
	/// isOmfForm therefore requires prefix AND suffix.
	inline constexpr unsigned char kSuffix[8] = {0x06, 0x0e, 0x2b, 0x34, 0x7f, 0x7f, 0x2a, 0x80};

	/// kPrefix + eight + kSuffix. Deliberately NO middle-field swap: the
	/// wrap is Avid's own byte layout, not an MXF UMID, so the PMR/MDB
	/// versus MXF/AVB byte-order distinction MobId::swapMiddleFields exists
	/// for does not apply. Caller guarantees kPmrSize valid bytes.
	inline std::array<unsigned char, MobId::kRawSize> wrap8(const unsigned char *eight)
	{
		std::array<unsigned char, MobId::kRawSize> out;
		std::memcpy(out.data(), kPrefix, sizeof kPrefix);
		std::memcpy(out.data() + sizeof kPrefix, eight, kPmrSize);
		std::memcpy(out.data() + sizeof kPrefix + kPmrSize, kSuffix, sizeof kSuffix);
		return out;
	}

	// MARK: - Canonical hex

	/// The canonical dotted hex of a v2 PMR record's 8-byte MOB.
	inline QString canonicalFromPmr8(const unsigned char *eight)
	{
		return MobId::format(wrap8(eight).data());
	}

	/// One formatter for whatever width a mob arrives in: a prefix-42
	/// omfi:UID is wrapped from its core, other 12-byte UIDs retain all words,
	/// and a 32-byte UMID (MC 2026 writes
	/// one on the physical mob of the same file) formats unchanged, and any
	/// other width is empty so a caller can skip it rather than guess.
	inline QString canonicalHex(QByteArrayView uid)
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
				return canonicalFromPmr8(raw + kUidCoreOffset);
			return QStringLiteral("omf:") + QString::fromLatin1(uid.toByteArray().toHex());
		}
		if (uid.size() == MobId::kRawSize)
			return MobId::format(raw);
		return {};
	}

	/// True when a canonical hex is a wrapped OMF-era id: Avid's prefix,
	/// any core, Avid's suffix. The dotted spelling is derived from the
	/// byte constants above so the two can't drift.
	inline bool isOmfForm(const QString &canonicalHex)
	{
		static const QString zeroWrap = [] {
			const unsigned char zeros[kPmrSize] = {};
			return canonicalFromPmr8(zeros);
		}();
		// "pppppppppppppppp.pppppppppppppppp." is 34 chars; ".ssssssssssssssss" is 17.
		constexpr int kPrefixChars = 2 * static_cast<int>(sizeof kPrefix) + 2;
		constexpr int kSuffixChars = 2 * static_cast<int>(sizeof kSuffix) + 1;
		return canonicalHex.size() == zeroWrap.size() &&
			   canonicalHex.startsWith(QStringView(zeroWrap).left(kPrefixChars)) &&
			   canonicalHex.endsWith(QStringView(zeroWrap).right(kSuffixChars));
	}
} // namespace OmfUid
