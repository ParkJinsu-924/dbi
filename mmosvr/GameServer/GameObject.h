#pragma once

#include "common.pb.h"
#include "Utils/Types.h"
#include "Utils/MathUtil.h"
#include "Utils/ObjectGuidGenerator.h"
#include <string>

class Zone;


enum class GameObjectType
{
	Player,
	Monster,
	Npc,
	Projectile,
};

class GameObject
{
public:
	// Explicit GUID (used by Player — GUID distinct from playerId)
	GameObject(const GameObjectType type, Zone& zone, const long long guid, std::string name = "")
		: type_(type)
		, guid_(guid)
		, name_(std::move(name))
		, zone_(zone)
	{
	}

	// Auto-allocated GUID (used by Monster, Npc, Projectile)
	GameObject(GameObjectType type, Zone& zone, std::string name = "")
		: GameObject(type, zone, GetObjectGuidGenerator().Generate(), std::move(name))
	{
	}

	virtual ~GameObject() = default;
	virtual void Update(float /*deltaTime*/) {}

	GameObject(const GameObject&) = delete;
	GameObject& operator=(const GameObject&) = delete;

	// Identity
	GameObjectType GetType() const { return type_; }
	long long GetGuid() const { return guid_; }
	const std::string& GetName() const { return name_; }

	// Transform
	const Proto::Vector2& GetPosition() const { return position_; }
	void SetPosition(const Proto::Vector2& p) { position_ = p; }

	// Distance (XZ 평면). Sq 버전은 sqrt 를 피해 범위 비교/정렬 용도.
	float DistanceTo(const Proto::Vector2& point) const
	{
		return MathUtil::Distance2D(position_, point);
	}
	float DistanceTo(const GameObject& other) const
	{
		return MathUtil::Distance2D(position_, other.position_);
	}
	float DistanceToSq(const Proto::Vector2& point) const
	{
		return MathUtil::Distance2DSq(position_, point);
	}
	float DistanceToSq(const GameObject& other) const
	{
		return MathUtil::Distance2DSq(position_, other.position_);
	}

	// Zone — 모든 GameObject 는 ctor 에서 Zone 에 바인딩되고, 그 zone 에서 수명 동안 고정.
	// Zone 간 이동은 "객체 재생성" 모델로 처리 (현 zone 에서 Remove → 새 zone 에서 Create).
	Zone& GetZone() const { return zone_; }
	int32 GetZoneId() const;   // zone_.GetId()  (정의는 GameObject.cpp)

protected:
	const GameObjectType type_;
	const long long guid_;
	std::string name_;

	Proto::Vector2 position_;
	Zone& zone_;   // ctor 에서 바인딩, 재할당 불가
};
