#include "pch.h"
#include "MonsterStates.h"
#include "Monster.h"
#include "Agent/SkillCooldownAgent.h"
#include "Agent/FSMAgent.h"
#include "Agent/AggroAgent.h"
#include "Zone.h"
#include "Player.h"
#include "SkillTemplate.h"
#include "SkillBehavior.h"
#include <cmath>
#include <random>


// ===========================================================================
// GlobalDetectState — runs every tick before current state
// ===========================================================================

void MonsterGlobalState::OnUpdate(Monster& owner, float /*deltaTime*/)
{
	switch (owner.Get<FSMAgent>().GetCurrentStateId())
	{
	case MonsterStateId::Idle:
	case MonsterStateId::Patrol:
	{
		// Aggro 엔트리가 있으면 그 top 을 타겟으로 Engage 진입 (타겟 조회는 GetTarget 이 담당).
		if (owner.Get<AggroAgent>().HasAny())
		{
			owner.Get<FSMAgent>().ChangeState(MonsterStateId::Engage);
			break;
		}

		// 가까운 Player 탐지 시 Aggro 최소치 세팅 (값 0 — 실제 교전 전 감지 표식)
		const auto player = owner.GetZone().FindNearest<Player>(
			owner.GetPosition(), owner.GetDetectRange());
		if (player)
			owner.Get<AggroAgent>().Add(player->GetGuid(), 0);
	}
	break;
	case MonsterStateId::Engage:
	case MonsterStateId::Return:
	default:
		break;
	}
}


// ===========================================================================
// Idle
// ===========================================================================

void IdleState::OnEnter(Monster& /*owner*/)
{
	// target 은 AggroAgent 가 유일 소스 — 별도 초기화 불필요.
	// aggro 가 남아있으면 다음 틱에 GlobalState 가 다시 Engage 로 전환한다.
	idleTime_ = 0.0f;
}

void IdleState::OnUpdate(Monster& owner, const float deltaTime)
{
	idleTime_ += deltaTime;
	if (idleTime_ >= 4.0f)
	{
		owner.Get<FSMAgent>().ChangeState(MonsterStateId::Patrol);
		return;
	}
}

void IdleState::OnExit(Monster& /*owner*/)
{
	idleTime_ = 0.0f;
}


// ===========================================================================
// Patrol — move to a random point within range of spawn, then go Idle
// ===========================================================================

void PatrolState::OnEnter(Monster& owner)
{
	constexpr float PATROL_RANGE = 5.0f;

	static thread_local std::mt19937 rng(std::random_device{}());
	std::uniform_real_distribution<float> angleDist(0.0f, 6.28318530718f);
	std::uniform_real_distribution<float> radiusDist(0.0f, PATROL_RANGE);

	const float angle = angleDist(rng);
	const float radius = radiusDist(rng);
	const auto& spawn = owner.GetSpawnPos();

	targetX_ = spawn.x() + radius * std::cos(angle);
	targetZ_ = spawn.y() + radius * std::sin(angle);
}

void PatrolState::OnUpdate(Monster& owner, const float deltaTime)
{
	Proto::Vector2 target;
	target.set_x(targetX_);
	target.set_y(targetZ_);

	if (owner.DistanceTo(target) <= 0.3f)
	{
		owner.Get<FSMAgent>().ChangeState(MonsterStateId::Idle);
		return;
	}

	owner.MoveToward(target, deltaTime);
}


// ===========================================================================
// Engage — 추격 + 시전 + 대기 통합. Chase/Attack 은 더 이상 없다.
// ===========================================================================

void EngageState::OnEnter(Monster& /*owner*/)
{
	phase_ = Phase::Approach;
}

void EngageState::OnUpdate(Monster& owner, const float deltaTime)
{
	// 1) 종료 조건: 타겟 소실 / leash 초과 → Return.
	//    GetTarget() 은 매 호출 시점에 AggroAgent.GetTop() 결과를 반영 (단일 소스).
	const auto target = owner.GetTarget();
	if (!target || !target->IsAlive() || owner.DistanceToSpawn() > owner.GetLeashRange())
	{
		owner.Get<FSMAgent>().ChangeState(MonsterStateId::Return);
		return;
	}

	// 2) 시전 가능한 스킬이 있으면 그 자리에서 시전 (캐스트 틱은 이동 생략).
	const float dist = owner.DistanceTo(target->GetPosition());
	const float now = GetTimeManager().GetTotalTime();

	if (const auto choice = owner.PickCastable(now, dist))
	{
		phase_ = Phase::Casting;
		choice->tmpl->behavior->Execute(*choice->tmpl, owner, *target, now);
		owner.Get<SkillCooldownAgent>().MarkUsed(choice->skillId, now + choice->appliedCooldown);
		return;
	}

	// 3) 시전 불가. basic 사거리 기준으로 접근 / 대기.
	const float engageRange = owner.GetBasicSkillRange();
	if (engageRange <= 0.0f || dist > engageRange)
	{
		phase_ = Phase::Approach;
		owner.MoveToward(target->GetPosition(), deltaTime);
	}
	else
	{
		phase_ = Phase::Waiting;
		// 제자리 대기 — 쿨다운 회복을 기다린다.
	}
}


// ===========================================================================
// Return
// ===========================================================================

void ReturnState::OnEnter(Monster& owner)
{
	// Aggro.Clear() 가 곧 "target 없음" — 단일 진실 원천.
	// Leash 초과로 Return 진입 시 신규 전투로 간주하고 초기화.
	owner.Get<AggroAgent>().Clear();
	// Heal 내부에서 HP 변화가 있으면 S_UnitHp 자동 방송.
	owner.Heal(owner.GetMaxHp());

	// TODO: Buff 시스템을 도입한 후, 무적 버프 추가 필요.
}

void ReturnState::OnUpdate(Monster& owner, const float deltaTime)
{
	float dist = owner.DistanceTo(owner.GetSpawnPos());
	if (dist <= 1.0f)
	{
		owner.Get<FSMAgent>().ChangeState(MonsterStateId::Idle);
		return;
	}

	owner.MoveToward(owner.GetSpawnPos(), deltaTime);
}
