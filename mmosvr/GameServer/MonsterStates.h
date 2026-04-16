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
// Patrol — move to a random point within range of spawn, then go Idle
// ---------------------------------------------------------------------------
class PatrolState : public IState<Monster>
{
public:
	void OnEnter(Monster& owner) override;
	void OnUpdate(Monster& owner, float deltaTime) override;

private:
	float targetX_ = 0.0f;
	float targetZ_ = 0.0f;
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

private: // timer 에 버그가 있음. Enter 에서 0 으로 만들기 때문에, Chase상태로 갔다가 다시 AttackState 로 들어오게 된다면,
	// 0으로 되기 때문에 바로 공격을 시도해서 2단 3단 공격이 되버리는 상황 발생.
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
