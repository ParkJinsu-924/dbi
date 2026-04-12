#pragma once

#include <typeindex>
#include <unordered_map>
#include <memory>


class ServiceLocator
{
public:
	template<typename T>
	void Register(std::shared_ptr<T> service)
	{
		services_[std::type_index(typeid(T))] = std::move(service);
	}

	template<typename T>
	std::shared_ptr<T> Get() const
	{
		auto it = services_.find(std::type_index(typeid(T)));
		if (it == services_.end())
			return nullptr;
		return std::static_pointer_cast<T>(it->second);
	}

	template<typename T>
	void Remove()
	{
		services_.erase(std::type_index(typeid(T)));
	}

private:
	std::unordered_map<std::type_index, std::shared_ptr<void>> services_;
};
