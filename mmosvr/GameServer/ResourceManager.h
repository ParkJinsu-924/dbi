#pragma once

#include "Utils/TSingleton.h"
#include "Utils/CsvParser.h"
#include <unordered_map>
#include <vector>
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
	virtual bool   LoadFromFile(const std::string& filePath) = 0;
	virtual size_t Count() const = 0;

	// 로드 후 FK 검증. 반환값 = 이 테이블이 발견한 에러 수 (0 = OK).
	// 에러 상세는 각 구현이 LOG_ERROR 로 기록. 참조 테이블은 GetResourceManager() 로 접근.
	// 기본 no-op — FK 없는 테이블은 override 불필요.
	virtual int OnValidate() const { return 0; }

	// Solution Explorer / 로그용 디스플레이 이름 (선택).
	virtual const char* DebugName() const { return "IResourceTable"; }
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

		// 로드 완료 후 파생 인덱스(name→item, sid→entries 등) 구축 기회.
		// 기본 구현은 no-op, 서브클래스가 오버라이드.
		OnLoaded();
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
	// 로드 직후 1회 호출. 파생 인덱스 rebuild 용.
	virtual void OnLoaded() {}

	std::unordered_map<typename T::KeyType, T> map_;
};


// ── ResourceManager ─────────────────────────────────────────────────

class ResourceManager : public TSingleton<ResourceManager>
{
public:
	void Init();

	// 모든 테이블의 OnValidate 를 등록 순서대로 호출해 FK 검증.
	// 1건이라도 실패 시 LOG_ERROR + throw → 서버 부팅 중단.
	void ValidateReferences() const;

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

		IResourceTableBase* raw = table.get();
		tables_[std::type_index(typeid(T))] = std::move(table);
		registrationOrder_.push_back(raw);  // OnValidate 순회 순서 결정성 보장
	}

	static std::string FindDataFile(const std::string& filename);

	std::unordered_map<std::type_index, std::unique_ptr<IResourceTableBase>> tables_;
	std::vector<IResourceTableBase*> registrationOrder_;  // 소유권은 tables_ 에, 여기는 관측용 raw.
};
