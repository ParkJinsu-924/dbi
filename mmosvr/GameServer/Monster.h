#pragma once

#include "Npc.h"
#include "MonsterStates.h"
#include "AttackTypes.h"
#include "AggroTable.h"

class Zone;
class Player;


class Monster : public Npc
{
public:
	explicit Monster(std::string name = "")
		: Npc(GameObjectType::Monster, std::move(name))
	{
	}

	void Update(float deltaTime) override;

	// --- AI 초기화 ---
	void InitAI(const Proto::Vector3& spawnPos, Zone* zone);

	// --- FSM 접근 ---
	MonsterFSM&       GetFSM()       { return fsm_; }
	const MonsterFSM& GetFSM() const { return fsm_; }
	MonsterStateId    GetStateId() const { return fsm_.GetCurrentStateId(); }

	// --- 상태에서 사용하는 public 유틸리티 ---
	Zone*  GetZone()  const { return zone_; }
	const Proto::Vector3& GetSpawnPos() const { return spawnPos_; }

	void SetTarget(long long guid) { targetGuid_ = guid; }
	void ClearTarget()             { targetGuid_ = 0; }
	std::shared_ptr<Player> GetTarget() const;

	// --- Aggro ---
	// 실제 저장/집계는 AggroTable 이 담당. Monster 는 얇은 위임만 한다.
	// OOC 타이머 진행은 전투 상태(Chase/Attack)에서만 의미 있으므로
	// MonsterGlobalState::OnUpdate 가 해당 state 에서만 TickAggroOOC 를 호출한다.
	// 반환값 true = 5초 경과로 table 이 자동 clear 됨 → 호출측이 Return 상태로 전이.
	void AddAggro(long long playerGuid, float amount);
	long long ResolveTopAggroGuid() const;                  // 없으면 0
	bool HasAggro() const;
	void ClearAggro();
	bool TickAggroOOC(float deltaTime);

	float DistanceTo(const Proto::Vector3& target) const;
	float DistanceToSpawn() const;
	void  MoveToward(const Proto::Vector3& target, float deltaTime);
	void  DoAttack(Player& target);

	// --- AI 파라미터 접근 ---
	float GetDetectRange()    const { return detectRange_; }
	float GetAttackRange()    const { return attackRange_; }
	float GetLeashRange()     const { return leashRange_; }
	float GetMoveSpeed()      const { return moveSpeed_; }
	float GetAttackCooldown() const { return attackCooldown_; }
	int32 GetAttackDamage()   const { return attackDamage_; }
	AttackType GetAttackType() const { return attackType_; }
	int32 GetSkillId()        const { return skillId_; }
	float GetLastAttackTime() const { return lastAttackTime_; }
	void  SetLastAttackTime(float t) { lastAttackTime_ = t; }

	void SetDetectRange(float v)    { detectRange_ = v; }
	void SetAttackRange(float v)    { attackRange_ = v; }
	void SetLeashRange(float v)     { leashRange_ = v; }
	void SetMoveSpeed(float v)      { moveSpeed_ = v; }
	void SetAttackCooldown(float v) { attackCooldown_ = v; }
	void SetAttackDamage(int32 v)   { attackDamage_ = v; }
	void SetAttackType(AttackType v) { attackType_ = v; }
	void SetSkillId(int32 v)        { skillId_ = v; }

private:
	void BroadcastState(MonsterStateId prev, MonsterStateId next);
	void BroadcastAttack(long long targetGuid, int32 damage);

	// --- FSM ---
	MonsterFSM fsm_;
	Zone* zone_ = nullptr;
	Proto::Vector3 spawnPos_;
	long long targetGuid_ = 0;

	// --- AI 파라미터 ---
	float detectRange_    = 10.0f;
	float attackRange_    = 2.0f;
	float leashRange_     = 15.0f;
	float moveSpeed_      = 3.0f;
	float attackCooldown_ = 1.5f;
	int32       attackDamage_   = 10;
	AttackType  attackType_     = AttackType::Melee;
	int32       skillId_        = 0;    // attackType=Homing/Skillshot 일 때 SkillTemplate.sid
	float       lastAttackTime_ = 0.0f; // GameTime::GetTotalTime() 기준

	// --- Aggro ---
	AggroTable aggro_;
};
