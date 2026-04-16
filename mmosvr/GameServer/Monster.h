#pragma once

#include "Npc.h"

class Zone;
class Player;

enum class MonsterState : uint8_t
{
	Idle,
	Chase,
	Attack,
	Return
};


class Monster : public Npc
{
public:
	explicit Monster(std::string name = "")
		: Npc(GameObjectType::Monster, std::move(name))
	{
	}

	void Update(float deltaTime) override;

	// --- AI initialisation (replaces CircularMovement) ---
	void InitAI(const Proto::Vector3& spawnPos, Zone* zone);

	// --- AI parameters ---
	void SetDetectRange(float v) { detectRange_ = v; }
	void SetAttackRange(float v) { attackRange_ = v; }
	void SetLeashRange(float v) { leashRange_ = v; }
	void SetMoveSpeed(float v) { moveSpeed_ = v; }
	void SetAttackCooldown(float v) { attackCooldown_ = v; }
	void SetAttackDamage(int32 v) { attackDamage_ = v; }

	MonsterState GetState() const { return state_; }

private:
	// --- FSM state handlers ---
	void UpdateIdle(float deltaTime);
	void UpdateChase(float deltaTime);
	void UpdateAttack(float deltaTime);
	void UpdateReturn(float deltaTime);

	void ChangeState(MonsterState newState);

	// --- Utility ---
	float DistanceTo(const Proto::Vector3& target) const;
	float DistanceToSpawn() const;
	void MoveToward(const Proto::Vector3& target, float deltaTime);
	std::shared_ptr<Player> GetTarget() const;
	void BroadcastState();
	void BroadcastAttack(long long targetGuid, int32 damage);

	// --- FSM ---
	MonsterState state_ = MonsterState::Idle;
	Zone* zone_ = nullptr;
	Proto::Vector3 spawnPos_;
	long long targetGuid_ = 0;
	float attackTimer_ = 0.0f;

	// --- AI parameters ---
	float detectRange_ = 10.0f;
	float attackRange_ = 2.0f;
	float leashRange_ = 15.0f;
	float moveSpeed_ = 3.0f;
	float attackCooldown_ = 1.5f;
	int32 attackDamage_ = 10;
};
