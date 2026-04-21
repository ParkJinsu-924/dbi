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

	// Player 세션이 살아있으면 chunk 송신. 끊겼거나 bind 안됐으면 조용히 skip.
	void SendIfConnected(const Player& player, const SendBufferChunkPtr& chunk)
	{
		if (const auto session = player.GetSession())
		{
			if (session->IsConnected())
				std::static_pointer_cast<Session>(session)->Send(chunk);
		}
	}
}


void Zone::Add(std::shared_ptr<GameObject> obj)
{
	// obj 의 zone_ 멤버는 ctor 에서 이미 바인딩 — 이 Zone 과 같아야 한다.
	// (object 재생성 모델이라 한 객체가 다른 Zone 으로 옮겨가지 않는다.)
	assert(&obj->GetZone() == this);
	InsertObject(std::move(obj));
}

void Zone::Remove(long long guid)
{
	EraseObject(guid);
}

std::shared_ptr<GameObject> Zone::Find(const long long guid) const
{
	const auto it = objects_.find(guid);
	if (it == objects_.end())
		return nullptr;
	return it->second;
}

void Zone::InsertObject(std::shared_ptr<GameObject> obj)
{
	const long long guid = obj->GetGuid();
	const GameObjectType type = obj->GetType();
	objects_[guid] = obj;
	objectsByType_[type][guid] = std::move(obj);
}

void Zone::EraseObject(const long long guid)
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
	// 1. 모든 객체 tick
	for (const auto& obj : objects_ | std::views::values)
		obj->Update(deltaTime);

	// 2. 제거 대상 수집 (consumed projectile 등) → pendingRemove_
	CollectDeadObjects();

	// 3. pending Add/Remove 일괄 반영 (Spawn 결과 + 위 수집분)
	FlushPendingObjects();

	// 4. 주기적 위치 방송 (Monster / 이동 중인 Player)
	BroadcastObjectPositions(deltaTime);
}

void Zone::FlushPendingObjects()
{
	if (pendingAdd_.empty() && pendingRemove_.empty())
		return;

	for (auto& obj : pendingAdd_)
		InsertObject(std::move(obj));
	for (const auto guid : pendingRemove_)
		EraseObject(guid);

	pendingAdd_.clear();
	pendingRemove_.clear();
}

void Zone::BroadcastObjectPositions(const float deltaTime)
{
	// Monster — 모든 몬스터의 현재 위치를 주기적으로 방송.
	monsterBroadcastAccum_ += deltaTime;
	if (monsterBroadcastAccum_ >= MONSTER_BROADCAST_INTERVAL)
	{
		monsterBroadcastAccum_ = 0.0f;
		ForEachOfType(GameObjectType::Monster, [&](long long /*guid*/, const std::shared_ptr<GameObject>& obj)
			{
				Broadcast(PacketMaker::MakeMonsterMove(*std::static_pointer_cast<Monster>(obj)));
			});
	}

	// Player — 이동 중인 플레이어만 방송. 정지 플레이어는 C_StopMove 수신 시 최종 위치를 한 번 방송했음.
	playerBroadcastAccum_ += deltaTime;
	if (playerBroadcastAccum_ >= PLAYER_BROADCAST_INTERVAL)
	{
		playerBroadcastAccum_ = 0.0f;
		ForEachOfType(GameObjectType::Player, [&](long long /*guid*/, const std::shared_ptr<GameObject>& obj)
			{
				auto player = std::static_pointer_cast<Player>(obj);
				if (!player->IsMoving())
					return;
				Broadcast(PacketMaker::MakePlayerMove(*player));
			});
	}
}

void Zone::BroadcastChunk(const SendBufferChunkPtr& chunk) const
{
	ForEachOfType(GameObjectType::Player, [&](long long /*guid*/, const std::shared_ptr<GameObject>& obj)
		{
			SendIfConnected(*std::static_pointer_cast<Player>(obj), chunk);
		});
}

void Zone::BroadcastChunkExcept(const SendBufferChunkPtr& chunk, long long excludeGuid) const
{
	ForEachOfType(GameObjectType::Player, [&](long long guid, const std::shared_ptr<GameObject>& obj)
		{
			if (guid == excludeGuid) return;
			SendIfConnected(*std::static_pointer_cast<Player>(obj), chunk);
		});
}

void Zone::SendChunkTo(const SendBufferChunkPtr& chunk, const long long guid) const
{
	// objects_ 직접 조회 — O(1). guid 가 Player 가 아니면 무시.
	const auto it = objects_.find(guid);
	if (it == objects_.end()) return;
	if (it->second->GetType() != GameObjectType::Player) return;

	SendIfConnected(*std::static_pointer_cast<Player>(it->second), chunk);
}

void Zone::BroadcastChunkTo(const SendBufferChunkPtr& chunk, const std::span<const long long> guids) const
{
	// 대상자 수 N << 전체 Zone 플레이어 M 인 경우 (파티/어그로) 직접 조회가 순회보다 빠름.
	for (const long long guid : guids)
		SendChunkTo(chunk, guid);
}

void Zone::CollectDeadObjects()
{
	// consumed projectile (Update 중 적중/만료된 것) → pendingRemove_ 큐에 적재.
	// 실제 erase 는 FlushPendingObjects 에서 수행 (순회 중 수정 방지).
	ForEachOfType(GameObjectType::Projectile,
		[&](const long long guid, const std::shared_ptr<GameObject>& obj)
		{
			const auto p = std::static_pointer_cast<Projectile>(obj);
			if (p->IsConsumed())
				pendingRemove_.push_back(guid);
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

	pendingAdd_.push_back(p);

	Broadcast(PacketMaker::MakeSkillshotProjectileSpawn(*p));

	return p;
}
