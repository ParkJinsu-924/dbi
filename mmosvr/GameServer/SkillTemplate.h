#pragma once

#include "ResourceManager.h"
#include "AttackTypes.h"


// Phase 1: 스킬 메타데이터만 보관. 데미지/버프 등의 "효과" 는 Effect + SkillEffectEntry 에 위임.
// targeting 은 SkillKind enum 값 재사용 (Homing=0, Skillshot=1 — Proto::ProjectileKind 와 값 일치 유지).

class SkillTable;

struct SkillTemplate
{
	using KeyType = int32;
	using Table = SkillTable;

	int32       sid                 = 0;
	std::string name;
	SkillKind   targeting           = SkillKind::Homing;
	float       projectile_speed    = 10.0f;
	float       projectile_radius   = 0.5f;   // Skillshot 충돌 반경
	float       projectile_range    = 0.0f;   // Skillshot 사거리
	float       projectile_lifetime = 5.0f;   // Homing 생존시간 안전장치
	float       cooldown            = 1.0f;
	int32       cost                = 0;      // 자원 소모 (Phase 2+)
	float       cast_range          = 30.0f;  // 서버측 사거리 validation (Phase 2+)

	KeyType GetKey() const { return sid; }

	CSV_DEFINE_TYPE(SkillTemplate,
		sid, name, targeting,
		projectile_speed, projectile_radius, projectile_range, projectile_lifetime,
		cooldown, cost, cast_range)
};


class SkillTable : public KeyedResourceTable<SkillTemplate>
{
public:
	const SkillTemplate* FindByName(const std::string& name) const
	{
		auto it = nameIndex_.find(name);
		return it != nameIndex_.end() ? it->second : nullptr;
	}

protected:
	// 로드 후 name→SkillTemplate* 역인덱스 구축. map_ 은 이후 수정되지 않으므로 포인터 안정.
	void OnLoaded() override
	{
		nameIndex_.clear();
		nameIndex_.reserve(map_.size());
		for (const auto& [sid, t] : map_)
			nameIndex_[t.name] = &t;
	}

private:
	std::unordered_map<std::string, const SkillTemplate*> nameIndex_;
};
