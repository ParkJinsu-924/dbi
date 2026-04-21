#include "pch.h"
#include "Agent/AggroAgent.h"
#include "Unit.h"
#include <cassert>


AggroAgent::AggroAgent(Unit& owner)
	: IAgent(owner)
{
	assert(owner.GetType() == GameObjectType::Monster);
}

void AggroAgent::Add(const long long playerGuid, const float amount)
{
	if (playerGuid == 0)
		return;
	table_[playerGuid] += amount;
}

long long AggroAgent::GetTop() const
{
	long long topGuid = 0;
	float topVal = -1.0f;
	for (const auto& [guid, val] : table_)
	{
		if (val > topVal)
		{
			topVal = val;
			topGuid = guid;
		}
	}
	return topGuid;
}
