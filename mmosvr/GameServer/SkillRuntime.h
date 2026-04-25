#pragma once

// 스킬 실행 진입점. Player(GamePacketHandler) / Monster(DefaultAttackBehavior) /
// Projectile(명중 처리) 가 공통 경유.
//
// 외부에 공개되는 진입점은 아래 3가지뿐. effect 적용/데미지 계산/공격 종류별
// 분기 같은 내부 헬퍼는 .cpp 의 anonymous namespace 에 격리되어 외부에서 호출 불가.
//
//   CastTargeted    — caster + target 객체가 모두 명확한 시전 (Player Targeted, Monster AI).
//                     Skillshot 분기는 caster→target 벡터로 방향 자동 계산.
//   CastSkillshot   — target 객체 없이 dir 만 있는 케이스 (Player C_UseSkill 의 Skillshot 입력).
//   ResolveHit      — Projectile 명중 시점 OnHit 적용 + S_SkillHit 방송.

#include "Utils/Types.h"
#include "common.pb.h"


class Unit;
class Zone;
struct SkillTemplate;


namespace SkillRuntime
{
	void CastTargeted(const SkillTemplate& skill, Unit& caster, Unit& target, Zone& zone);

	void CastSkillshot(Unit& caster, float dirX, float dirZ,
	                   const SkillTemplate& skill, Zone& zone);

	void ResolveHit(Unit* caster, Unit& target, int32 skillId,
	                const Proto::Vector2& casterPos,
	                const Proto::Vector2& hitPos,
	                Zone& zone);
}
