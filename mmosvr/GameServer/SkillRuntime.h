#pragma once

// 스킬 실행 진입점. Player(GamePacketHandler) / Monster.DoAttack 양쪽이 공통 경유.
// Phase 1: Melee / Hitscan / Homing / Skillshot 네 가지 kind 를 Cast() 하나로 디스패치.
// Phase 2: OnCast/OnTick 트리거, Buff 적용, scaling 스탯 계산 확장.
// Phase 3: Lua 훅 포인트 — 스킬별 on_cast/on_hit 스크립트 호출점은 여기서 디스패치.

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
	// 주어진 스킬의 OnHit 트리거 Damage effect magnitude 합.
	// Phase 2: scaling_ad/ap 에 시전자 스탯을 곱해 합산 (현재는 base magnitude 만).
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

	// --- Instant 공격 (Melee / Hitscan) ---
	// 즉시 데미지 + S_SkillHit 브로드캐스트. HP 가 바뀌면 S_UnitHp 도 함께.
	inline void CastInstant(
		const SkillTemplate& skill,
		Unit& caster, Unit& target, Zone& zone)
	{
		const int32 dmg = ComputeOnHitDamage(skill.sid);
		const int32 hpBefore = target.GetHp();
		if (dmg > 0)
			target.TakeDamage(dmg);

		zone.Broadcast(PacketMaker::MakeSkillHit(
			caster.GetGuid(), target.GetGuid(), skill.sid, dmg,
			caster.GetPosition(), target.GetPosition()));

		if (target.GetHp() != hpBefore)
			zone.Broadcast(PacketMaker::MakeUnitHp(target));
	}

	// --- Homing 발사 ---
	inline void CastHoming(
		long long casterGuid, GameObjectType casterType,
		const Proto::Vector3& casterPos,
		long long targetGuid,
		const SkillTemplate& skill,
		Zone& zone)
	{
		const int32 dmg = ComputeOnHitDamage(skill.sid);
		zone.SpawnHomingProjectile(
			casterGuid, casterType, targetGuid,
			skill.sid, casterPos,
			dmg, skill.projectile_speed, skill.projectile_lifetime);
	}

	// --- Skillshot 발사 ---
	inline void CastSkillshot(
		long long casterGuid, GameObjectType casterType,
		const Proto::Vector3& casterPos,
		float dirX, float dirZ,
		const SkillTemplate& skill,
		Zone& zone)
	{
		const int32 dmg = ComputeOnHitDamage(skill.sid);
		zone.SpawnSkillshotProjectile(
			casterGuid, casterType,
			skill.sid, casterPos, dirX, dirZ,
			dmg, skill.projectile_speed, skill.projectile_radius, skill.projectile_range);
	}

	// 범용 디스패처 — Monster 평타처럼 "대상 지정된 스킬" 에 쓴다.
	// Skillshot 은 방향이 필요하므로 caster→target 벡터로 자동 계산 (평타/AI 용).
	inline void Cast(const SkillTemplate& skill, Unit& caster, Unit& target, Zone& zone)
	{
		switch (skill.targeting)
		{
		case SkillKind::Melee:
		case SkillKind::Hitscan:
			CastInstant(skill, caster, target, zone);
			break;

		case SkillKind::Homing:
			CastHoming(caster.GetGuid(), caster.GetType(), caster.GetPosition(),
			           target.GetGuid(), skill, zone);
			break;

		case SkillKind::Skillshot:
		{
			float dx = target.GetPosition().x() - caster.GetPosition().x();
			float dz = target.GetPosition().z() - caster.GetPosition().z();
			const float len = std::sqrt(dx * dx + dz * dz);
			if (len > 1e-4f) { dx /= len; dz /= len; }
			else             { dx = 1.0f; dz = 0.0f; }
			CastSkillshot(caster.GetGuid(), caster.GetType(), caster.GetPosition(),
			              dx, dz, skill, zone);
			break;
		}
		}
	}
}
