#include "pch.h"
#include "MonsterStates.h"
#include "Monster.h"
#include "Zone.h"
#include "Player.h"
#include <cmath>


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
// Patrol — circular patrol around spawn point
// ===========================================================================

void PatrolState::OnEnter(Monster& /*owner*/)
{
	angle_ = 0.0f;
	patrolTime_ = 0.0f;
}

void PatrolState::OnUpdate(Monster& owner, const float deltaTime)
{
	// Detection is handled by GlobalDetectState
	
	patrolTime_ += deltaTime;
	if (patrolTime_ >= 1.0f)
	{
		patrolTime_ = 0.0f;
		owner.GetFSM().ChangeState(MonsterStateId::Idle);
		return;
	}

	constexpr float PATROL_RADIUS = 3.0f;
	constexpr float PATROL_SPEED  = 0.8f;   // radians per second

	angle_ += PATROL_SPEED * deltaTime;
	if (angle_ > 6.28318530718f)
		angle_ -= 6.28318530718f;

	const auto& spawn = owner.GetSpawnPos();
	Proto::Vector3 target;
	target.set_x(spawn.x() + PATROL_RADIUS * std::cos(angle_));
	target.set_y(spawn.y());
	target.set_z(spawn.z() + PATROL_RADIUS * std::sin(angle_));

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

void AttackState::OnEnter(Monster& /*owner*/)
{
	attackTimer_ = 0.0f;   // 진입 즉시 첫 공격
}

void AttackState::OnUpdate(Monster& owner, const float deltaTime)
{
	auto target = owner.GetTarget();

	if (!target || !target->IsAlive() || owner.DistanceToSpawn() > owner.GetLeashRange())
	{
		owner.GetFSM().ChangeState(MonsterStateId::Return);
		return;
	}

	float dist = owner.DistanceTo(target->GetPosition());

	if (dist > owner.GetAttackRange())
	{
		owner.GetFSM().ChangeState(MonsterStateId::Chase);
		return;
	}

	attackTimer_ -= deltaTime;
	if (attackTimer_ <= 0.0f)
	{
		owner.DoAttack(*target);
		attackTimer_ = owner.GetAttackCooldown();
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
