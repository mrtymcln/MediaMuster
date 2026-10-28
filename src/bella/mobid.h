#pragma once

#include <QByteArrayView>
#include <QtGlobal>
#include <array>
#include <cstddef>
#include <variant>

namespace Bella
{
	enum class MobIdLayout
	{
		PmrLegacy8,
		OmfUid12,
		Aaf32
	};

	enum class ByteOrder
	{
		LittleEndian,
		BigEndian
	};

	enum class MobIdKind
	{
		Aaf32,
		OmfUid12
	};

	enum class MobIdError
	{
		InvalidWidth
	};

	class MobId;
	using MobIdResult = std::variant<MobId, MobIdError>;

	/// Converts an exact, bounded ID using its established layout and byte order.
	/// Unknown source layouts/orders must be resolved by the reader before calling.
	/// Expected width failures return InvalidWidth; input bytes are never retained.
	[[nodiscard]] MobIdResult normalizeMobId(QByteArrayView bytes, MobIdLayout layout,
										   ByteOrder byteOrder) noexcept;

	/// Owns a complete ID in database numeric-field order, independent of host order.
	/// An unfamiliar legacy UID remains a distinct 12-byte form, not a padded AAF ID.
	class MobId
	{
	public:
		[[nodiscard]] MobIdKind kind() const noexcept;

		/// Borrows this value's active bytes (32 or 12); do not outlive the value.
		[[nodiscard]] QByteArrayView bytes() const noexcept;

	private:
		friend MobIdResult normalizeMobId(QByteArrayView, MobIdLayout, ByteOrder) noexcept;
		MobId(const std::array<quint8, 32> &bytes, MobIdKind kind) noexcept;

		std::array<quint8, 32> m_bytes;
		MobIdKind m_kind;
	};

	[[nodiscard]] bool operator==(const MobId &left, const MobId &right) noexcept;
	[[nodiscard]] bool operator!=(const MobId &left, const MobId &right) noexcept;
	[[nodiscard]] size_t qHash(const MobId &id, size_t seed = 0) noexcept;
}
