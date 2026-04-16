#pragma once

#include "Zone.h"
#include "Utils/TSingleton.h"
#include <unordered_map>
#include <memory>
#include <shared_mutex>


constexpr int32 DEFAULT_ZONE_ID = 1;

#define GetZoneManager() ZoneManager::Instance()

class ZoneManager : public TSingleton<ZoneManager>
{
public:
	void Init();
	void Update(float deltaTime);
	void Shutdown();

	Zone* CreateZone(int32 id);
	Zone* GetZone(int32 id) const;
	void RemoveZone(int32 id);

private:
	mutable std::shared_mutex mutex_;
	std::unordered_map<int32, std::unique_ptr<Zone>> zones_;
};
