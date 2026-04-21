#pragma once

#include "Agent/IAgent.h"
#include "common.pb.h"


// ===========================================================================
// MovementAgent — Player 클릭-투-무브 상태. 목적지 하나와 활성 플래그만 들고,
// Tick 마다 Unit::MoveToward 로 한 틱 접근. 도달하면 자동 비활성.
//
// 외부 트리거:
//   - C_MoveCommand 수신 → SetDestination(target)
//   - C_StopMove / C_UseSkill 수신 → Clear()
//
// Zone::BroadcastObjectPositions 가 IsMoving() 을 읽어 이동 중인 Player 만
// 주기 방송 대상으로 삼는다.
// ===========================================================================
class MovementAgent : public IAgent
{
public:
	explicit MovementAgent(Unit& owner) : IAgent(owner) {}

	void Tick(float dt) override;

	void SetDestination(const Proto::Vector2& dest) { destination_ = dest; active_ = true; }
	void Clear() { active_ = false; }

	bool IsMoving() const { return active_; }
	const Proto::Vector2& GetDestination() const { return destination_; }

private:
	Proto::Vector2 destination_;
	bool active_ = false;
};
