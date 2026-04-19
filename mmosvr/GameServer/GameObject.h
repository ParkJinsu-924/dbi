#pragma once

#include "common.pb.h"
#include "Utils/Types.h"
#include "Utils/MathUtil.h"
#include "Utils/ObjectGuidGenerator.h"
#include <string>


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
	GameObject(const GameObjectType type, const long long guid, std::string name = "")
		: type_(type)
		, guid_(guid)
		, name_(std::move(name))
	{
	}

	// Auto-allocated GUID (used by Monster, Npc)
	explicit GameObject(GameObjectType type, std::string name = "")
		: GameObject(type, GetObjectGuidGenerator().Generate(), std::move(name))
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

	// Zone
	int32 GetZoneId() const { return zoneId_; }
	void SetZoneId(int32 id) { zoneId_ = id; }

protected:
	const GameObjectType type_;
	const long long guid_;
	std::string name_;

	Proto::Vector2 position_;
	int32 zoneId_ = 0;
};
