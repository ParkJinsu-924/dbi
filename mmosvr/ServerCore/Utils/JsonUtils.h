#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <optional>
#include <concepts>


namespace JU
{
	// --- Concepts ---

	namespace concepts
	{
		template <typename T>
		concept Serializable = requires(const T& t)
		{
			{ nlohmann::json(t) } -> std::same_as<nlohmann::json>;
		};

		template <typename T>
		concept Deserializable = requires(const nlohmann::json& j)
		{
			{ j.get<T>() } -> std::same_as<T>;
		};

		template <typename T>
		concept JsonCompatible = Serializable<T> && Deserializable<T>;

		template <typename T>
		concept JsonContainer = requires(T t)
		{
			std::begin(t);
			std::end(t);
			typename T::value_type;
		} && Deserializable<typename T::value_type>;
	}

	// --- ParseResult ---

	template<typename T>
	struct ParseResult
	{
		bool        success = false;
		T           value{};
		std::string error;

		explicit operator bool() const noexcept { return success; }

		const T& operator*()  const&  noexcept { return value; }
		T&       operator*()  &       noexcept { return value; }
		T&&      operator*()  &&      noexcept { return std::move(value); }
		const T* operator->() const   noexcept { return &value; }
		T*       operator->()         noexcept { return &value; }
	};

	namespace detail
	{
		template<typename T>
		inline ParseResult<T> MakeSuccess(T&& value)
		{
			return ParseResult<T>{ true, std::forward<T>(value), {} };
		}

		template<typename T>
		inline ParseResult<T> MakeError(std::string error)
		{
			return ParseResult<T>{ false, T{}, std::move(error) };
		}
	}

	// --- Struct <-> JSON ---

	template<concepts::Serializable T>
	inline nlohmann::json StructToJson(const T& obj)
	{
		return nlohmann::json(obj);
	}

	template<concepts::Serializable T>
	inline std::string StructToJsonString(const T& obj, const int indent = -1)
	{
		return nlohmann::json(obj).dump(indent);
	}

	template<concepts::Deserializable T>
	ParseResult<T> JsonToStruct(const nlohmann::json& j)
	{
		try
		{
			return detail::MakeSuccess<T>(j.get<T>());
		}
		catch (const nlohmann::json::exception& e)
		{
			return detail::MakeError<T>(e.what());
		}
	}

	template<concepts::Deserializable T>
	ParseResult<T> JsonStringToStruct(const std::string& jsonStr)
	{
		if (jsonStr.empty())
			return detail::MakeError<T>("Input string is empty");

		try
		{
			auto j = nlohmann::json::parse(jsonStr);
			return detail::MakeSuccess<T>(j.get<T>());
		}
		catch (const nlohmann::json::exception& e)
		{
			return detail::MakeError<T>(e.what());
		}
	}

	// --- Container <-> JSON ---

	template<concepts::JsonContainer Container>
	ParseResult<Container> JsonToContainer(const nlohmann::json& j)
	{
		try
		{
			if (!j.is_array())
				return detail::MakeError<Container>("JSON is not an array");

			return detail::MakeSuccess<Container>(j.get<Container>());
		}
		catch (const nlohmann::json::exception& e)
		{
			return detail::MakeError<Container>(e.what());
		}
	}

	template<concepts::JsonContainer Container>
		requires concepts::Serializable<typename Container::value_type>
	nlohmann::json ContainerToJson(const Container& container)
	{
		return nlohmann::json(container);
	}

	template<concepts::JsonContainer Container>
	ParseResult<Container> JsonStringToContainer(const std::string& jsonStr)
	{
		if (jsonStr.empty())
			return detail::MakeError<Container>("Input string is empty");

		try
		{
			auto j = nlohmann::json::parse(jsonStr);
			if (!j.is_array())
				return detail::MakeError<Container>("JSON is not an array");

			return detail::MakeSuccess<Container>(j.get<Container>());
		}
		catch (const nlohmann::json::exception& e)
		{
			return detail::MakeError<Container>(e.what());
		}
	}

	template<concepts::JsonContainer Container>
		requires concepts::Serializable<typename Container::value_type>
	std::string ContainerToJsonString(const Container& container, int indent = -1)
	{
		return nlohmann::json(container).dump(indent);
	}

	// --- JSON string helpers ---

	inline ParseResult<nlohmann::json> JsonStringToJson(const std::string& jsonStr)
	{
		if (jsonStr.empty())
			return detail::MakeError<nlohmann::json>("Input string is empty");

		try
		{
			return detail::MakeSuccess<nlohmann::json>(nlohmann::json::parse(jsonStr));
		}
		catch (const nlohmann::json::parse_error& e)
		{
			return detail::MakeError<nlohmann::json>(e.what());
		}
	}

	inline std::string JsonToJsonString(const nlohmann::json& j, const int indent = -1)
	{
		return j.dump(indent);
	}

	inline bool IsValidJsonString(const std::string& jsonStr) noexcept
	{
		if (jsonStr.empty())
			return false;

		return nlohmann::json::accept(jsonStr);
	}

	// --- Field access helpers ---

	inline bool HasKey(const nlohmann::json& j, const std::string& key) noexcept
	{
		return j.is_object() && j.contains(key);
	}

	inline bool HasKeys(const nlohmann::json& j, std::initializer_list<std::string> keys)
	{
		if (!j.is_object())
			return false;

		for (const auto& key : keys)
		{
			if (!j.contains(key))
				return false;
		}
		return true;
	}

	// Safe field extraction with default value
	template<typename T>
	T GetOr(const nlohmann::json& j, const std::string& key, const T& defaultValue)
	{
		try
		{
			if (j.is_object() && j.contains(key))
				return j.at(key).get<T>();
		}
		catch (const nlohmann::json::exception&) {}

		return defaultValue;
	}

	// Safe field extraction returning std::nullopt on failure
	template<typename T>
	std::optional<T> GetOptional(const nlohmann::json& j, const std::string& key)
	{
		try
		{
			if (j.is_object() && j.contains(key))
				return j.at(key).get<T>();
		}
		catch (const nlohmann::json::exception&) {}

		return std::nullopt;
	}
}
