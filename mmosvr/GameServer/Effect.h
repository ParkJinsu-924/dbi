#pragma once

#include "ResourceManager.h"
#include "AttackTypes.h"


// ─────────────────────────────────────────────────────────────────────
// Effect — "타겟에게 일어나는 일" 을 데이터로 1급 표현.
//
// Phase 1 : Damage / Heal / StatMod / CCState 지원.
//           duration > 0 && (StatMod || CCState) 이면 Buff 로 간주되어
//           BuffContainer 에 부착되고 Tick 에 의해 만료 제거된다.
// Phase 2+: Dash/Summon + 스택 정책 + 이벤트 훅.
// ─────────────────────────────────────────────────────────────────────

enum class EffectType : int32
{
	Damage     = 0,
	Heal       = 1,
	StatMod    = 2,   // stat 컬럼이 가리키는 스탯을 magnitude 만큼 수정 (is_percent 로 %vs flat)
	CCState    = 3,   // 스탯 변화 없이 cc_flag 만 부여 (Stun/Silence/Root/Invulnerable)
	Dash       = 4,   // Phase 2
	Summon     = 5,   // Phase 2
};


struct Effect
{
	using KeyType = int32;

	int32       eid         = 0;
	EffectType  type        = EffectType::Damage;
	float       magnitude   = 0.0f;   // Damage=dmg, Heal=hp, StatMod=수치(+/-), CCState=0
	float       duration    = 0.0f;   // 지속시간(초). 0 = 즉발.
	float       scaling_ad  = 0.0f;   // Phase 2 — 시전자 공격력 계수
	float       scaling_ap  = 0.0f;   // Phase 2 — 시전자 마법력 계수
	StatType    stat        = StatType::None;    // StatMod 전용
	bool        is_percent  = false;              // StatMod 에서 magnitude 해석 (true = %, false = flat)
	CCFlag      cc_flag     = CCFlag::None;       // StatMod(Slow 카테고리) / CCState 에서 부여

	KeyType GetKey() const { return eid; }

	// duration > 0 이면서 StatMod 또는 CCState 이면 지속 효과 (Buff).
	bool IsBuff() const
	{
		return duration > 0.0f && (type == EffectType::StatMod || type == EffectType::CCState);
	}

	CSV_DEFINE_TYPE(Effect,
		eid, type, magnitude, duration, scaling_ad, scaling_ap,
		stat, is_percent, cc_flag)
};
