#include "pch.h"
#include "Unit.h"
#include "Effect.h"
#include <algorithm>


void Unit::ApplyEffect(const Effect& e, Unit& caster)
{
	switch (e.type)
	{
	case EffectType::Damage:
		TakeDamage(static_cast<int32>(e.magnitude));
		break;

	case EffectType::Heal:
		Heal(static_cast<int32>(e.magnitude));
		break;

	case EffectType::StatMod:
	case EffectType::CCState:
		// 지속 효과 — BuffContainer 가 refresh/broadcast 를 책임진다.
		buffs_.Add(e, caster.GetGuid());
		break;

	default:
		// Dash/Summon 등은 Phase 2+.
		break;
	}
}

float Unit::GetEffectiveMoveSpeed(const float baseSpeed) const
{
	float flat = 0.0f, pct = 0.0f;
	buffs_.GetStatModifier(StatType::MoveSpeed, flat, pct);
	return (std::max)(0.0f, baseSpeed * (1.0f + pct) + flat);
}
