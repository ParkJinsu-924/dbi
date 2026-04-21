#pragma once

class Unit;

// ===========================================================================
// IAgent — Unit 에 부착되는 기능 모듈의 베이스.
// Unit 은 ctor 에서 AddAgent<T>() 로 등록하고, 이후 Get<T>() 로 접근한다.
// Tick 기본 구현은 no-op; 주기적 갱신이 필요한 Agent 만 override.
// ===========================================================================
class IAgent
{
public:
	virtual ~IAgent() = default;

	// Unit::Update 매 틱마다 호출. 등록 순서대로 실행된다.
	virtual void Tick(float /*deltaTime*/) {}

protected:
	// 소유 Unit 에 대한 참조. Unit 수명이 Agent 수명을 포함한다.
	explicit IAgent(Unit& owner) : owner_(owner) {}

	Unit& owner_;
};
