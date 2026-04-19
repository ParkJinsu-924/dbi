#pragma once

#include "Utils/Types.h"
#include "game.pb.h"


// SkillTemplate.targeting — 스킬 수행 방식.
// Monster/Player 공통: 모든 공격(평타 포함)은 Skill 을 거쳐 실행된다.
//   Melee     : 즉시 데미지 (시각적 근접 타격)
//   Hitscan   : 즉시 데미지 + 광선 라인 연출
//   Homing    : HomingProjectile 발사
//   Skillshot : SkillshotProjectile 발사
//
// Proto::ProjectileKind(HOMING=0, SKILLSHOT=1) 와 값이 더 이상 일치하지 않는다 —
// PacketMaker 가 projectile spawn 패킷을 만들 때 명시적으로 매핑한다.
enum class SkillKind : int32
{
	Melee     = 0,
	Hitscan   = 1,
	Homing    = 2,
	Skillshot = 3,
};
