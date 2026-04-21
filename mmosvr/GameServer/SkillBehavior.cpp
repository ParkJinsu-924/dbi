#include "pch.h"
#include "SkillBehavior.h"
#include "Agent/BuffAgent.h"
#include "Monster.h"
#include "Player.h"
#include "SkillTemplate.h"
#include "SkillRuntime.h"


void DefaultAttackBehavior::Execute(const SkillTemplate& skill, Monster& owner, Player& target, float /*now*/)
{
	// 기존 Monster::DoAttack 과 동일:
	//  - Stun 시 공격 불가. (Silence 는 "기본 공격" 이라 면제 — LoL 관습 유지)
	//  - SkillRuntime::Cast 로 targeting 별 디스패치 위임.
	if (!owner.Get<BuffAgent>().CanAttack()) return;
	SkillRuntime::Cast(skill, owner, target, owner.GetZone());
}
