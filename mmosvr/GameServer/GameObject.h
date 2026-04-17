#pragma once

#include "common.pb.h"
#include "Utils/Types.h"
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
	const Proto::Vector3& GetPosition() const { return position_; }
	void SetPosition(const Proto::Vector3& p) { position_ = p; }

	// Zone
	int32 GetZoneId() const { return zoneId_; }
	void SetZoneId(int32 id) { zoneId_ = id; }

protected:
	const GameObjectType type_;
	const long long guid_;
	std::string name_;

	Proto::Vector3 position_;
	int32 zoneId_ = 0;
};
