#pragma once

#include "Agent/IAgent.h"
#include "Utils/Types.h"

#include <unordered_map>


// ===========================================================================
// AggroAgent — Monster 전용. 플레이어 guid → 누적 어그로 수치 관리.
// top aggro 대상을 몬스터의 target 으로 사용한다.
//
// Phase 1:
//   - 데미지 1 = 어그로 1
//   - Clear() 는 Leash 초과 / 사망 등 외부 트리거에서 호출
//
// 확장 여지 (후속 Phase):
//   - 역할 배수 (Tank/DPS/Healer) → Add 호출부에서 multiplier 곱한 값 전달
//   - Transition 110% 규칙 → GetTop 결과를 사용하는 쪽에서 비교
//   - Taunt → Add 로 현재 top * 1.2 주입
//
// "Monster 에만 붙는다" 는 계약은 등록 지점(Monster::Monster()) 유일성으로
// 보장. ctor 의 assert 가 Debug 빌드 계약 가드.
// ===========================================================================
class AggroAgent : public IAgent
{
public:
	explicit AggroAgent(Unit& owner);

	// player_guid 에 amount 만큼 어그로 누적. 0 guid 는 무시.
	void Add(long long playerGuid, float amount);

	// 현재 어그로가 가장 높은 player guid. 비어있으면 0.
	long long GetTop() const;

	bool HasAny() const { return !table_.empty(); }

	// 전체 초기화. Leash reset / 사망 / 외부 트리거 용.
	void Clear() { table_.clear(); }

private:
	std::unordered_map<long long /*playerGuid*/, float /*aggro*/> table_;
};
