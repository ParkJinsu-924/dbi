#pragma once

#include "Agent/IAgent.h"
#include "Utils/Types.h"

#include <unordered_map>


// ===========================================================================
// SkillCooldownAgent — Unit 공통. Skill id 별 "다음 사용 가능 시각" 관리.
// Player 는 TryConsume (소비형), Monster 는 IsReady + MarkUsed (조회 + 설정 분리)
// 두 패턴을 모두 지원한다.
// 시간 기준은 TimeManager.GetTotalTime() (초).
// ===========================================================================
class SkillCooldownAgent : public IAgent
{
public:
	explicit SkillCooldownAgent(Unit& owner) : IAgent(owner) {}

	// 사용 가능하면 다음 사용 가능 시각을 now+cooldownSec 로 갱신하고 true.
	// 쿨다운 중이면 false. (Player 스타일)
	bool TryConsume(int32 skillId, float cooldownSec);

	// now 시점에 사용 가능한지만 조회. (Monster PickCastable 스타일)
	bool IsReady(int32 skillId, float now) const;

	// 스킬 사용을 외부에서 마킹. nextUsable = 다음 사용 가능 시각.
	void MarkUsed(int32 skillId, float nextUsable) { nextUsable_[skillId] = nextUsable; }

private:
	std::unordered_map<int32, float> nextUsable_;
};
