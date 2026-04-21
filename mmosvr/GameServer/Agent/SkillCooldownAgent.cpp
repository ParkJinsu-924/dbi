#include "pch.h"
#include "Agent/SkillCooldownAgent.h"


bool SkillCooldownAgent::TryConsume(const int32 skillId, const float cooldownSec)
{
	const float now = GetTimeManager().GetTotalTime();
	auto it = nextUsable_.find(skillId);
	if (it != nextUsable_.end() && now < it->second)
		return false;

	nextUsable_[skillId] = now + cooldownSec;
	return true;
}

bool SkillCooldownAgent::IsReady(const int32 skillId, const float now) const
{
	auto it = nextUsable_.find(skillId);
	return (it == nextUsable_.end()) || (now >= it->second);
}
