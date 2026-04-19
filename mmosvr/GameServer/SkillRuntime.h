#pragma once

// 스킬 실행 진입점. Player(GamePacketHandler) / Monster.DoAttack 양쪽이 공통 경유.
// Phase 1: Melee / Hitscan / Homing / Skillshot 네 가지 kind 를 Cast() 하나로 디스패치.
//          모든 SkillEffect (Damage/Heal/StatMod/CCState) 가 OnCast/OnHit 트리거 시점에
//          ApplyEffects() 를 통해 target/self 에 부착된다.
// Phase 2: OnTick 트리거, scaling 스탯 계산 확장.
// Phase 3: Lua 훅 포인트.

#include "Utils/Types.h"
#include "SkillTemplate.h"
#include "Effect.h"
#include "SkillEffect.h"
#include "Zone.h"
#include "Unit.h"
#include "PacketMaker.h"

#include <cmath>


namespace SkillRuntime
{
	// 주어진 스킬의 OnHit Damage effect magnitude 합.
	// S_SkillHit.damage 표시값 및 Projectile.damage_ 스냅샷에 사용.
	inline int32 ComputeOnHitDamage(int32 sid)
	{
		const auto* effectTable      = GetResourceManager().Get<Effect>();
		const auto* skillEffectTable = GetResourceManager().Get<SkillEffectEntry>();
		if (!effectTable || !skillEffectTable)
			return 0;

		int32 total = 0;
		for (const auto* se : skillEffectTable->FindBySkill(sid))
		{
			if (se->trigger != EffectTrigger::OnHit)
				continue;
			const Effect* e = effectTable->Find(se->eid);
			if (!e || e->type != EffectType::Damage)
				continue;
			total += static_cast<int32>(e->magnitude);
		}
		return total;
	}

	// 주어진 trigger 의 SkillEffect 들을 target 구분에 맞춰 Unit 에 적용한다.
	// target=Self  → caster
	// target=Enemy → primaryTarget (상대)
	// target=Ally  → Phase 2+
	inline void ApplyEffects(int32 sid, EffectTrigger trigger,
	                         Unit* caster, Unit* primaryTarget)
	{
		const auto* skEffTable = GetResourceManager().Get<SkillEffectEntry>();
		const auto* effectTable = GetResourceManager().Get<Effect>();
		if (!skEffTable || !effectTable)
			return;

		for (const auto* se : skEffTable->FindBySkill(sid))
		{
			if (se->trigger != trigger)
				continue;
			const Effect* e = effectTable->Find(se->eid);
			if (!e) continue;

			Unit* victim = nullptr;
			switch (se->target)
			{
			case EffectTargetScope::Self:  victim = caster;        break;
			case EffectTargetScope::Enemy: victim = primaryTarget; break;
			case EffectTargetScope::Ally:  victim = nullptr;       break;   // Phase 2+
			}
			if (!victim) continue;

			Unit& casterRef = caster ? *caster : *victim;
			victim->ApplyEffect(*e, casterRef);
		}
	}

	// --- Instant 공격 (Melee / Hitscan) ---
	// OnCast + OnHit 효과 적용 + S_SkillHit + S_UnitHp. HP 0 이 되어도 S_UnitHp 는 보내 동기화.
	inline void CastInstant(const SkillTemplate& skill,
	                        Unit& caster, Unit& target, Zone& zone)
	{
		const int32 dmg = ComputeOnHitDamage(skill.sid);
		const int32 hpBefore = target.GetHp();

		ApplyEffects(skill.sid, EffectTrigger::OnCast, &caster, &target);
		ApplyEffects(skill.sid, EffectTrigger::OnHit,  &caster, &target);

		zone.Broadcast(PacketMaker::MakeSkillHit(
			caster.GetGuid(), target.GetGuid(), skill.sid, dmg,
			caster.GetPosition(), target.GetPosition()));

		if (target.GetHp() != hpBefore)
			zone.Broadcast(PacketMaker::MakeUnitHp(target));
	}

	// --- Homing 발사 ---
	// OnCast (Self 버프 등) 즉발. 명중 시 OnHit 은 Projectile::ApplyHit 에서 처리.
	inline void CastHoming(Unit& caster, long long targetGuid,
	                       const SkillTemplate& skill, Zone& zone)
	{
		ApplyEffects(skill.sid, EffectTrigger::OnCast, &caster, nullptr);
		const int32 dmg = ComputeOnHitDamage(skill.sid);
		zone.SpawnHomingProjectile(
			caster.GetGuid(), caster.GetType(), targetGuid,
			skill.sid, caster.GetPosition(),
			dmg, skill.projectile_speed, skill.projectile_lifetime);
	}

	// --- Skillshot 발사 ---
	inline void CastSkillshot(Unit& caster, float dirX, float dirZ,
	                          const SkillTemplate& skill, Zone& zone)
	{
		ApplyEffects(skill.sid, EffectTrigger::OnCast, &caster, nullptr);
		const int32 dmg = ComputeOnHitDamage(skill.sid);
		zone.SpawnSkillshotProjectile(
			caster.GetGuid(), caster.GetType(),
			skill.sid, caster.GetPosition(), dirX, dirZ,
			dmg, skill.projectile_speed, skill.projectile_radius, skill.projectile_range);
	}

	// 범용 디스패처 — Monster 평타처럼 "대상 지정된 스킬" 에 쓴다.
	// Skillshot 은 caster→target 벡터로 방향 자동 계산.
	inline void Cast(const SkillTemplate& skill, Unit& caster, Unit& target, Zone& zone)
	{
		switch (skill.targeting)
		{
		case SkillKind::Melee:
		case SkillKind::Hitscan:
			CastInstant(skill, caster, target, zone);
			break;

		case SkillKind::Homing:
			CastHoming(caster, target.GetGuid(), skill, zone);
			break;

		case SkillKind::Skillshot:
		{
			float dx = target.GetPosition().x() - caster.GetPosition().x();
			float dz = target.GetPosition().z() - caster.GetPosition().z();
			const float len = std::sqrt(dx * dx + dz * dz);
			if (len > 1e-4f) { dx /= len; dz /= len; }
			else             { dx = 1.0f; dz = 0.0f; }
			CastSkillshot(caster, dx, dz, skill, zone);
			break;
		}
		}
	}
}
