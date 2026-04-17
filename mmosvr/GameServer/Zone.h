#pragma once

#include "Utils/Synchronized.h"
#include "Utils/Types.h"
#include "Network/SendBuffer.h"
#include "common.pb.h"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include "GameObject.h"
#include "Network/PacketSession.h"

class GameObject;
class Player;
class Monster;
class HomingProjectile;
class SkillshotProjectile;

class Zone
{
public:
	explicit Zone(int32 id) : id_(id) {}

	int32 GetId() const { return id_; }

	// Unified API for all GameObject types — 즉시 처리 (외부 스레드에서 안전)
	void Add(std::shared_ptr<GameObject> obj);
	void Remove(long long guid);
	std::shared_ptr<GameObject> Find(long long guid) const;

	template<typename T>
	std::shared_ptr<T> FindAs(long long guid) const
	{
		return std::dynamic_pointer_cast<T>(Find(guid));
	}

	template<typename T>
	std::vector<std::shared_ptr<T>> GetObjectsByType() const
	{
		return objects_.Read([](const auto& m)
			{
				std::vector<std::shared_ptr<T>> result;
				for (const auto& [guid, obj] : m)
					if (auto casted = std::dynamic_pointer_cast<T>(obj))
						result.push_back(std::move(casted));
				return result;
			});
	}

	// Tick all objects + broadcast monster positions periodically
	void Update(float deltaTime);

	// Broadcast a protobuf message to all Players in this zone
	template<typename T>
	void Broadcast(const T& msg)
	{
		BroadcastChunk(PacketSession::MakeSendBuffer(msg));
	}

	// Find the nearest alive Player within maxRange of the given position.
	// Returns nullptr if none found.
	std::shared_ptr<Player> FindNearestPlayer(const Proto::Vector3& from, float maxRange) const;

	// Find the nearest alive Monster within maxRange. Returns nullptr if none found.
	std::shared_ptr<Monster> FindNearestMonster(const Proto::Vector3& from, float maxRange) const;

	// Projectile factories. Spawn 은 pending 큐로 들어가고 다음 Flush 시점에 objects_ 등록 →
	// Zone::Update 의 read 락 안에서 호출해도 안전하다 (Monster.DoAttack, Skill 핸들러 등).
	// S_ProjectileSpawn 패킷은 즉시 브로드캐스트.
	std::shared_ptr<HomingProjectile> SpawnHomingProjectile(
		long long ownerGuid, GameObjectType ownerType, long long targetGuid,
		const Proto::Vector3& startPos,
		int32 damage, float speed, float lifetime);

	std::shared_ptr<SkillshotProjectile> SpawnSkillshotProjectile(
		long long ownerGuid, GameObjectType ownerType,
		const Proto::Vector3& startPos,
		float dirX, float dirZ,
		int32 damage, float speed, float radius, float range);

private:
	void BroadcastChunk(SendBufferChunkPtr chunk); // For Broadcast function, use Broadcast() instead.
	void BroadcastMonsterPositions();
	void FlushPending();              // pending Add/Remove 일괄 적용

	const int32 id_;
	Synchronized<std::unordered_map<long long, std::shared_ptr<GameObject>>, std::shared_mutex> objects_;

	// Update 중에 발생한 등록/소멸 요청을 한 틱 끝에서 일괄 반영.
	Synchronized<std::vector<std::shared_ptr<GameObject>>, std::mutex> pendingAdd_;
	Synchronized<std::vector<long long>, std::mutex> pendingRemove_;

	float monsterBroadcastAccum_ = 0.0f;
};