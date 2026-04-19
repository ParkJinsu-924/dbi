#include "pch.h"
#include "MonsterManager.h"
#include "ResourceManager.h"
#include "MonsterTemplate.h"
#include "ZoneManager.h"
#include "Zone.h"
#include "game.pb.h"
#include "SpawnEntry.h"
#include "PacketMaker.h"


std::shared_ptr<Monster> MonsterManager::Spawn(int32 zoneId, const std::string& name,
                                               const Proto::Vector2& spawnPos)
{
	auto* zone = GetZoneManager().GetZone(zoneId);
	if (!zone)
		return nullptr;

	auto monster = std::make_shared<Monster>(name);
	monster->SetZoneId(zoneId);
	monster->InitAI(spawnPos, zone);

	zone->Add(monster);

	zone->Broadcast(PacketMaker::MakeMonsterSpawn(*monster));

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

	auto monster = std::make_shared<Monster>(tmpl->name);
	monster->SetZoneId(zoneId);
	monster->SetHp(tmpl->hp);
	monster->SetMaxHp(tmpl->maxHp);
	monster->SetDetectRange(tmpl->detectRange);
	monster->SetLeashRange(tmpl->leashRange);
	monster->SetMoveSpeed(tmpl->moveSpeed);
	monster->SetBasicSkillId(tmpl->basicSkillId);
	monster->InitAI(spawnPos, zone);

	zone->Add(monster);

	zone->Broadcast(PacketMaker::MakeMonsterSpawn(*monster));

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
