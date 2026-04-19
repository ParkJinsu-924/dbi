#include "pch.h"
#include "AggroTable.h"


void AggroTable::Add(const long long playerGuid, const float amount)
{
	if (playerGuid == 0)
		return;
	table_[playerGuid] += amount;
}

long long AggroTable::ResolveTop() const
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

void AggroTable::Clear()
{
	table_.clear();
}
