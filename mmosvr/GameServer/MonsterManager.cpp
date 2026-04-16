#include "pch.h"
#include "MonsterManager.h"
#include "ZoneManager.h"
#include "Zone.h"
#include "game.pb.h"


std::shared_ptr<Monster> MonsterManager::Spawn(int32 zoneId, const std::string& name,
	const Proto::Vector3& spawnPos)
{
	auto* zone = GetZoneManager().GetZone(zoneId);
	if (!zone)
		return nullptr;

	auto monster = std::make_shared<Monster>(name);
	monster->SetZoneId(zoneId);
	monster->InitAI(spawnPos, zone);

	zone->Add(monster);

	Proto::S_MonsterSpawn pkt;
	pkt.set_guid(monster->GetGuid());
	pkt.set_name(monster->GetName());
	*pkt.mutable_position() = monster->GetPosition();
	zone->Broadcast(pkt);

	LOG_INFO("Monster spawned: guid=" + std::to_string(monster->GetGuid())
		+ " name=" + name + " zone=" + std::to_string(zoneId));
	return monster;
}

void MonsterManager::Despawn(int32 zoneId, long long guid)
{
	auto* zone = GetZoneManager().GetZone(zoneId);
	if (!zone) return;

	zone->Remove(guid);

	Proto::S_MonsterDespawn pkt;
	pkt.set_guid(guid);
	zone->Broadcast(pkt);

	LOG_INFO("Monster despawned: guid=" + std::to_string(guid));
}
