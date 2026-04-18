#pragma once

#include "Utils/Types.h"
#include <unordered_map>


// MMORPG 표준 어그로(Aggro) 테이블.
// 플레이어 guid → 누적 어그로 수치. top aggro 대상을 몬스터의 target 으로 사용한다.
//
// Phase 1:
//   - 데미지 1 = 어그로 1
//   - OOC (Out Of Combat: 5초간 Add 호출 없음) 시 자체 초기화
//   - Clear() 는 Leash 초과 / 사망 등 외부에서도 호출
//
// 확장 여지 (후속 Phase):
//   - 역할 배수 (Tank/DPS/Healer) → Add 호출부에서 multiplier 곱한 값 전달
//   - Transition 110% 규칙 → ResolveTop 결과를 사용하는 쪽에서 비교
//   - Taunt → Add 로 현재 top * 1.2 주입
class AggroTable
{
public:
	// player_guid 에 amount 만큼 어그로 누적. 0 guid 나 0 이하 amount 는 무시.
	void Add(long long playerGuid, float amount);

	// 현재 어그로가 가장 높은 player guid. 비어있으면 0.
	long long ResolveTop() const;

	bool Empty() const { return table_.empty(); }

	// 전체 초기화. Leash reset / 사망 / 외부 트리거 용.
	void Clear();

	// OOC 타이머 진행. 5초 이상 Add 가 없으면 내부 Clear 하고 true 반환.
	// 호출측은 반환값을 보고 state 전이(예: Return)를 수행.
	bool TickOOC(float deltaTime);

private:
	static constexpr float OOC_RESET_SECONDS = 5.0f;

	std::unordered_map<long long /*playerGuid*/, float /*aggro*/> table_;
	float oocTimer_ = 0.0f;  // 마지막 Add 이후 경과 시간(초)
};
