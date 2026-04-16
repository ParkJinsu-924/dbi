#pragma once

#include <magic_enum.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <algorithm>
#include <type_traits>
#include <limits>


namespace EU
{
	namespace detail
	{
		template <typename E>
		constexpr E GetInvalidEnumValue()
		{
			static_assert(std::is_enum_v<E>, "E must be an enum type.");

			if constexpr (magic_enum::enum_count<E>() == 0)
				return static_cast<E>(0);

			auto maxVal = magic_enum::enum_integer(magic_enum::enum_values<E>().back());
			return static_cast<E>(maxVal + 1);
		}
	}

	// --- String conversion ---

	// Returns string_view directly (zero allocation, preferred for logging/comparison)
	template <typename E>
	constexpr std::string_view EnumToStringView(E value)
	{
		static_assert(std::is_enum_v<E>, "E must be an enum type.");
		return magic_enum::enum_name(value);
	}

	// Returns std::string (use when you need ownership or toLower)
	template <typename E>
	std::string EnumToString(E value, const bool toLower = false)
	{
		static_assert(std::is_enum_v<E>, "E must be an enum type.");

		std::string str(magic_enum::enum_name(value));

		if (toLower)
		{
			std::transform(str.begin(), str.end(), str.begin(),
				[](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
		}

		return str;
	}

	// Case-insensitive string to enum. Returns invalid value on failure.
	template <typename E>
	E StringToEnum(const std::string& value)
	{
		static_assert(std::is_enum_v<E>, "E must be an enum type.");
		return magic_enum::enum_cast<E>(value, magic_enum::case_insensitive)
			.value_or(detail::GetInvalidEnumValue<E>());
	}

	// Case-insensitive string to enum. Returns std::nullopt on failure.
	template <typename E>
	std::optional<E> TryStringToEnum(const std::string& value)
	{
		static_assert(std::is_enum_v<E>, "E must be an enum type.");
		return magic_enum::enum_cast<E>(value, magic_enum::case_insensitive);
	}

	// --- Integer conversion ---

	template <typename E, typename T>
	constexpr E IntToEnum(T value)
	{
		static_assert(std::is_enum_v<E>, "E must be an enum type.");
		static_assert(std::is_integral_v<T>, "Value must be integral.");

		using UnderlyingType = std::underlying_type_t<E>;

		if constexpr (sizeof(UnderlyingType) < sizeof(T))
		{
			if (value < static_cast<T>((std::numeric_limits<UnderlyingType>::min)()) ||
				value > static_cast<T>((std::numeric_limits<UnderlyingType>::max)()))
			{
				return detail::GetInvalidEnumValue<E>();
			}
		}

		return magic_enum::enum_cast<E>(static_cast<UnderlyingType>(value))
			.value_or(detail::GetInvalidEnumValue<E>());
	}

	template <typename E>
	constexpr int EnumToInt(E value)
	{
		static_assert(std::is_enum_v<E>, "E must be an enum type.");
		return static_cast<int>(magic_enum::enum_integer(value));
	}

	// --- Validation ---

	template <typename E>
	constexpr bool IsValid(E value)
	{
		static_assert(std::is_enum_v<E>, "E must be an enum type.");
		return magic_enum::enum_contains(value);
	}

	// --- Flags ---

	template <typename E>
	constexpr bool HasFlag(E value, E flag)
	{
		static_assert(std::is_enum_v<E>, "E must be an enum type.");
		using U = std::underlying_type_t<E>;
		return (static_cast<U>(value) & static_cast<U>(flag)) != 0;
	}

	// --- Optional ---

	template <typename E>
	constexpr E OptionalToEnum(const std::optional<E>& opt)
	{
		static_assert(std::is_enum_v<E>, "E must be an enum type.");
		return opt.value_or(detail::GetInvalidEnumValue<E>());
	}

	// --- Enumeration ---

	template <typename E>
	std::vector<std::string> GetEnumNames(const bool toLower = false)
	{
		static_assert(std::is_enum_v<E>, "E must be an enum type.");

		auto names = magic_enum::enum_names<E>();

		std::vector<std::string> out;
		out.reserve(names.size());

		for (const auto& sv : names)
		{
			std::string s{ sv };

			if (toLower)
			{
				std::transform(s.begin(), s.end(), s.begin(),
					[](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			}

			out.emplace_back(std::move(s));
		}

		return out;
	}

	// Iterate over all enum values with a callback: EU::ForEach<E>([](E val) { ... });
	template <typename E, typename Func>
	constexpr void ForEach(Func&& func)
	{
		static_assert(std::is_enum_v<E>, "E must be an enum type.");

		for (auto value : magic_enum::enum_values<E>())
			func(value);
	}
}
