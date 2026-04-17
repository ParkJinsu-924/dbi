#pragma once

#include "Utils/Types.h"
#include "game.pb.h"


// Monster/Player 의 공격 수행 방식.
// CSV (monster_templates.csv 의 attackType 컬럼) 에 숫자로 저장되고,
// CsvParser::Row::GetOr 의 is_enum_v 분기가 static_cast 로 변환해 로드한다.
enum class AttackType : int32
{
	Melee     = 0,  // 즉시 데미지
	Hitscan   = 1,  // 즉시 데미지 + 광선 시각화 패킷
	Homing    = 2,  // HomingProjectile 발사
	Skillshot = 3,  // SkillshotProjectile 발사
};


// SkillTemplate.kind — 발사할 투사체 종류.
// Proto::ProjectileKind 와 값이 일치해야 한다 (S_ProjectileSpawn 에서 proto enum 으로 변환되므로).
enum class SkillKind : int32
{
	Homing    = 0,
	Skillshot = 1,
};

static_assert(static_cast<int32>(SkillKind::Homing)    == Proto::PROJECTILE_HOMING,
	"SkillKind::Homing must match Proto::PROJECTILE_HOMING");
static_assert(static_cast<int32>(SkillKind::Skillshot) == Proto::PROJECTILE_SKILLSHOT,
	"SkillKind::Skillshot must match Proto::PROJECTILE_SKILLSHOT");
