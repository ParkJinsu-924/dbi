#pragma once

#include "ResourceManager.h"
#include "AttackTypes.h"
#include "GameConstants.h"


class MonsterTable;

struct MonsterTemplate
{
	using KeyType = int32;
	using Table = MonsterTable;

	int32       tid            = 0;
	std::string name;
	int32       hp             = 100;
	int32       maxHp          = 100;
	float       detectRange    = GameConfig::Monster::DEFAULT_DETECT_RANGE;
	float       leashRange     = GameConfig::Monster::DEFAULT_LEASH_RANGE;
	float       moveSpeed      = GameConfig::Monster::DEFAULT_MOVE_SPEED;
	// 몬스터가 쓸 스킬은 monster_skills.csv (MonsterSkillEntry) 에서 tid 로 조인.

	KeyType GetKey() const { return tid; }

	CSV_DEFINE_TYPE(MonsterTemplate,
		tid, name, hp, maxHp,
		detectRange, leashRange, moveSpeed)
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
