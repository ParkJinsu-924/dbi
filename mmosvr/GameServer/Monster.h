#pragma once

#include "GameObject.h"


class Monster : public GameObject
{
public:
	explicit Monster(std::string name = "")
		: GameObject(GameObjectType::Monster, std::move(name))
	{
	}

	int32 GetHp() const { return hp_; }
	void SetHp(int32 hp) { hp_ = hp; }

	// --- Test movement: walk in a circle around a fixed center ---
	void InitCircularMovement(const Proto::Vector3& center,
		float radius, float angularSpeedRad, float startAngleRad = 0.0f);
	void UpdateMovement(float deltaTime);

private:
	int32 hp_ = 100;

	// Circular movement state
	Proto::Vector3 center_;
	float radius_ = 0.0f;
	float angularSpeed_ = 0.0f;  // radians per second
	float angle_ = 0.0f;
};
