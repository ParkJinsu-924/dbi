#pragma once

// Strategy 인터페이스 — 스킬마다의 "AI 수준 행동" 을 캡슐화한다.
// 데미지/Effect 적용 같은 실제 처리는 SkillRuntime::Cast 가 담당하며,
// Behavior 는 그 호출 여부 및 주변 처리(예: 추가 조건, 선행 이동 등)를 결정한다.
// 기본 구현은 DefaultAttackBehavior — SkillRuntime::Cast 한 번 호출만.

class Monster;
class Player;
struct SkillTemplate;


class ISkillBehavior
{
public:
	virtual ~ISkillBehavior() = default;

	// 쿨다운·사거리 외 추가 시전 조건. 기본은 항상 true.
	virtual bool CanCast(const Monster& /*owner*/, const Player& /*target*/, float /*now*/) const
	{
		return true;
	}

	// 시전 실행. 데미지 적용/패킷 브로드캐스트 등.
	// now 는 TimeManager.totalTime 기준 (PickCastable 과 동일 clock).
	virtual void Execute(const SkillTemplate& skill, Monster& owner, Player& target, float now) = 0;

	// 0 = 즉발. 향후 선딜 스킬이 필요해지면 이 값이 사용된다 (이번 리팩토링 범위 밖).
	virtual float GetCastTime() const { return 0.0f; }
};


// 현재 Monster::DoAttack 의 로직을 그대로 담는 기본 구현. 구현은 .cpp.
class DefaultAttackBehavior : public ISkillBehavior
{
public:
	void Execute(const SkillTemplate& skill, Monster& owner, Player& target, float now) override;
};
