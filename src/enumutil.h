#pragma once

#include <type_traits>

/// C++17 backport of 'std::to_underlying' for future C++23 upgrade.
namespace Enum
{

	template <class E>
	constexpr auto to_underlying(E e) noexcept
	{
		static_assert(std::is_enum_v<E>, "to_underlying requires an enum type");
		return static_cast<std::underlying_type_t<E>>(e);
	}

}