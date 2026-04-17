#pragma once

#include "ResourceManager.h"
#include "AttackTypes.h"


class MonsterTable;

struct MonsterTemplate
{
	using KeyType = int32;
	using Table = MonsterTable;

	int32       tid            = 0;
	std::string name;
	int32       hp             = 100;
	int32       maxHp          = 100;
	float       detectRange    = 10.0f;
	float       attackRange    = 2.0f;
	float       leashRange     = 15.0f;
	float       moveSpeed      = 3.0f;
	float       attackCooldown = 1.5f;
	int32       attackDamage   = 10;
	AttackType  attackType     = AttackType::Melee;
	int32       skillId        = 0;    // attackType=Homing/Skillshot 일 때 SkillTemplate.sid (0=미사용)

	KeyType GetKey() const { return tid; }

	CSV_DEFINE_TYPE(MonsterTemplate,
		tid, name, hp, maxHp,
		detectRange, attackRange, leashRange,
		moveSpeed, attackCooldown, attackDamage, attackType, skillId)
};


class MonsterTable : public KeyedResourceTable<MonsterTemplate>
{
public:
	const MonsterTemplate* FindByName(const std::string& name) const
	{
		for (const auto& [tid, t] : map_)
			if (t.name == name) return &t;
		return nullptr;
	}

	std::vector<const MonsterTemplate*> FindAll(auto predicate) const
	{
		std::vector<const MonsterTemplate*> result;
		for (const auto& [tid, t] : map_)
			if (predicate(t)) result.push_back(&t);
		return result;
	}
};
