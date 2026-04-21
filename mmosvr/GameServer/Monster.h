#pragma once

#include "Npc.h"
#include "MonsterStates.h"
#include "AttackTypes.h"
#include "Agent/FSMAgent.h"
#include "Agent/AggroAgent.h"
#include <optional>

class Player;
struct SkillTemplate;
struct MonsterTemplate;


class Monster : public Npc
{
public:
	explicit Monster(Zone& zone)
		: Npc(GameObjectType::Monster, zone)
	{
		moveSpeed_ = 3.0f;
		AddAgent<FSMAgent>();
		AddAgent<AggroAgent>();
	}

	void Update(float deltaTime) override;

	// --- AI 초기화 ---
	// 호출 전에 Zone::Add 로 존에 배치되어 있어야 한다 (GetZone() 사용).
	void InitAI(const Proto::Vector2& spawnPos);

	// MonsterTemplate 의 스탯/파라미터를 일괄 적용. templateId 는 별도 인자로 받아
	// monster_skills.csv 조인 키로 사용한다.
	void ApplyTemplate(int32 templateId, const MonsterTemplate& tmpl);

	// --- FSM 접근 ---
	MonsterFSM&       GetFSM()       { return Get<FSMAgent>().GetFSM(); }
	const MonsterFSM& GetFSM() const { return Get<FSMAgent>().GetFSM(); }
	MonsterStateId    GetStateId() const { return Get<FSMAgent>().GetCurrentStateId(); }

	// --- 상태에서 사용하는 public 유틸리티 ---
	const Proto::Vector2& GetSpawnPos() const { return spawnPos_; }

	// Target 의 유일 진실 원천 = AggroAgent.GetTop().
	// 강제 타겟팅(Taunt 등) 이 필요하면 AggroAgent.Add(guid, huge_value) 로 구현.
	long long GetTargetGuid() const { return Get<AggroAgent>().GetTop(); }
	std::shared_ptr<Player> GetTarget() const;

	float DistanceToSpawn() const;

	// --- AI 파라미터 접근 (MoveSpeed 는 Unit 에 있음) ---
	float GetDetectRange()    const { return detectRange_; }
	float GetLeashRange()     const { return leashRange_; }

	void SetDetectRange(float v)    { detectRange_ = v; }
	void SetLeashRange(float v)     { leashRange_ = v; }

	// --- Skill rotation ---
	// templateId = MonsterTemplate.tid. monster_skills.csv 조회 키.
	int32 GetTemplateId() const { return templateId_; }
	void  SetTemplateId(int32 v) { templateId_ = v; }

	// 이 몬스터의 "기본" 스킬 (monster_skills.csv 의 is_basic=true) 의 cast_range.
	// Engage 상태에서 접근/대기 전환 임계값으로 사용 — 즉 몬스터가 머물고 싶어하는 거리.
	// basic 스킬이 없으면(잘못된 데이터) 0 반환.
	float GetBasicSkillRange() const;

	// 현재 시각(now) 에서 distance 거리의 타겟에게 시전 가능한 스킬을 가중 추첨.
	// cast_range 내이고 실효 쿨다운(max(sk.cooldown, minInterval)) 을 통과한 후보만 대상.
	struct SkillChoice
	{
		const SkillTemplate* tmpl;
		int32 skillId;
		float appliedCooldown;   // 다음 사용 가능 시각 = now + appliedCooldown
	};
	std::optional<SkillChoice> PickCastable(float now, float distance) const;

private:
	Proto::Vector2 spawnPos_;

	// --- AI 파라미터 ---
	// 공격 스킬은 monster_skills.csv (tid 기준) 에서 로드. 사거리/쿨다운/데미지는 SkillTemplate 참조.
	int32 templateId_  = 0;
	float detectRange_ = 10.0f;
	float leashRange_  = 15.0f;
};
