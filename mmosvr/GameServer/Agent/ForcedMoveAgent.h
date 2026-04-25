#pragma once

#include "Agent/IAgent.h"
#include "common.pb.h"


// ===========================================================================
// ForcedMoveAgent — 서버 권위로 Unit 을 자동 보간 이동시키는 Agent.
//
// 사용처 (예정 — 현재 미연결):
//   - 돌진/대시 스킬: 사용자 입력 무시하고 destination 까지 강제 이동
//   - 넉백: 피격자를 attacker 반대 방향으로 일정 거리 밀어냄
//   - NPC follow-path 등 서버가 강제하는 자동 이동
//
// 클라이언트 권위 클릭-투-무브와는 별개. 강제 이동 활성 동안 호출 측이
// BuffAgent::CanMove() 를 false 로 만들거나 C_PlayerMove 를 거부해야 한다.
//
// API (TBD — 첫 사용처 등장 시 확정):
//   StartForced(dest, speed, onArrive)
//   Clear()
//   IsActive()
// ===========================================================================
class ForcedMoveAgent : public IAgent
{
public:
	explicit ForcedMoveAgent(Unit& owner) : IAgent(owner) {}

	void Tick(float dt) override;

	bool IsActive() const { return active_; }

private:
	// 첫 사용처 도입 시 destination/속도/도달 콜백 등 채울 예정.
	bool active_ = false;
};
