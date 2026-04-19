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


// StatMod 타입 Effect 가 건드리는 스탯 종류.
// None : CCState 처럼 스탯 변화 없이 CC 플래그만 부여하는 경우.
enum class StatType : int32
{
	None         = 0,
	MoveSpeed    = 1,
	AttackSpeed  = 2,
	MaxHp        = 3,
	Damage       = 4,
};


// CC (Crowd Control) 플래그. Buff/Debuff 가 부여하는 행동 제약.
// 하나의 Effect 는 하나의 CCFlag 만 가진다 (여러 CC 가 필요하면 스킬에 Effect 를 여러 줄 연결).
// BuffContainer::GetCCFlags() 가 부착된 모든 Buff 의 플래그를 OR 해서 Unit 의 최종 상태를 결정.
enum class CCFlag : uint32
{
	None         = 0,
	Stun         = 1u << 0,   // 이동/공격/스킬 전부 차단
	Silence      = 1u << 1,   // 스킬 차단 (평타/이동 가능)
	Root         = 1u << 2,   // 이동 차단 (공격/스킬 가능)
	Slow         = 1u << 3,   // 이동속도 감소 (StatMod 와 함께 쓰는 카테고리 플래그)
	Invulnerable = 1u << 4,   // 데미지 무효
};
