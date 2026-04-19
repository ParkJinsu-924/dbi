#pragma once

#include "Monster.h"
#include "Utils/TSingleton.h"
#include <memory>
#include <string>


#define GetMonsterManager() MonsterManager::Instance()

class MonsterManager : public TSingleton<MonsterManager>
{
public:
	std::shared_ptr<Monster> Spawn(int32 zoneId, const std::string& name,
		const Proto::Vector2& spawnPos);

	std::shared_ptr<Monster> Spawn(int32 zoneId, int32 templateId,
		const Proto::Vector2& spawnPos);

	void Despawn(int32 zoneId, long long guid);
};
