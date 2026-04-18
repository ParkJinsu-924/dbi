#pragma once

#include "ResourceManager.h"


// ─────────────────────────────────────────────────────────────────────
// Effect — "타겟에게 일어나는 일" 을 데이터로 1급 표현.
//
// Phase 1 : Damage 만 실제 구현.
// Phase 2+: Heal/ApplyBuff/Dash/Summon 을 런타임에 분기 처리.
//           동일한 Effect 레코드를 여러 스킬이 공유 (재사용성).
// ─────────────────────────────────────────────────────────────────────

enum class EffectType : int32
{
	Damage     = 0,   // Phase 1
	Heal       = 1,   // Phase 2
	ApplyBuff  = 2,   // Phase 2 — magnitude=효과값, duration=지속시간
	Dash       = 3,   // Phase 2
	Summon     = 4,   // Phase 2
};


struct Effect
{
	using KeyType = int32;

	int32       eid         = 0;
	EffectType  type        = EffectType::Damage;
	float       magnitude   = 0.0f;   // type 별 의미: Damage=dmg, Heal=heal, Buff=효과크기
	float       duration    = 0.0f;   // Buff 류의 지속시간(초)
	float       scaling_ad  = 0.0f;   // Phase 2 — 시전자 공격력 계수
	float       scaling_ap  = 0.0f;   // Phase 2 — 시전자 마법력 계수

	KeyType GetKey() const { return eid; }

	CSV_DEFINE_TYPE(Effect,
		eid, type, magnitude, duration, scaling_ad, scaling_ap)
};
