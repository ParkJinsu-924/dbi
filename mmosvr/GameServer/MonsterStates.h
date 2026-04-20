#pragma once

#include "FSM.h"

class Monster;

enum class MonsterStateId : uint8_t
{
	Idle,
	Patrol,
	Engage,
	Return
};

using MonsterFSM = StateMachine<Monster, MonsterStateId>;


// ---------------------------------------------------------------------------
// GlobalDetectState — every tick, detect player within range -> Engage.
// Idle/Patrol 상태에서만 감지 수행. 이미 교전(Engage)/귀환(Return) 중이면 스킵.
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
	void OnExit(Monster& owner) override;

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
// Engage — 타겟 획득 후 전투 행동 전체 (접근 + 시전 + 대기).
// 매 틱 PickCastable 결과로 "시전 가능하면 시전, 아니면 거리 기준 접근/대기" 분기.
// phase_ 는 로직에 영향 없는 관측 태그 — 디버그·분석용.
// ---------------------------------------------------------------------------
class EngageState : public IState<Monster>
{
public:
	enum class Phase : uint8_t { Approach, Casting, Waiting };

	void OnEnter(Monster& owner) override;
	void OnUpdate(Monster& owner, float deltaTime) override;

	Phase GetPhase() const { return phase_; }

private:
	Phase phase_ = Phase::Approach;
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
