#pragma once

#include "Utils/Types.h"
#include "Network/SendBuffer.h"
#include "common.pb.h"
#include <unordered_map>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>
#include "GameObject.h"
#include "Network/PacketSession.h"

class GameObject;
class Player;
class Monster;
class Npc;
class Projectile;
class HomingProjectile;
class SkillshotProjectile;


// T -> GameObjectType 컴파일 타임 매핑. 새 파생 타입 추가 시 여기에 특수화 한 줄만 추가.
// GetObjectsByType<T> / FindAs<T> / 기타 T 기반 조회가 런타임 분기 없이 해당 bucket 만 본다.
template <typename T> struct GameObjectTraits;
template <> struct GameObjectTraits<Player>     { static constexpr GameObjectType kType = GameObjectType::Player; };
template <> struct GameObjectTraits<Monster>    { static constexpr GameObjectType kType = GameObjectType::Monster; };
template <> struct GameObjectTraits<Npc>        { static constexpr GameObjectType kType = GameObjectType::Npc; };
template <> struct GameObjectTraits<Projectile> { static constexpr GameObjectType kType = GameObjectType::Projectile; };


class Zone
{
public:
	explicit Zone(const int32 id) : zoneId_(id) {}

	int32 GetId() const { return zoneId_; }

	// Unified API for all GameObject types.
	void Add(std::shared_ptr<GameObject> obj);
	void Remove(long long guid);
	std::shared_ptr<GameObject> Find(long long guid) const;

	template<typename T>
	std::shared_ptr<T> FindAs(long long guid) const
	{
		return std::dynamic_pointer_cast<T>(Find(guid));
	}

	// GameObjectTraits<T>::kType bucket 만 순회. 새 타입은 상단 traits 특수화 1줄로 추가.
	template<typename T>
	std::vector<std::shared_ptr<T>> GetObjectsByType() const
	{
		constexpr GameObjectType type = GameObjectTraits<T>::kType;

		std::vector<std::shared_ptr<T>> result;
		const auto it = objectsByType_.find(type);
		if (it == objectsByType_.end())
			return result;
		result.reserve(it->second.size());
		for (const auto& kv : it->second)
			result.push_back(std::static_pointer_cast<T>(kv.second));
		return result;
	}

	// Iterate all objects of one type without allocating (hot path: broadcast/find).
	template<typename Func>
	void ForEachOfType(const GameObjectType type, Func&& fn) const
	{
		const auto it = objectsByType_.find(type);
		if (it == objectsByType_.end()) return;
		for (const auto& [guid, obj] : it->second)
			fn(guid, obj);
	}

	// Tick all objects + broadcast monster positions periodically
	void Update(float deltaTime);

	// Broadcast a protobuf message to all Players in this zone
	template<typename T>
	void Broadcast(const T& msg)
	{
		BroadcastChunk(PacketSession::MakeSendBuffer(msg));
	}

	// Broadcast to all Players in the zone except the one with `excludeGuid`.
	// 신규 입장 알림처럼 "본인은 이미 자기 정보를 알고 있는" 경우에 사용.
	template<typename T>
	void BroadcastExcept(const T& msg, long long excludeGuid)
	{
		BroadcastChunkExcept(PacketSession::MakeSendBuffer(msg), excludeGuid);
	}

	// Send to a single Player by guid (1:1). guid 가 Player 가 아니거나 세션이 끊겼으면 무시.
	template<typename T>
	void SendTo(const T& msg, long long guid)
	{
		SendChunkTo(PacketSession::MakeSendBuffer(msg), guid);
	}

	// Broadcast to a specific list of Players (파티/어그로 대상자 등).
	// guids 내 중복/비-Player/연결 끊긴 세션은 자동 스킵. SendBuffer 는 1회만 직렬화되어 공유됨.
	template<typename T>
	void BroadcastTo(const T& msg, std::span<const long long> guids)
	{
		BroadcastChunkTo(PacketSession::MakeSendBuffer(msg), guids);
	}

	// GameObjectTraits<T>::kType bucket 에서 from 기준 maxRange 이내 가장 가까운 살아있는 객체 반환.
	// T 는 IsAlive() 를 가진 Unit 계열 (Player/Monster/Npc 등). 없으면 nullptr.
	template<typename T>
	std::shared_ptr<T> FindNearest(const Proto::Vector2& from, const float maxRange) const
	{
		std::shared_ptr<T> nearest;
		float nearestDistSq = maxRange * maxRange;

		ForEachOfType(GameObjectTraits<T>::kType, [&](long long /*guid*/, const std::shared_ptr<GameObject>& obj)
			{
				auto t = std::static_pointer_cast<T>(obj);
				if (!t->IsAlive()) return;

				const float distSq = obj->DistanceToSq(from);
				if (distSq < nearestDistSq)
				{
					nearestDistSq = distSq;
					nearest = t;
				}
			});

		return nearest;
	}

	// Projectile factories. Spawn 은 pending 큐로 들어가고 다음 Flush 시점에 objects_ 등록 →
	// Zone::Update 의 read 락 안에서 호출해도 안전하다 (Monster skill behavior, Skill 핸들러 등).
	// S_ProjectileSpawn 패킷은 즉시 브로드캐스트.
	std::shared_ptr<HomingProjectile> SpawnHomingProjectile(
		long long ownerGuid, GameObjectType ownerType, long long targetGuid,
		int32 skillId, const Proto::Vector2& startPos,
		int32 damage, float speed, float lifetime);

	std::shared_ptr<SkillshotProjectile> SpawnSkillshotProjectile(
		long long ownerGuid, GameObjectType ownerType,
		int32 skillId, const Proto::Vector2& startPos,
		float dirX, float dirZ,
		int32 damage, float speed, float radius, float range);

private:
	void BroadcastChunk(const SendBufferChunkPtr& chunk) const; // For Broadcast function, use Broadcast() instead.
	void BroadcastChunkExcept(const SendBufferChunkPtr& chunk, long long excludeGuid) const;
	void SendChunkTo(const SendBufferChunkPtr& chunk, long long guid) const;
	void BroadcastChunkTo(const SendBufferChunkPtr& chunk, std::span<const long long> guids) const;
	void CollectDeadObjects();                // consumed projectile 등 제거 대상 pendingRemove_ 에 수집
	void FlushPendingObjects();               // pending Add/Remove 일괄 적용
	void BroadcastObjectPositions(float deltaTime);

	void InsertObject(std::shared_ptr<GameObject> obj);  // objects_ + objectsByType_ 동시 갱신
	void EraseObject(long long guid);

	const int32 zoneId_;

	// guid 직접 검색용 (Find / FindAs) — O(1).
	std::unordered_map<long long, std::shared_ptr<GameObject>> objects_;

	// 타입별 순회용 (Broadcast / FindNearest / Projectile cleanup) — O(N_type).
	// 같은 객체를 두 자료구조가 공유 (control block 공유, 추가 비용은 포인터 사이즈 정도).
	std::unordered_map<GameObjectType,
		std::unordered_map<long long, std::shared_ptr<GameObject>>> objectsByType_;

	// Zone 은 단일 GameLoopThread 에서만 접근되므로 락 없음.
	// Update 순회 중 objects_ 직접 수정 시 iterator invalidation 방지용 pending 큐.
	std::vector<std::shared_ptr<GameObject>> pendingAdd_;
	std::vector<long long>                   pendingRemove_;

	// 누적 delta 가 SNAPSHOT_INTERVAL_SEC 넘으면 S_WorldSnapshot 1회 송신.
	float    snapshotAccum_ = 0.0f;
	uint32_t snapshotTick_  = 0;
};