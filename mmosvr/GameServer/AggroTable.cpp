#include "pch.h"
#include "AggroTable.h"


void AggroTable::Add(const long long playerGuid, const float amount)
{
	if (playerGuid == 0)
		return;
	table_[playerGuid] += amount;
	oocTimer_ = 0.0f;  // 전투 활동 감지 → OOC 카운터 리셋
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
	oocTimer_ = 0.0f;
}

bool AggroTable::TickOOC(const float deltaTime)
{
	if (table_.empty())
		return false;

	oocTimer_ += deltaTime;
	if (oocTimer_ >= OOC_RESET_SECONDS)
	{
		Clear();
		return true;
	}
	return false;
}
