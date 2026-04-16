#include "pch.h"
#include "MonsterStates.h"
#include "Monster.h"
#include "Zone.h"
#include "Player.h"


// ===========================================================================
// Idle
// ===========================================================================

void IdleState::OnEnter(Monster& owner)
{
	owner.ClearTarget();
}

void IdleState::OnUpdate(Monster& owner, float /*deltaTime*/)
{
	auto player = owner.GetZone()->FindNearestPlayer(
		owner.GetPosition(), owner.GetDetectRange());

	if (player)
	{
		owner.SetTarget(player->GetGuid());
		owner.GetFSM().ChangeState(owner, MonsterStateId::Chase);
	}
}


// ===========================================================================
// Chase
// ===========================================================================

void ChaseState::OnUpdate(Monster& owner, float deltaTime)
{
	auto target = owner.GetTarget();

	if (!target || !target->IsAlive() || owner.DistanceToSpawn() > owner.GetLeashRange())
	{
		owner.GetFSM().ChangeState(owner, MonsterStateId::Return);
		return;
	}

	float dist = owner.DistanceTo(target->GetPosition());

	if (dist <= owner.GetAttackRange())
	{
		owner.GetFSM().ChangeState(owner, MonsterStateId::Attack);
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
		owner.GetFSM().ChangeState(owner, MonsterStateId::Return);
		return;
	}

	float dist = owner.DistanceTo(target->GetPosition());

	if (dist > owner.GetAttackRange())
	{
		owner.GetFSM().ChangeState(owner, MonsterStateId::Chase);
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
		owner.GetFSM().ChangeState(owner, MonsterStateId::Idle);
		return;
	}

	owner.MoveToward(owner.GetSpawnPos(), deltaTime);
}
