#pragma once

#include <atomic>


// ===========================================================================
// AgentRegistry — Agent 타입별 정적 인덱스 발급.
// 첫 IdOf<T>() 호출 시 counter_ 에서 값 할당, 이후 같은 T 는 같은 id 반환.
// Unit::agents_ 벡터의 인덱스로 사용되어 Get<T>() 가 배열 인덱싱 1 회 비용으로
// 동작한다. id 는 프로세스 내 일관되며, 빌드 간에는 호출 순서에 따라 달라질
// 수 있다 (의미상 문제 없음).
//
// counter_ 는 atomic — 현재 Unit 생성이 GameLoopThread 단일 스레드에 직렬화
// 되어 있어 실질 race 는 없지만, 향후 병렬 Unit 생성 경로가 도입될 때
// data race → UB 로 이어지지 않도록 방어. hot path 가 아니므로 비용 무시.
// ===========================================================================
class AgentRegistry
{
public:
	template<typename T>
	static int IdOf()
	{
		static const int id = counter_.fetch_add(1, std::memory_order_relaxed);
		return id;
	}

	static int Count() { return counter_.load(std::memory_order_relaxed); }

private:
	inline static std::atomic<int> counter_{ 0 };
};
