#pragma once

#include "GameObject.h"


class Unit : public GameObject
{
public:
	explicit Unit(GameObjectType type, std::string name = "")
		: GameObject(type, std::move(name))
	{
	}

	Unit(GameObjectType type, long long guid, std::string name)
		: GameObject(type, guid, std::move(name))
	{
	}

	int32 GetHp() const { return hp_; }
	void SetHp(int32 hp) { hp_ = hp; }
	int32 GetMaxHp() const { return maxHp_; }
	void SetMaxHp(int32 maxHp) { maxHp_ = maxHp; }
	bool IsAlive() const { return hp_ > 0; }

	void TakeDamage(int32 amount) { hp_ = (std::max)(0, hp_ - amount); }
	void Heal(int32 amount) { hp_ = (std::min)(maxHp_, hp_ + amount); }

protected:
	int32 hp_ = 100;
	int32 maxHp_ = 100;
};
