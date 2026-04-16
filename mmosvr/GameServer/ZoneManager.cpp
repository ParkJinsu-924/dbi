#include "pch.h"
#include "ZoneManager.h"


void ZoneManager::Init()
{
	zones_[DEFAULT_ZONE_ID] = std::make_unique<Zone>(DEFAULT_ZONE_ID);
	LOG_INFO("ZoneManager initialized (zone " + std::to_string(DEFAULT_ZONE_ID) + " created)");
}

void ZoneManager::Update(float deltaTime)
{
	std::shared_lock lock(mutex_);
	for (auto& [id, zone] : zones_)
		zone->Update(deltaTime);
}

void ZoneManager::Shutdown()
{
	std::unique_lock lock(mutex_);
	zones_.clear();
	LOG_INFO("ZoneManager shutdown");
}

Zone* ZoneManager::CreateZone(int32 id)
{
	std::unique_lock lock(mutex_);
	auto [it, inserted] = zones_.try_emplace(id, nullptr);
	if (inserted)
	{
		it->second = std::make_unique<Zone>(id);
		LOG_INFO("Zone created: id=" + std::to_string(id));
	}
	return it->second.get();
}

Zone* ZoneManager::GetZone(int32 id) const
{
	std::shared_lock lock(mutex_);
	auto it = zones_.find(id);
	if (it == zones_.end())
		return nullptr;
	return it->second.get();
}

void ZoneManager::RemoveZone(int32 id)
{
	std::unique_lock lock(mutex_);
	zones_.erase(id);
}
