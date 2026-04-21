#pragma once

#include "Agent/IAgent.h"
#include "AggroTable.h"
#include "Utils/Types.h"


// ===========================================================================
// AggroAgent — Monster 전용. AggroTable 소유 + delegate.
// Add / GetTop / HasAny / Clear 네 가지 핵심 API 만 노출.
// "Monster 에만 붙는다" 는 계약은 등록 지점(Monster::Monster()) 유일성으로
// 보장. ctor 의 assert 가 Debug 빌드 계약 가드.
// ===========================================================================
class AggroAgent : public IAgent
{
public:
	explicit AggroAgent(Unit& owner);

	void Add(long long playerGuid, float amount) { table_.Add(playerGuid, amount); }
	long long GetTop() const { return table_.ResolveTop(); }  // 없으면 0
	bool HasAny() const { return !table_.Empty(); }
	void Clear() { table_.Clear(); }

private:
	AggroTable table_;
};
