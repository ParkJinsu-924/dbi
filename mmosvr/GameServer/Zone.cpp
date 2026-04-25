#include "pch.h"
#include "Zone.h"
#include "Utils/Metrics.h"
#include "GameObject.h"
#include "Player.h"
#include "Monster.h"
#include "Projectile.h"
#include "HomingProjectile.h"
#include "SkillshotProjectile.h"
#include "PacketMaker.h"
#include "GameConstants.h"
#include "Network/Session.h"
#include "game.pb.h"


namespace
{
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

namespace
{
	void BumpTypeGauge(GameObjectType type, int delta)
	{
		switch (type)
		{
		case GameObjectType::Player:
			ServerMetrics::currentPlayers.fetch_add(delta, std::memory_order_relaxed); break;
		case GameObjectType::Monster:
			ServerMetrics::currentMonsters.fetch_add(delta, std::memory_order_relaxed); break;
		case GameObjectType::Projectile:
			ServerMetrics::currentProjectiles.fetch_add(delta, std::memory_order_relaxed); break;
		default:
			break;
		}
	}
}

void Zone::InsertObject(std::shared_ptr<GameObject> obj)
{
	const long long guid = obj->GetGuid();
	const GameObjectType type = obj->GetType();
	const bool isNew = (objects_.find(guid) == objects_.end());
	objects_[guid] = obj;
	objectsByType_[type][guid] = std::move(obj);
	if (isNew)
		BumpTypeGauge(type, +1);
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
	BumpTypeGauge(type, -1);
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
	Metrics::ScopedTimer _bcastTimer(ServerMetrics::broadcastUs);

	// Player/Monster 주기 방송을 하나의 S_WorldSnapshot 으로 묶는다.
	// Spawn/Leave/HP/SkillHit 같은 이산 이벤트는 여기 포함 안 됨 (개별 패킷 유지).
	snapshotAccum_ += deltaTime;
	if (snapshotAccum_ < GameConfig::Zone::SNAPSHOT_INTERVAL_SEC) return;
	snapshotAccum_ = 0.0f;

	Proto::S_UnitPositions snap;
	snap.set_server_tick(++snapshotTick_);

	// Player 위치는 클라 권위 — C_PlayerMove 마다 즉시 S_PlayerMove 로 브로드캐스트
	// 되므로 스냅샷에 포함하지 않는다. 서버 권위 강제 이동(돌진/넉백 등) 도입 시
	// ForcedMoveAgent::IsActive() 게이트로 다시 합칠 것.

	// Monster — 전체 포함 (수가 적고 AI 이동 빈도도 낮음).
	ForEachOfType(GameObjectType::Monster, [&](const long long guid, const std::shared_ptr<GameObject>& obj)
		{
			auto m = std::static_pointer_cast<Monster>(obj);
			auto* u = snap.add_units();
			u->set_guid(guid);
			*u->mutable_position() = m->GetPosition();
			u->set_yaw(0.0f);
		});

	if (snap.units_size() == 0) return;

	Broadcast(snap);
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
