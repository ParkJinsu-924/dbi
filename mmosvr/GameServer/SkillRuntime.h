#pragma once

// Phase 1 스킬 실행 진입점. Player(GamePacketHandler) / Monster.DoAttack 양쪽이 공통 경유.
// Phase 2: OnCast/OnTick 트리거, Buff 적용, scaling 스탯 계산 확장.
// Phase 3: Lua 훅 포인트 — 스킬별 on_cast/on_hit 스크립트 호출점은 여기서 디스패치.

#include "Utils/Types.h"
#include "SkillTemplate.h"
#include "Effect.h"
#include "SkillEffect.h"
#include "Zone.h"

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

	// Homing 발사. damageOverride > 0 이면 effect 계산 무시하고 override 사용.
	inline void CastHoming(
		long long casterGuid, GameObjectType casterType,
		const Proto::Vector3& casterPos,
		long long targetGuid,
		const SkillTemplate& skill,
		Zone& zone,
		int32 damageOverride = 0)
	{
		const int32 dmg = damageOverride > 0 ? damageOverride : ComputeOnHitDamage(skill.sid);
		zone.SpawnHomingProjectile(
			casterGuid, casterType, targetGuid, casterPos,
			dmg, skill.projectile_speed, skill.projectile_lifetime);
	}

	// Skillshot 발사. 방향(dirX, dirZ)는 호출측에서 정규화 완료된 값을 넘길 것.
	inline void CastSkillshot(
		long long casterGuid, GameObjectType casterType,
		const Proto::Vector3& casterPos,
		float dirX, float dirZ,
		const SkillTemplate& skill,
		Zone& zone,
		int32 damageOverride = 0)
	{
		const int32 dmg = damageOverride > 0 ? damageOverride : ComputeOnHitDamage(skill.sid);
		zone.SpawnSkillshotProjectile(
			casterGuid, casterType, casterPos, dirX, dirZ,
			dmg, skill.projectile_speed, skill.projectile_radius, skill.projectile_range);
	}
}
