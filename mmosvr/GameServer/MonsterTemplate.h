#pragma once

#include "ResourceManager.h"
#include "AttackTypes.h"
#include "SkillTemplate.h"


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
		auto it = nameIndex_.find(name);
		return it != nameIndex_.end() ? it->second : nullptr;
	}

	std::vector<const MonsterTemplate*> FindAll(auto predicate) const
	{
		std::vector<const MonsterTemplate*> result;
		for (const auto& [tid, t] : map_)
			if (predicate(t)) result.push_back(&t);
		return result;
	}

	int OnValidate() const override
	{
		int errors = 0;
		const auto* skills = GetResourceManager().Get<SkillTemplate>();
		if (!skills) return 0;

		for (const auto& [tid, m] : map_)
		{
			if (m.skillId != 0 && !skills->Find(m.skillId))
			{
				LOG_ERROR(std::format(
					"monster_templates: tid={} ({}) references non-existent skillId={}",
					tid, m.name, m.skillId));
				++errors;
			}
		}
		return errors;
	}

	const char* DebugName() const override { return "monster_templates"; }

protected:
	void OnLoaded() override
	{
		nameIndex_.clear();
		nameIndex_.reserve(map_.size());
		for (const auto& [tid, t] : map_)
			nameIndex_[t.name] = &t;
	}

private:
	std::unordered_map<std::string, const MonsterTemplate*> nameIndex_;
};
