#include "pch.h"
#include "MonsterStates.h"
#include "Monster.h"
#include "Zone.h"
#include "Player.h"
#include <cmath>
#include <random>


// ===========================================================================
// GlobalDetectState — runs every tick before current state
// ===========================================================================

void MonsterGlobalState::OnUpdate(Monster& owner, float /*deltaTime*/)
{
	switch (owner.GetStateId())
	{
	case MonsterStateId::Idle:
	case MonsterStateId::Patrol:
	{
		const auto player = owner.GetZone()->FindNearestPlayer(
			owner.GetPosition(), owner.GetDetectRange());

		if (player)
		{
			owner.SetTarget(player->GetGuid());
			owner.GetFSM().ChangeState(MonsterStateId::Chase);
		}
	}
	break;
	case MonsterStateId::Chase:
		break;
	case MonsterStateId::Attack:
		break;
	case MonsterStateId::Return:
		break;
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
	if (idleTime_ >= 1.0f)
	{
		idleTime_ = 0.0f;
		owner.GetFSM().ChangeState(MonsterStateId::Patrol);
		return;
	}
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

void ChaseState::OnUpdate(Monster& owner, float deltaTime)
{
	auto target = owner.GetTarget();

	if (!target || !target->IsAlive() || owner.DistanceToSpawn() > owner.GetLeashRange())
	{
		owner.GetFSM().ChangeState(MonsterStateId::Return);
		return;
	}

	float dist = owner.DistanceTo(target->GetPosition());

	if (dist <= owner.GetAttackRange())
	{
		owner.GetFSM().ChangeState(MonsterStateId::Attack);
		return;
	}

	owner.MoveToward(target->GetPosition(), deltaTime);
}


// ===========================================================================
// Attack
// ===========================================================================

void AttackState::OnUpdate(Monster& owner, const float deltaTime)
{
	auto target = owner.GetTarget();

	if (!target || !target->IsAlive() || owner.DistanceToSpawn() > owner.GetLeashRange())
	{
		owner.GetFSM().ChangeState(MonsterStateId::Return);
		return;
	}

	const float dist = owner.DistanceTo(target->GetPosition());

	if (dist > owner.GetAttackRange())
	{
		owner.GetFSM().ChangeState(MonsterStateId::Chase);
		return;
	}

	const float now = GetTimeManager().GetTotalTime();
	if (now - owner.GetLastAttackTime() >= owner.GetAttackCooldown())
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
}

void ReturnState::OnUpdate(Monster& owner, float deltaTime)
{
	float dist = owner.DistanceTo(owner.GetSpawnPos());
	if (dist <= 1.0f)
	{
		owner.SetPosition(owner.GetSpawnPos());
		owner.Heal(owner.GetMaxHp());
		owner.GetFSM().ChangeState(MonsterStateId::Patrol);
		return;
	}

	owner.MoveToward(owner.GetSpawnPos(), deltaTime);
}
