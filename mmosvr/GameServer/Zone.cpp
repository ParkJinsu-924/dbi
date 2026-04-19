#include "pch.h"
#include "Zone.h"
#include "GameObject.h"
#include "Player.h"
#include "Monster.h"
#include "Projectile.h"
#include "HomingProjectile.h"
#include "SkillshotProjectile.h"
#include "PacketMaker.h"
#include "Utils/MathUtil.h"
#include "Network/Session.h"
#include "game.pb.h"


namespace
{
	constexpr float MONSTER_BROADCAST_INTERVAL = 0.1f;  // 10 Hz
	constexpr float PLAYER_BROADCAST_INTERVAL  = 0.1f;  // 10 Hz. 클릭 이동 중인 플레이어의 위치만 방송.
}


void Zone::Add(std::shared_ptr<GameObject> obj)
{
	insertObject(std::move(obj));
}

void Zone::Remove(long long guid)
{
	eraseObject(guid);
}

std::shared_ptr<GameObject> Zone::Find(const long long guid) const
{
	const auto it = objects_.find(guid);
	if (it == objects_.end())
		return nullptr;
	return it->second;
}

void Zone::insertObject(std::shared_ptr<GameObject> obj)
{
	const long long guid = obj->GetGuid();
	const GameObjectType type = obj->GetType();
	objects_[guid] = obj;
	objectsByType_[type][guid] = std::move(obj);
}

void Zone::eraseObject(const long long guid)
{
	const auto it = objects_.find(guid);
	if (it == objects_.end()) return;
	const GameObjectType type = it->second->GetType();
	const auto bucketIt = objectsByType_.find(type);
	if (bucketIt != objectsByType_.end())
		bucketIt->second.erase(guid);
	objects_.erase(it);
}

void Zone::Update(const float deltaTime)
{
	// 1. Tick all objects
	for (const auto& obj : objects_ | std::views::values)
		obj->Update(deltaTime);
	
	// 2. Auto-cleanup consumed projectiles (Update 중 적중/만료된 것)
	ForEachOfType(GameObjectType::Projectile, 
		[&](const long long guid, const std::shared_ptr<GameObject>& obj)
	{
		const auto p = std::static_pointer_cast<Projectile>(obj);
		if (p->IsConsumed())
			pendingRemove_.push_back(guid);
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

	// 5. Broadcast moving-player positions periodically (LoL-style click-to-move)
	playerBroadcastAccum_ += deltaTime;
	if (playerBroadcastAccum_ >= PLAYER_BROADCAST_INTERVAL)
	{
		playerBroadcastAccum_ = 0.0f;
		BroadcastPlayerPositions();
	}
}

void Zone::FlushPending()
{
	if (pendingAdd_.empty() && pendingRemove_.empty())
		return;

	for (auto& obj : pendingAdd_)
		insertObject(std::move(obj));
	for (const auto guid : pendingRemove_)
		eraseObject(guid);

	pendingAdd_.clear();
	pendingRemove_.clear();
}

void Zone::BroadcastChunk(const SendBufferChunkPtr& chunk) const
{
	ForEachOfType(GameObjectType::Player, [&](long long /*guid*/, const std::shared_ptr<GameObject>& obj)
	{
		auto player = std::static_pointer_cast<Player>(obj);
		if (auto session = player->GetSession())
		{
			if (session->IsConnected())
				std::static_pointer_cast<Session>(session)->Send(chunk);
		}
	});
}

void Zone::BroadcastChunkExcept(const SendBufferChunkPtr& chunk, long long excludeGuid) const
{
	ForEachOfType(GameObjectType::Player, [&](long long guid, const std::shared_ptr<GameObject>& obj)
	{
		if (guid == excludeGuid) return;
		auto player = std::static_pointer_cast<Player>(obj);
		if (auto session = player->GetSession())
		{
			if (session->IsConnected())
				std::static_pointer_cast<Session>(session)->Send(chunk);
		}
	});
}

void Zone::SendChunkTo(const SendBufferChunkPtr& chunk, const long long guid) const
{
	// objects_ 직접 조회 — O(1). guid 가 Player 가 아니면 무시.
	const auto it = objects_.find(guid);
	if (it == objects_.end()) return;
	if (it->second->GetType() != GameObjectType::Player) return;

	const auto player = std::static_pointer_cast<Player>(it->second);
	if (const auto session = player->GetSession())
	{
		if (session->IsConnected())
			std::static_pointer_cast<Session>(session)->Send(chunk);
	}
}

void Zone::BroadcastChunkTo(const SendBufferChunkPtr& chunk, std::span<const long long> guids) const
{
	// 대상자 수 N << 전체 Zone 플레이어 M 인 경우 (파티/어그로) 직접 조회가 순회보다 빠름.
	for (const long long guid : guids)
		SendChunkTo(chunk, guid);
}

std::shared_ptr<Player> Zone::FindNearestPlayer(const Proto::Vector2& from, float maxRange) const
{
	std::shared_ptr<Player> nearest;
	float nearestDistSq = maxRange * maxRange;

	ForEachOfType(GameObjectType::Player, [&](long long /*guid*/, const std::shared_ptr<GameObject>& obj)
	{
		auto player = std::static_pointer_cast<Player>(obj);
		if (!player->IsAlive()) return;

		const float distSq = MathUtil::Distance2DSq(obj->GetPosition(), from);
		if (distSq < nearestDistSq)
		{
			nearestDistSq = distSq;
			nearest = player;
		}
	});

	return nearest;
}

std::shared_ptr<Monster> Zone::FindNearestMonster(const Proto::Vector2& from, float maxRange) const
{
	std::shared_ptr<Monster> nearest;
	float nearestDistSq = maxRange * maxRange;

	ForEachOfType(GameObjectType::Monster, [&](long long /*guid*/, const std::shared_ptr<GameObject>& obj)
	{
		const auto monster = std::static_pointer_cast<Monster>(obj);
		if (!monster->IsAlive()) return;

		const float distSq = MathUtil::Distance2DSq(obj->GetPosition(), from);
		if (distSq < nearestDistSq)
		{
			nearestDistSq = distSq;
			nearest = monster;
		}
	});

	return nearest;
}

void Zone::BroadcastMonsterPositions()
{
	ForEachOfType(GameObjectType::Monster, [&](long long /*guid*/, const std::shared_ptr<GameObject>& obj)
	{
		Broadcast(PacketMaker::MakeMonsterMove(*std::static_pointer_cast<Monster>(obj)));
	});
}

void Zone::BroadcastPlayerPositions()
{
	// 이동 중인 플레이어만 방송한다. 정지 플레이어는 C_StopMove 수신 시 이미 최종 위치를 한 번 방송했음.
	ForEachOfType(GameObjectType::Player, [&](long long /*guid*/, const std::shared_ptr<GameObject>& obj)
	{
		auto player = std::static_pointer_cast<Player>(obj);
		if (!player->IsMoving())
			return;
		Broadcast(PacketMaker::MakePlayerMove(*player));
	});
}

std::shared_ptr<HomingProjectile> Zone::SpawnHomingProjectile(
	long long ownerGuid, GameObjectType ownerType, long long targetGuid,
	int32 skillId, const Proto::Vector2& startPos,
	int32 damage, float speed, float lifetime)
{
	auto p = std::make_shared<HomingProjectile>(
		ownerGuid, ownerType, targetGuid,
		skillId, damage, speed, lifetime, *this);
	p->SetPosition(startPos);
	p->SetZoneId(id_);

	pendingAdd_.push_back(p);

	Broadcast(PacketMaker::MakeHomingProjectileSpawn(*p));

	return p;
}

std::shared_ptr<SkillshotProjectile> Zone::SpawnSkillshotProjectile(
	long long ownerGuid, GameObjectType ownerType,
	int32 skillId, const Proto::Vector2& startPos,
	float dirX, float dirZ,
	int32 damage, float speed, float radius, float range)
{
	auto p = std::make_shared<SkillshotProjectile>(
		ownerGuid, ownerType, dirX, dirZ,
		skillId, damage, speed, radius, range, *this);
	p->SetPosition(startPos);
	p->SetZoneId(id_);

	pendingAdd_.push_back(p);

	Broadcast(PacketMaker::MakeSkillshotProjectileSpawn(*p));

	return p;
}
