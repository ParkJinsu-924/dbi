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
#include "PacketMaker.h"


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
		// 이미 누적된 aggro 가 있으면 top 대상으로 Engage 진입.
		if (owner.Get<AggroAgent>().HasAny())
		{
			const long long topGuid = owner.Get<AggroAgent>().GetTop();
			if (topGuid != 0)
			{
				owner.SetTarget(topGuid);
				owner.Get<FSMAgent>().ChangeState(MonsterStateId::Engage);
				break;
			}
		}

		{ // 가까운 Player 탐지 시 Aggro 세팅
			const auto player = owner.GetZone().FindNearestPlayer(
				owner.GetPosition(), owner.GetDetectRange());

			if (player)
			{
				owner.Get<AggroAgent>().Add(player->GetGuid(), 0); // Aggro 최소치 세팅
			}
		}
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

void IdleState::OnEnter(Monster& owner)
{
	owner.ClearTarget();
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
	// 1) 매 틱 top aggro 재계산, 현재 target 과 다르면 교체.
	const long long topGuid = owner.Get<AggroAgent>().GetTop();
	if (topGuid != 0)
	{
		const auto cur = owner.GetTarget();
		if (cur == nullptr || cur->GetGuid() != topGuid)
			owner.SetTarget(topGuid);
	}

	// 2) 종료 조건: 타겟 소실 / leash 초과 → Return.
	const auto target = owner.GetTarget();
	if (!target || !target->IsAlive() || owner.DistanceToSpawn() > owner.GetLeashRange())
	{
		owner.Get<FSMAgent>().ChangeState(MonsterStateId::Return);
		return;
	}

	// 3) 시전 가능한 스킬이 있으면 그 자리에서 시전 (캐스트 틱은 이동 생략).
	const float dist = owner.DistanceTo(target->GetPosition());
	const float now = GetTimeManager().GetTotalTime();

	if (const auto choice = owner.PickCastable(now, dist))
	{
		phase_ = Phase::Casting;
		choice->tmpl->behavior->Execute(*choice->tmpl, owner, *target, now);
		owner.Get<SkillCooldownAgent>().MarkUsed(choice->skillId, now + choice->appliedCooldown);
		return;
	}

	// 4) 시전 불가. basic 사거리 기준으로 접근 / 대기.
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
	owner.ClearTarget();
	// Leash 초과로 Return 진입 시 aggro 도 함께 초기화 (신규 전투 시작으로 간주)
	owner.Get<AggroAgent>().Clear();
	owner.Heal(owner.GetMaxHp());

	// TODO: Buff 시스템을 도입한 후, 무적 버프 추가 필요.

	owner.GetZone().Broadcast(PacketMaker::MakeUnitHp(owner));
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
