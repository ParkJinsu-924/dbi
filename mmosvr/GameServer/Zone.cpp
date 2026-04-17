#include "pch.h"
#include "Zone.h"
#include "GameObject.h"
#include "Player.h"
#include "Monster.h"
#include "Projectile.h"
#include "HomingProjectile.h"
#include "SkillshotProjectile.h"
#include "Network/Session.h"
#include "Network/PacketSession.h"
#include "game.pb.h"


namespace
{
	constexpr float MONSTER_BROADCAST_INTERVAL = 0.1f;  // 10 Hz
}


void Zone::Add(std::shared_ptr<GameObject> obj)
{
	long long guid = obj->GetGuid();
	objects_.Write([&](auto& m)
		{
			m[guid] = std::move(obj);
		});
}

void Zone::Remove(long long guid)
{
	objects_.Write([&](auto& m)
		{
			m.erase(guid);
		});
}

std::shared_ptr<GameObject> Zone::Find(long long guid) const
{
	return objects_.Read([&](const auto& m) -> std::shared_ptr<GameObject>
		{
			auto it = m.find(guid);
			if (it == m.end())
				return nullptr;
			return it->second;
		});
}

void Zone::Update(float deltaTime)
{
	// 1. Tick all objects
	objects_.Read([&](const auto& m)
		{
			for (const auto& [guid, obj] : m)
				obj->Update(deltaTime);
		});

	// 2. Auto-cleanup consumed projectiles (Update 중 적중/만료된 것)
	objects_.Read([&](const auto& m)
		{
			for (const auto& [guid, obj] : m)
			{
				if (obj->GetType() != GameObjectType::Projectile)
					continue;
				auto p = std::static_pointer_cast<Projectile>(obj);
				if (p->IsConsumed())
				{
					pendingRemove_.WithLock([&](auto& v) { v.push_back(guid); });
				}
			}
		});

	// 3. Flush pending Add/Remove (Spawn 호출 결과 + 위 cleanup)
	FlushPending();

	// 4. Broadcast monster positions periodically
	monsterBroadcastAccum_ += deltaTime;
	if (monsterBroadcastAccum_ >= MONSTER_BROADCAST_INTERVAL)
	{
		monsterBroadcastAccum_ = 0.0f;
		BroadcastMonsterPositions();
	}
}

void Zone::FlushPending()
{
	std::vector<std::shared_ptr<GameObject>> toAdd;
	std::vector<long long> toRemove;
	pendingAdd_.WithLock([&](auto& v) { toAdd.swap(v); });
	pendingRemove_.WithLock([&](auto& v) { toRemove.swap(v); });

	if (toAdd.empty() && toRemove.empty())
		return;

	objects_.Write([&](auto& m)
		{
			for (auto& obj : toAdd)
				m[obj->GetGuid()] = std::move(obj);
			for (auto guid : toRemove)
				m.erase(guid);
		});
}

void Zone::BroadcastChunk(SendBufferChunkPtr chunk)
{
	objects_.Read([&](const auto& m)
		{
			for (const auto& [guid, obj] : m)
			{
				if (obj->GetType() != GameObjectType::Player)
					continue;

				auto player = std::static_pointer_cast<Player>(obj);
				if (auto session = player->GetSession())
				{
					if (session->IsConnected())
						std::static_pointer_cast<Session>(session)->Send(chunk);
				}
			}
		});
}

std::shared_ptr<Player> Zone::FindNearestPlayer(const Proto::Vector3& from, float maxRange) const
{
	std::shared_ptr<Player> nearest;
	float nearestDistSq = maxRange * maxRange;

	objects_.Read([&](const auto& m)
		{
			for (const auto& [guid, obj] : m)
			{
				if (obj->GetType() != GameObjectType::Player)
					continue;

				auto player = std::static_pointer_cast<Player>(obj);
				if (!player->IsAlive())
					continue;

				const auto& pos = obj->GetPosition();
				float dx = pos.x() - from.x();
				float dz = pos.z() - from.z();
				float distSq = dx * dx + dz * dz;

				if (distSq < nearestDistSq)
				{
					nearestDistSq = distSq;
					nearest = player;
				}
			}
		});

	return nearest;
}

std::shared_ptr<Monster> Zone::FindNearestMonster(const Proto::Vector3& from, float maxRange) const
{
	std::shared_ptr<Monster> nearest;
	float nearestDistSq = maxRange * maxRange;

	objects_.Read([&](const auto& m)
		{
			for (const auto& [guid, obj] : m)
			{
				if (obj->GetType() != GameObjectType::Monster)
					continue;

				auto monster = std::static_pointer_cast<Monster>(obj);
				if (!monster->IsAlive())
					continue;

				const auto& pos = obj->GetPosition();
				float dx = pos.x() - from.x();
				float dz = pos.z() - from.z();
				float distSq = dx * dx + dz * dz;

				if (distSq < nearestDistSq)
				{
					nearestDistSq = distSq;
					nearest = monster;
				}
			}
		});

	return nearest;
}

void Zone::BroadcastMonsterPositions()
{
	objects_.Read([&](const auto& m)
		{
			for (const auto& [guid, obj] : m)
			{
				if (obj->GetType() != GameObjectType::Monster)
					continue;

				Proto::S_MonsterMove pkt;
				pkt.set_guid(guid);
				*pkt.mutable_position() = obj->GetPosition();
				Broadcast(pkt);
			}
		});
}

std::shared_ptr<HomingProjectile> Zone::SpawnHomingProjectile(
	long long ownerGuid, GameObjectType ownerType, long long targetGuid,
	const Proto::Vector3& startPos,
	int32 damage, float speed, float lifetime)
{
	auto p = std::make_shared<HomingProjectile>(
		ownerGuid, ownerType, targetGuid,
		damage, speed, lifetime, this);
	p->SetPosition(startPos);
	p->SetZoneId(id_);

	pendingAdd_.WithLock([&](auto& v) { v.push_back(p); });

	Proto::S_ProjectileSpawn pkt;
	pkt.set_guid(p->GetGuid());
	pkt.set_owner_guid(ownerGuid);
	pkt.set_kind(Proto::PROJECTILE_HOMING);
	*pkt.mutable_start_pos() = startPos;
	pkt.set_speed(speed);
	pkt.set_target_guid(targetGuid);
	pkt.set_max_lifetime(lifetime);
	Broadcast(pkt);

	return p;
}

std::shared_ptr<SkillshotProjectile> Zone::SpawnSkillshotProjectile(
	long long ownerGuid, GameObjectType ownerType,
	const Proto::Vector3& startPos,
	float dirX, float dirZ,
	int32 damage, float speed, float radius, float range)
{
	auto p = std::make_shared<SkillshotProjectile>(
		ownerGuid, ownerType, dirX, dirZ,
		damage, speed, radius, range, this);
	p->SetPosition(startPos);
	p->SetZoneId(id_);

	pendingAdd_.WithLock([&](auto& v) { v.push_back(p); });

	Proto::S_ProjectileSpawn pkt;
	pkt.set_guid(p->GetGuid());
	pkt.set_owner_guid(ownerGuid);
	pkt.set_kind(Proto::PROJECTILE_SKILLSHOT);
	*pkt.mutable_start_pos() = startPos;
	pkt.set_speed(speed);
	auto* dir = pkt.mutable_dir();
	dir->set_x(dirX);
	dir->set_y(0.0f);
	dir->set_z(dirZ);
	pkt.set_radius(radius);
	pkt.set_max_range(range);
	Broadcast(pkt);

	return p;
}
