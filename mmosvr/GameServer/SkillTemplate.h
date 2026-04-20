#pragma once

#include "ResourceManager.h"
#include "AttackTypes.h"
#include "SkillBehavior.h"
#include <memory>
#include <string>


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

	// AI 행동 이름. "" 또는 "default" → DefaultAttackBehavior. 특수 스킬은 이 필드로 식별.
	std::string behaviorName;

	// 로드 후 SkillTable::OnLoaded 에서 주입. Runtime 에서는 항상 non-null.
	// mutable: CSV 로드 후 SkillTable 이 채워 넣지만, 소비자는 const 뷰로 접근.
	mutable std::shared_ptr<ISkillBehavior> behavior;

	KeyType GetKey() const { return sid; }

	CSV_DEFINE_TYPE(SkillTemplate,
		sid, name, targeting,
		projectile_speed, projectile_radius, projectile_range, projectile_lifetime,
		cooldown, cost, cast_range, behaviorName)
};


class SkillTable : public KeyedResourceTable<SkillTemplate>
{
protected:
	void OnLoaded() override;   // 구현은 SkillTemplate.cpp (신규) — behavior 바인딩.
};
