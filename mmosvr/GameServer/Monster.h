#pragma once

#include "Npc.h"
#include "MonsterStates.h"
#include "AttackTypes.h"
#include "AggroTable.h"

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
	void  DoAttack(Player& target);

	// --- AI 파라미터 접근 ---
	float GetDetectRange()    const { return detectRange_; }
	float GetLeashRange()     const { return leashRange_; }
	float GetMoveSpeed()      const { return moveSpeed_; }
	int32 GetBasicSkillId()   const { return basicSkillId_; }
	const SkillTemplate* GetBasicSkill() const;   // ResourceManager 조회. 미존재 시 nullptr.
	float GetLastAttackTime() const { return lastAttackTime_; }
	void  SetLastAttackTime(float t) { lastAttackTime_ = t; }

	void SetDetectRange(float v)    { detectRange_ = v; }
	void SetLeashRange(float v)     { leashRange_ = v; }
	void SetMoveSpeed(float v)      { moveSpeed_ = v; }
	void SetBasicSkillId(int32 v)   { basicSkillId_ = v; }

private:
	void BroadcastState(MonsterStateId prev, MonsterStateId next);

	// --- FSM ---
	MonsterFSM fsm_;
	Proto::Vector2 spawnPos_;
	long long targetGuid_ = 0;

	// --- AI 파라미터 ---
	// 공격 관련 파라미터(사거리/쿨다운/데미지)는 basicSkillId_ 가 가리키는 SkillTemplate + SkillEffect 에서 로드.
	// Monster 자체는 "어떤 평타 스킬을 쓰는가" 만 알면 된다.
	float detectRange_ = 10.0f;
	float leashRange_  = 15.0f;
	float moveSpeed_   = 3.0f;
	int32 basicSkillId_   = 0;
	float lastAttackTime_ = 0.0f; // GameTime::GetTotalTime() 기준 — AttackState 쿨다운 판정용

	// --- Aggro ---
	AggroTable aggro_;
};
