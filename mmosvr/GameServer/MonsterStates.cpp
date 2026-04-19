#include "pch.h"
#include "MonsterStates.h"
#include "Monster.h"
#include "Zone.h"
#include "Player.h"
#include "SkillTemplate.h"
#include <cmath>
#include <random>
#include "PacketMaker.h"


// ===========================================================================
// GlobalDetectState — runs every tick before current state
// ===========================================================================

void MonsterGlobalState::OnUpdate(Monster& owner, float deltaTime)
{
	switch (owner.GetStateId())
	{
	case MonsterStateId::Idle:
	case MonsterStateId::Patrol:
	{
		// 이미 누적된 aggro 가 있으면 top 대상으로 Chase
		if (owner.HasAggro())
		{
			const long long topGuid = owner.GetTopAggroGuid();
			if (topGuid != 0)
			{
				owner.SetTarget(topGuid);
				owner.GetFSM().ChangeState(MonsterStateId::Chase);
				break;
			}
		}
		
		{ // 가까운 Player 탐지 시 Aggro 세팅
			const auto player = owner.GetZone()->FindNearestPlayer(
				owner.GetPosition(), owner.GetDetectRange());

			if (player)
			{
				owner.AddAggro(player->GetGuid(), 0); // Aggro 최소치 세팅
			}
		}
	}
	break;
	case MonsterStateId::Chase:
	case MonsterStateId::Attack:
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
		owner.GetFSM().ChangeState(MonsterStateId::Patrol);
		return;
	}
}

void IdleState::OnExit(Monster& owner)
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
	targetZ_ = spawn.z() + radius * std::sin(angle);
}

void PatrolState::OnUpdate(Monster& owner, const float deltaTime)
{
	Proto::Vector3 target;
	target.set_x(targetX_);
	target.set_y(owner.GetSpawnPos().y());
	target.set_z(targetZ_);

	if (owner.DistanceTo(target) <= 0.3f)
	{
		owner.GetFSM().ChangeState(MonsterStateId::Idle);
		return;
	}

	owner.MoveToward(target, deltaTime);
}


// ===========================================================================
// Chase
// ===========================================================================

void ChaseState::OnUpdate(Monster& owner, const float deltaTime)
{
	// 매 틱 top aggro 재계산. 현재 target 과 달라졌으면 즉시 전환.
	const long long topGuid = owner.GetTopAggroGuid();
	if (topGuid != 0)
	{
		const auto cur = owner.GetTarget();
		if (cur == nullptr || cur->GetGuid() != topGuid)
			owner.SetTarget(topGuid);
	}

	const auto target = owner.GetTarget();

	if (!target || !target->IsAlive() || owner.DistanceToSpawn() > owner.GetLeashRange())
	{
		owner.GetFSM().ChangeState(MonsterStateId::Return);
		return;
	}

	const float dist = owner.DistanceTo(target->GetPosition());
	const auto* sk = owner.GetBasicSkill();
	const float attackRange = sk ? sk->cast_range : 2.0f;

	if (dist <= attackRange)
	{
		owner.GetFSM().ChangeState(MonsterStateId::Attack);
		return;
	}

	owner.MoveToward(target->GetPosition(), deltaTime);
}


// ===========================================================================
// Attack
// ===========================================================================

void AttackState::OnUpdate(Monster& owner, const float /*deltaTime*/)
{
	auto target = owner.GetTarget();

	if (!target || !target->IsAlive() || owner.DistanceToSpawn() > owner.GetLeashRange())
	{
		owner.GetFSM().ChangeState(MonsterStateId::Return);
		return;
	}

	const float dist = owner.DistanceTo(target->GetPosition());
	const auto* sk = owner.GetBasicSkill();
	const float attackRange = sk ? sk->cast_range : 2.0f;

	if (dist > attackRange)
	{
		owner.GetFSM().ChangeState(MonsterStateId::Chase);
		return;
	}

	const float cooldown = sk ? sk->cooldown : 1.5f;
	const float now = GetTimeManager().GetTotalTime();
	if (now - owner.GetLastAttackTime() >= cooldown)
	{
		owner.DoAttack(*target);
		owner.SetLastAttackTime(now);
	}
}


// ===========================================================================
// Return
// ===========================================================================

void ReturnState::OnEnter(Monster& owner)
{
	owner.ClearTarget();
	// Leash 초과로 Return 진입 시 aggro 도 함께 초기화 (신규 전투 시작으로 간주)
	owner.ClearAggro();
	owner.Heal(owner.GetMaxHp());
	
	// TODO: Buff 시스템을 도입한 후, 무적 버프 추가 필요.
	
	if (const auto zone = owner.GetZone())
	{
		zone->Broadcast(PacketMaker::MakeUnitHp(owner));
	}
}

void ReturnState::OnUpdate(Monster& owner, const float deltaTime)
{
	float dist = owner.DistanceTo(owner.GetSpawnPos());
	if (dist <= 1.0f)
	{
		owner.GetFSM().ChangeState(MonsterStateId::Idle);
		return;
	}

	owner.MoveToward(owner.GetSpawnPos(), deltaTime);
}
