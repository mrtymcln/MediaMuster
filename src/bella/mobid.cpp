#include "mobid.h"

#include <QHashFunctions>
#include <QtEndian>
#include <algorithm>
#include <cstring>

namespace Bella
{
	namespace
	{
		constexpr qsizetype kPmrLegacySize = 8;
		constexpr qsizetype kOmfUidSize = 12;
		constexpr qsizetype kAafSize = 32;
		constexpr qsizetype kMaterialOffset = 16;
		constexpr qsizetype kMaterialFirstShortOffset = 20;
		constexpr qsizetype kMaterialSecondShortOffset = 22;
		constexpr quint32 kAvidOrganization = 42;

		constexpr std::array<quint8, 16> kLegacyPrefix{
			0x06, 0x0a, 0x2b, 0x34, 0x01, 0x01, 0x01, 0x01,
			0x01, 0x01, 0x0f, 0x00, 0x13, 0x00, 0x00, 0x00};
		constexpr std::array<quint8, 8> kLegacySuffix{
			0x06, 0x0e, 0x2b, 0x34, 0x7f, 0x7f, 0x2a, 0x80};

		qsizetype storedSize(MobIdLayout layout) noexcept
		{
			switch (layout)
			{
			case MobIdLayout::PmrLegacy8:
				return kPmrLegacySize;
			case MobIdLayout::OmfUid12:
				return kOmfUidSize;
			case MobIdLayout::Aaf32:
				return kAafSize;
			}
			Q_UNREACHABLE();
		}

		template <typename T>
		void normalizeNumber(const char *source, quint8 *destination, ByteOrder byteOrder) noexcept
		{
			const T value = byteOrder == ByteOrder::BigEndian
							? qFromBigEndian<T>(source)
							: qFromLittleEndian<T>(source);
			qToLittleEndian<T>(value, destination);
		}

		void wrapLegacyId(std::array<quint8, 32> &bytes, qsizetype coreOffset) noexcept
		{
			// Preserve the normalized two-word core before writing the known wrapper.
			std::copy_n(bytes.data() + coreOffset, kPmrLegacySize, bytes.data() + kMaterialOffset);
			std::copy(kLegacyPrefix.begin(), kLegacyPrefix.end(), bytes.begin());
			std::copy(kLegacySuffix.begin(), kLegacySuffix.end(), bytes.begin() + 24);
		}
	}

	MobId::MobId(const std::array<quint8, 32> &bytes, MobIdKind kind) noexcept
		: m_bytes(bytes), m_kind(kind)
	{
	}

	MobIdKind MobId::kind() const noexcept
	{
		return m_kind;
	}

	QByteArrayView MobId::bytes() const noexcept
	{
		return {m_bytes.data(), m_kind == MobIdKind::Aaf32 ? kAafSize : kOmfUidSize};
	}

	MobIdResult normalizeMobId(QByteArrayView bytes, MobIdLayout layout, ByteOrder byteOrder) noexcept
	{
		Q_ASSERT(byteOrder == ByteOrder::LittleEndian || byteOrder == ByteOrder::BigEndian);
		if (bytes.size() != storedSize(layout))
			return MobIdError::InvalidWidth;

		std::array<quint8, 32> normalized{};
		if (layout == MobIdLayout::Aaf32)
		{
			std::memcpy(normalized.data(), bytes.data(), normalized.size());
			// AAF's material fields are 32/16/16 bits, including wrapped legacy IDs.
			normalizeNumber<quint32>(bytes.data() + kMaterialOffset,
									normalized.data() + kMaterialOffset, byteOrder);
			normalizeNumber<quint16>(bytes.data() + kMaterialFirstShortOffset,
									normalized.data() + kMaterialFirstShortOffset, byteOrder);
			normalizeNumber<quint16>(bytes.data() + kMaterialSecondShortOffset,
									normalized.data() + kMaterialSecondShortOffset, byteOrder);
			return MobId(normalized, MobIdKind::Aaf32);
		}

		// Legacy layouts store complete 32-bit words, not AAF's material-field layout.
		for (qsizetype offset = 0; offset < bytes.size(); offset += sizeof(quint32))
			normalizeNumber<quint32>(bytes.data() + offset, normalized.data() + offset, byteOrder);

		if (layout == MobIdLayout::PmrLegacy8)
		{
			wrapLegacyId(normalized, 0);
			return MobId(normalized, MobIdKind::Aaf32);
		}
		if (qFromLittleEndian<quint32>(normalized.data()) == kAvidOrganization)
		{
			wrapLegacyId(normalized, sizeof(quint32));
			return MobId(normalized, MobIdKind::Aaf32);
		}
		return MobId(normalized, MobIdKind::OmfUid12);
	}

	bool operator==(const MobId &left, const MobId &right) noexcept
	{
		return left.kind() == right.kind() && left.bytes() == right.bytes();
	}

	bool operator!=(const MobId &left, const MobId &right) noexcept
	{
		return !(left == right);
	}

	size_t qHash(const MobId &id, size_t seed) noexcept
	{
		return ::qHashMulti(seed, id.kind(), id.bytes());
	}
}
