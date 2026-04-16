#pragma once

#include "FSM.h"

class Monster;

enum class MonsterStateId : uint8_t
{
	Idle,
	Patrol,
	Chase,
	Attack,
	Return
};

using MonsterFSM = StateMachine<Monster, MonsterStateId>;


// ---------------------------------------------------------------------------
// GlobalDetectState — every tick, detect player within range -> Chase
// Runs before the current state. Skips detection in Chase/Attack/Return.
// ---------------------------------------------------------------------------
class MonsterGlobalState : public IState<Monster>
{
public:
	void OnUpdate(Monster& owner, float deltaTime) override;
};


// ---------------------------------------------------------------------------
// Idle — spawn point standby
// ---------------------------------------------------------------------------
class IdleState : public IState<Monster>
{
public:
	void OnEnter(Monster& owner) override;
	void OnUpdate(Monster& owner, float deltaTime) override;
	
private:
	float idleTime_ = 0.0f;
};


// ---------------------------------------------------------------------------
// Patrol — patrol around spawn point in a circle
// ---------------------------------------------------------------------------
class PatrolState : public IState<Monster>
{
public:
	void OnEnter(Monster& owner) override;
	void OnUpdate(Monster& owner, float deltaTime) override;

private:
	float angle_ = 0.0f;
	float patrolTime_ = 0.0f;
};


// ---------------------------------------------------------------------------
// Chase — 타겟 플레이어 추적
// ---------------------------------------------------------------------------
class ChaseState : public IState<Monster>
{
public:
	void OnUpdate(Monster& owner, float deltaTime) override;
};


// ---------------------------------------------------------------------------
// Attack — 공격 범위 내 타겟 공격 (쿨다운 기반)
// ---------------------------------------------------------------------------
class AttackState : public IState<Monster>
{
public:
	void OnEnter(Monster& owner) override;
	void OnUpdate(Monster& owner, float deltaTime) override;

private:
	float attackTimer_ = 0.0f;
};


// ---------------------------------------------------------------------------
// Return — 스폰 지점으로 복귀
// ---------------------------------------------------------------------------
class ReturnState : public IState<Monster>
{
public:
	void OnEnter(Monster& owner) override;
	void OnUpdate(Monster& owner, float deltaTime) override;
};
