#pragma once

#include "Npc.h"
#include "MonsterStates.h"
#include "AttackTypes.h"
#include "AggroTable.h"
#include <optional>
#include <unordered_map>

class Player;
struct SkillTemplate;


class Monster : public Npc
{
public:
	Monster(std::string name, Zone& zone)
		: Npc(GameObjectType::Monster, zone, std::move(name))
	{
	}

	void Update(float deltaTime) override;

	// --- AI 초기화 ---
	// 호출 전에 Zone::Add 로 존에 배치되어 있어야 한다 (GetZone() 사용).
	void InitAI(const Proto::Vector2& spawnPos);

	// --- FSM 접근 ---
	MonsterFSM&       GetFSM()       { return fsm_; }
	const MonsterFSM& GetFSM() const { return fsm_; }
	MonsterStateId    GetStateId() const { return fsm_.GetCurrentStateId(); }

	// --- 상태에서 사용하는 public 유틸리티 ---
	const Proto::Vector2& GetSpawnPos() const { return spawnPos_; }

	void SetTarget(long long guid) { targetGuid_ = guid; }
	void ClearTarget()             { targetGuid_ = 0; }
	long long GetTargetGuid() const { return targetGuid_; }
	std::shared_ptr<Player> GetTarget() const;

	// --- Aggro ---
	// 실제 저장/집계는 AggroTable 이 담당. Monster 는 얇은 위임만 한다.
	void AddAggro(long long playerGuid, float amount);
	long long GetTopAggroGuid() const;                  // 없으면 0
	bool HasAggro() const;
	void ClearAggro();

	float DistanceToSpawn() const;
	void  MoveToward(const Proto::Vector2& target, float deltaTime);

	// --- AI 파라미터 접근 ---
	float GetDetectRange()    const { return detectRange_; }
	float GetLeashRange()     const { return leashRange_; }
	float GetMoveSpeed()      const { return moveSpeed_; }

	void SetDetectRange(float v)    { detectRange_ = v; }
	void SetLeashRange(float v)     { leashRange_ = v; }
	void SetMoveSpeed(float v)      { moveSpeed_ = v; }

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

	// PickCastable 로 선택된 스킬을 시전 후 호출 — 해당 skillId 의 다음 사용 가능 시각 갱신.
	void MarkSkillUsed(int32 skillId, float nextUsable) { skillNextUsable_[skillId] = nextUsable; }

private:
	void BroadcastState(MonsterStateId prev, MonsterStateId next);

	// --- FSM ---
	MonsterFSM fsm_;
	Proto::Vector2 spawnPos_;
	long long targetGuid_ = 0;

	// --- AI 파라미터 ---
	// 공격 스킬은 monster_skills.csv (tid 기준) 에서 로드. 사거리/쿨다운/데미지는 SkillTemplate 참조.
	int32 templateId_  = 0;
	float detectRange_ = 10.0f;
	float leashRange_  = 15.0f;
	float moveSpeed_   = 3.0f;

	// skillId -> 다음 사용 가능 시각(TimeManager.totalTime 기준). 엔트리 없으면 0 (즉시 가능).
	std::unordered_map<int32, float> skillNextUsable_;

	// --- Aggro ---
	AggroTable aggro_;
};
