#pragma once

#include "../Monster.h"
#include "Utils/TSingleton.h"
#include <memory>
#include <string>


#define GetMonsterManager() MonsterManager::Instance()

class MonsterManager : public TSingleton<MonsterManager>
{
public:
	std::shared_ptr<Monster> Spawn(int32 zoneId, const std::string& name,
		const Proto::Vector3& center, float radius, float angularSpeedRad,
		float startAngleRad = 0.0f);

	void Despawn(int32 zoneId, long long guid);
};
