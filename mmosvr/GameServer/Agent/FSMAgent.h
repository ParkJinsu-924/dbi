#pragma once

#include "Agent/IAgent.h"
#include "MonsterStates.h"   // MonsterFSM, MonsterStateId


// ===========================================================================
// FSMAgent — Monster 전용. MonsterFSM 소유 + state 관리 delegate.
// Tick 은 BuffAgent.CanAct() 체크 후 fsm_.Update 만.
// "Monster 에만 붙는다" 는 계약은 등록 지점(Monster::Monster()) 이 유일하다는
// 점으로 보장. ctor 의 assert 가 Debug 빌드에서 계약을 검증한다.
// ===========================================================================
class FSMAgent : public IAgent
{
public:
	explicit FSMAgent(Unit& owner);

	void Tick(float deltaTime) override;

	// FSM 직접 접근 (상태 등록, OnStateChanged 콜백 등 초기 설정용).
	MonsterFSM&       GetFSM()       { return fsm_; }
	const MonsterFSM& GetFSM() const { return fsm_; }

	// 자주 쓰는 delegate shortcut.
	MonsterStateId GetCurrentStateId() const { return fsm_.GetCurrentStateId(); }
	void ChangeState(MonsterStateId s) { fsm_.ChangeState(s); }

private:
	MonsterFSM fsm_;
};
