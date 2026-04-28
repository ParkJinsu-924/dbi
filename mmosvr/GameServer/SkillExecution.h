#pragma once

// 스킬 한 번의 실행 과정을 담당하는 진입점. Player(GamePacketHandler) /
// Monster(EngageState) / Projectile(명중 처리) 가 공통 경유.
//
// 외부에 공개되는 진입점은 아래 4가지뿐. effect 적용/데미지 계산/공격 종류별
// 분기 같은 내부 헬퍼는 .cpp 의 anonymous namespace 에 격리되어 외부에서 호출 불가.
//
//   CastTargeted        — 즉발 (cast_time=0) Targeted 시전. Player Targeted / 즉발 Monster.
//                         Skillshot 분기는 caster→target 벡터로 방향 자동 계산.
//   CastSkillshot       — target 객체 없이 dir 만 있는 케이스 (Player C_UseSkill 의 Skillshot 입력).
//   BeginTargetedCast   — wind-up (cast_time>0) Targeted 시전 시작. OnCast 적용 + Unit::BeginCast.
//                         실제 임팩트(OnHit + S_SkillHit) 는 Unit::TickCast 가 cast_time 후 호출.
//   ResolveHit          — Projectile/wind-up cast 명중 시점 OnHit 적용 + S_SkillHit 방송.

#include "Utils/Types.h"
#include "common.pb.h"


class Unit;
class Zone;
struct SkillTemplate;


namespace SkillExecution
{
	void CastTargeted(const SkillTemplate& skill, Unit& caster, Unit& target, Zone& zone);

	void CastSkillshot(Unit& caster, float dirX, float dirZ,
	                   const SkillTemplate& skill, Zone& zone);

	// wind-up 진입. OnCast 효과(self-buff 등) 즉시 적용 + caster.BeginCast 로 pendingCast 세팅.
	// appliedCooldown 은 cast 완료/cancel 시 SkillCooldownAgent.MarkUsed 에 사용.
	void BeginTargetedCast(const SkillTemplate& skill, Unit& caster, Unit& target,
	                       Zone& zone, float now, float appliedCooldown);

	void ResolveHit(Unit* caster, Unit& target, int32 skillId,
	                const Proto::Vector2& casterPos,
	                const Proto::Vector2& hitPos,
	                Zone& zone);
}
