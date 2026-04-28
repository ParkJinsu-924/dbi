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
	// wind-up. 0 이면 즉발(현행 동작), >0 이면 S_SkillCastStart 후 cast_time 경과 후 ResolveHit.
	float       cast_time           = 0.0f;
	// 임팩트 후 follow-through 길이. cast_end_time = cast_time + recovery_time.
	// 이 시점부터 cooldown 이 돌기 시작하고 pendingCast 가 해제된다.
	// 0 이면 임팩트 직후 시전 완료 (recovery 없음).
	float       recovery_time       = 0.0f;

	// AI 행동 이름. "" 또는 "default" → DefaultAttackBehavior. 특수 스킬은 이 필드로 식별.
	std::string behaviorName;

	// 로드 후 SkillTable::OnLoaded 에서 주입. Runtime 에서는 항상 non-null.
	// mutable: 소비자(Find)가 const 뷰로 접근해도 OnLoaded 가 대입할 수 있어야 해서.
	//   **Runtime 에서 이 필드를 다시 쓰지 말 것** — OnLoaded 이후로는 read-only 로 간주.
	mutable std::shared_ptr<ISkillBehavior> behavior;

	KeyType GetKey() const { return sid; }

	CSV_DEFINE_TYPE(SkillTemplate,
		sid, name, targeting,
		projectile_speed, projectile_radius, projectile_range, projectile_lifetime,
		cooldown, cost, cast_range, cast_time, recovery_time, behaviorName)
};


class SkillTable : public KeyedResourceTable<SkillTemplate>
{
protected:
	void OnLoaded() override;   // 구현은 SkillTemplate.cpp (신규) — behavior 바인딩.
};
