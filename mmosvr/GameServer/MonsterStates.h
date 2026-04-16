#pragma once

#include "FSM.h"

class Monster;

enum class MonsterStateId : uint8_t
{
	Idle,
	Chase,
	Attack,
	Return
};

using MonsterFSM = StateMachine<Monster, MonsterStateId>;


// ---------------------------------------------------------------------------
// Idle — 스폰 지점 대기, detectRange 내 플레이어 탐색
// ---------------------------------------------------------------------------
class IdleState : public IState<Monster>
{
public:
	void OnEnter(Monster& owner) override;
	void OnUpdate(Monster& owner, float deltaTime) override;
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
