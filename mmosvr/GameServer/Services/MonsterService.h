#pragma once

#include "../Monster.h"
#include "Utils/Synchronized.h"
#include "Utils/TSingleton.h"
#include <unordered_map>
#include <memory>


#define GetMonsterService() MonsterService::Instance()

class MonsterService : public TSingleton<MonsterService>
{
public:
	// Spawn a monster in a zone with circular test movement.
	// Broadcasts S_MonsterSpawn to the zone.
	std::shared_ptr<Monster> Spawn(int32 zoneId, const std::string& name,
		const Proto::Vector3& center, float radius, float angularSpeedRad,
		float startAngleRad = 0.0f);

	void Despawn(long long guid);

	std::shared_ptr<Monster> Find(long long guid) const;

	// For initial player entering: snapshot all monsters in a given zone.
	std::vector<std::shared_ptr<Monster>> GetMonstersInZone(int32 zoneId) const;

private:
	Synchronized<std::unordered_map<long long, std::shared_ptr<Monster>>, std::shared_mutex> monsters_;
};
