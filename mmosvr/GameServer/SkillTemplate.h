#pragma once

#include "ResourceManager.h"


class SkillTable;

struct SkillTemplate
{
	using KeyType = int32;
	using Table = SkillTable;

	int32       sid          = 0;
	std::string name;
	int32       kind         = 0;     // 0=Homing, 1=Skillshot (Proto::ProjectileKind 와 일치)
	float       speed        = 10.0f;
	float       radius       = 0.5f;  // Skillshot 충돌 반경
	float       range        = 0.0f;  // Skillshot 사거리 (구 maxRange — max/min 매크로 충돌 회피)
	float       lifetime     = 5.0f;  // Homing 안전장치   (구 maxLifetime)
	float       cooldown     = 1.0f;
	int32       damage       = 10;

	KeyType GetKey() const { return sid; }

	CSV_DEFINE_TYPE(SkillTemplate,
		sid, name, kind, speed, radius, range, lifetime, cooldown, damage)
};


class SkillTable : public KeyedResourceTable<SkillTemplate>
{
public:
	const SkillTemplate* FindByName(const std::string& name) const
	{
		for (const auto& [sid, t] : map_)
			if (t.name == name) return &t;
		return nullptr;
	}
};
