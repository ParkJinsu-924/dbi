#include "pch.h"
#include "SkillExecution.h"

#include "SkillTemplate.h"
#include "Effect.h"
#include "SkillEffect.h"
#include "Zone.h"
#include "Unit.h"
#include "Agent/BuffAgent.h"
#include "PacketMaker.h"
#include "Utils/MathUtil.h"
#include "Utils/TimeManager.h"


// ===========================================================================
// 내부 헬퍼 — anonymous namespace 로 TU 외부에서 보이지 않도록 격리.
// ===========================================================================
namespace
{
	// 단일 Effect 디스패치: 즉발(Damage/Heal) 은 victim 에 바로 반영,
	// 지속(StatMod/CCState) 은 BuffAgent::Add 로 저장.
	// caster=nullptr 이면 TakeDamage 내 aggro 자동 누적이 생략된다 (환경 피해 등).
	void ApplyEffectToUnit(Unit& victim, const Effect& e, Unit* caster)
	{
		switch (e.type)
		{
		case EffectType::Damage:
			victim.TakeDamage(static_cast<int32>(e.magnitude), caster);
			break;

		case EffectType::Heal:
			victim.Heal(static_cast<int32>(e.magnitude));
			break;

		case EffectType::StatMod:
		case EffectType::CCState:
			victim.Get<BuffAgent>().Add(e, caster ? caster->GetGuid() : 0);
			break;

		default:
			break;
		}
	}

	// 주어진 스킬의 OnHit Damage effect magnitude 합.
	// S_SkillHit.damage 표시값 및 Projectile.damage_ 스냅샷에 사용.
	int32 ComputeOnHitDamage(int32 sid)
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
	void ApplyEffects(int32 sid, EffectTrigger trigger,
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

			ApplyEffectToUnit(*victim, *e, caster);
		}
	}

	// Instant 공격 (Melee / Hitscan) — OnCast + OnHit 적용 + S_SkillHit.
	// S_UnitHp 는 Unit::TakeDamage 내부에서 처리.
	void CastInstant(const SkillTemplate& skill,
	                 Unit& caster, Unit& target, Zone& zone)
	{
		ApplyEffects(skill.sid, EffectTrigger::OnCast, &caster, &target);
		SkillExecution::ResolveHit(&caster, target, skill.sid,
		                           caster.GetPosition(), target.GetPosition(), zone);
	}

	// Homing 발사 — OnCast (Self 버프 등) 즉발. 명중 시 OnHit 은 Projectile::ApplyHit 에서 처리.
	void CastHoming(Unit& caster, long long targetGuid,
	                const SkillTemplate& skill, Zone& zone)
	{
		ApplyEffects(skill.sid, EffectTrigger::OnCast, &caster, nullptr);
		const int32 dmg = ComputeOnHitDamage(skill.sid);
		zone.SpawnHomingProjectile(
			caster.GetGuid(), caster.GetType(), targetGuid,
			skill.sid, caster.GetPosition(),
			dmg, skill.projectile_speed, skill.projectile_lifetime);
	}
}


// ===========================================================================
// 공개 API
// ===========================================================================
namespace SkillExecution
{
	void ResolveHit(Unit* caster, Unit& target, int32 skillId,
	                const Proto::Vector2& casterPos,
	                const Proto::Vector2& hitPos,
	                Zone& zone)
	{
		ApplyEffects(skillId, EffectTrigger::OnHit, caster, &target);

		const int32 displayDmg = ComputeOnHitDamage(skillId);
		zone.Broadcast(PacketMaker::MakeSkillHit(
			caster ? caster->GetGuid() : 0, target.GetGuid(), skillId, displayDmg,
			casterPos, hitPos));
	}

	void CastSkillshot(Unit& caster, float dirX, float dirZ,
	                   const SkillTemplate& skill, Zone& zone)
	{
		ApplyEffects(skill.sid, EffectTrigger::OnCast, &caster, nullptr);
		const int32 dmg = ComputeOnHitDamage(skill.sid);
		zone.SpawnSkillshotProjectile(
			caster.GetGuid(), caster.GetType(),
			skill.sid, caster.GetPosition(), dirX, dirZ,
			dmg, skill.projectile_speed, skill.projectile_radius, skill.projectile_range);
	}

	void CastTargeted(const SkillTemplate& skill, Unit& caster, Unit& target, Zone& zone)
	{
		// 즉발 경로. wind-up (cast_time>0) 은 BeginTargetedCast 가 별도 진입점이므로
		// 여기서는 분기하지 않는다 — EngageState 가 cast_time 보고 분기한 결과를 받는다.
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
			const auto dir = MathUtil::NormalizedDir2D(caster.GetPosition(), target.GetPosition());
			CastSkillshot(caster, dir.x, dir.y, skill, zone);
			break;
		}
		}
	}

	void BeginTargetedCast(const SkillTemplate& skill, Unit& caster, Unit& target,
	                       Zone& zone, const float now, const float appliedCooldown)
	{
		// wind-up 진입. OnCast 효과(self-buff/cost 등)는 시전 *시작* 시점에 적용 — LoL/Albion 관습.
		// OnHit + S_SkillHit 은 Unit::TickCast → ResolveHit 가 cast_time 경과 시점에 처리.
		ApplyEffects(skill.sid, EffectTrigger::OnCast, &caster, &target);
		caster.BeginCast(skill, target, now, appliedCooldown);
		(void)zone;   // zone 은 caster.GetZone() 와 동일 — 인터페이스 일관성 위해 받지만 사용 안 함.
	}
}
