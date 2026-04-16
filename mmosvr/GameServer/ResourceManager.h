#pragma once

#include "Utils/TSingleton.h"
#include "Utils/CsvParser.h"
#include <unordered_map>
#include <memory>
#include <typeindex>
#include <filesystem>
#include <string>
#include <concepts>


#define GetResourceManager() ResourceManager::Instance()


// ── Concept ─────────────────────────────────────────────────────────

template <typename T>
concept KeyedResource = requires(const Csv::Row& row)
{
	typename T::KeyType;
	{ T::FromCsv(row) } -> std::same_as<T>;
	{ std::declval<const T&>().GetKey() } -> std::convertible_to<typename T::KeyType>;
};


// ── Table type resolver ─────────────────────────────────────────────
// If T defines T::Table, use it. Otherwise, use KeyedResourceTable<T>.

template <KeyedResource T> class KeyedResourceTable;

template <typename T>
struct ResourceTableTypeHelper { using Type = KeyedResourceTable<T>; };

template <typename T> requires requires { typename T::Table; }
struct ResourceTableTypeHelper<T> { using Type = typename T::Table; };

template <typename T>
using ResourceTableOf = typename ResourceTableTypeHelper<T>::Type;


// ── Table base ──────────────────────────────────────────────────────

class IResourceTableBase
{
public:
	virtual ~IResourceTableBase() = default;
	virtual bool LoadFromFile(const std::string& filePath) = 0;
	virtual size_t Count() const = 0;
};


// ── Keyed table (map by key) ────────────────────────────────────────
// Subclass to add custom query methods (e.g., FindByName).

template <KeyedResource T>
class KeyedResourceTable : public IResourceTableBase
{
public:
	bool LoadFromFile(const std::string& filePath) override
	{
		auto table = Csv::LoadFile(filePath);
		if (table.Empty())
			return false;

		for (const auto& row : table.rows)
		{
			try
			{
				T item = T::FromCsv(row);
				auto key = item.GetKey();
				map_.emplace(key, std::move(item));
			}
			catch (const std::exception& e)
			{
				LOG_WARN(std::format("ResourceManager: Skipping invalid entry in {}: {}",
					filePath, e.what()));
			}
		}
		return true;
	}

	size_t Count() const override { return map_.size(); }

	const T* Find(const typename T::KeyType& key) const
	{
		auto it = map_.find(key);
		return it != map_.end() ? &it->second : nullptr;
	}

	const std::unordered_map<typename T::KeyType, T>& GetAll() const { return map_; }

protected:
	std::unordered_map<typename T::KeyType, T> map_;
};


// ── ResourceManager ─────────────────────────────────────────────────

class ResourceManager : public TSingleton<ResourceManager>
{
public:
	void Init();

	// Returns the table for T.
	// If T defines T::Table (custom table), returns that type.
	// Otherwise returns KeyedResourceTable<T>*.
	template <KeyedResource T>
	const ResourceTableOf<T>* Get() const
	{
		auto it = tables_.find(std::type_index(typeid(T)));
		if (it == tables_.end())
			return nullptr;
		return static_cast<const ResourceTableOf<T>*>(it->second.get());
	}

private:
	template <KeyedResource T>
	void Register(const std::string& filename)
	{
		auto table = std::make_unique<ResourceTableOf<T>>();
		std::string resolved = FindDataFile(filename);
		if (!resolved.empty())
		{
			table->LoadFromFile(resolved);
			LOG_INFO(std::format("ResourceManager: Loaded {} entries from {}",
				table->Count(), filename));
		}
		else
		{
			LOG_WARN("ResourceManager: Data file not found: " + filename);
		}
		tables_[std::type_index(typeid(T))] = std::move(table);
	}

	static std::string FindDataFile(const std::string& filename);

	std::unordered_map<std::type_index, std::unique_ptr<IResourceTableBase>> tables_;
};
