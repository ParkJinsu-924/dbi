#pragma once

#include "Npc.h"
#include "MonsterStates.h"

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
	int32 GetAttackType()     const { return attackType_; }

	void SetDetectRange(float v)    { detectRange_ = v; }
	void SetAttackRange(float v)    { attackRange_ = v; }
	void SetLeashRange(float v)     { leashRange_ = v; }
	void SetMoveSpeed(float v)      { moveSpeed_ = v; }
	void SetAttackCooldown(float v) { attackCooldown_ = v; }
	void SetAttackDamage(int32 v)   { attackDamage_ = v; }
	void SetAttackType(int32 v)     { attackType_ = v; }

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
	int32 attackDamage_   = 10;
	int32 attackType_     = 0;    // 0=Melee, 1=Hitscan
};
