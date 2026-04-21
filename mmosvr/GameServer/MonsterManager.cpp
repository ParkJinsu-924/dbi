#include "pch.h"
#include "MonsterManager.h"
#include "ResourceManager.h"
#include "MonsterTemplate.h"
#include "ZoneManager.h"
#include "Zone.h"
#include "game.pb.h"
#include "SpawnEntry.h"
#include "PacketMaker.h"


namespace
{
	// zone 등록 + FSM 시작 + Spawn 패킷 방송의 공통 후반부.
	// 템플릿 기반 Spawn 은 ApplyTemplate 이후에 이 단계를 거쳐야 클라가 스탯을 받는다.
	void FinalizeSpawn(const std::shared_ptr<Monster>& monster, const Proto::Vector2& spawnPos)
	{
		Zone& zone = monster->GetZone();
		zone.Add(monster);
		monster->InitAI(spawnPos);
		zone.Broadcast(PacketMaker::MakeMonsterSpawn(*monster));
	}
}


std::shared_ptr<Monster> MonsterManager::Spawn(int32 zoneId, const std::string& name,
                                               const Proto::Vector2& spawnPos)
{
	auto* zone = GetZoneManager().GetZone(zoneId);
	if (!zone)
		return nullptr;

	auto monster = std::make_shared<Monster>(name, *zone);
	FinalizeSpawn(monster, spawnPos);

	LOG_INFO("Monster spawned: guid=" + std::to_string(monster->GetGuid())
		+ " name=" + name + " zone=" + std::to_string(zoneId));
	return monster;
}

std::shared_ptr<Monster> MonsterManager::Spawn(int32 zoneId, int32 templateId,
	const Proto::Vector2& spawnPos)
{
	const auto* tmpl = GetResourceManager().Get<MonsterTemplate>()->Find(templateId);
	if (!tmpl)
	{
		LOG_ERROR(std::format("MonsterManager: Unknown templateId {}", templateId));
		return nullptr;
	}

	auto* zone = GetZoneManager().GetZone(zoneId);
	if (!zone)
		return nullptr;

	auto monster = std::make_shared<Monster>(tmpl->name, *zone);
	monster->ApplyTemplate(templateId, *tmpl);
	FinalizeSpawn(monster, spawnPos);

	LOG_INFO(std::format("Monster spawned: guid={} template={} name={} zone={}",
		monster->GetGuid(), templateId, tmpl->name, zoneId));
	return monster;
}

void MonsterManager::Despawn(int32 zoneId, long long guid)
{
	auto* zone = GetZoneManager().GetZone(zoneId);
	if (!zone) return;

	zone->Remove(guid);
	zone->Broadcast(PacketMaker::MakeMonsterDespawn(guid));

	LOG_INFO("Monster despawned: guid=" + std::to_string(guid));
}
