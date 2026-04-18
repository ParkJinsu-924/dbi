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

void MonsterGlobalState::OnUpdate(Monster& owner, float deltaTime)
{
	switch (owner.GetStateId())
	{
	case MonsterStateId::Idle:
	case MonsterStateId::Patrol:
	{
		// 1순위: 이미 누적된 aggro 가 있으면 top 대상으로 Chase
		if (owner.HasAggro())
		{
			const long long topGuid = owner.ResolveTopAggroGuid();
			if (topGuid != 0)
			{
				owner.SetTarget(topGuid);
				owner.GetFSM().ChangeState(MonsterStateId::Chase);
				break;
			}
		}
		// 2순위 (기존): 거리 기반 탐지 — detect range 내 가장 가까운 플레이어 Chase
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
	case MonsterStateId::Attack:
	{
		// 전투 상태에서만 OOC 타이머 진행.
		// 5초간 새 aggro 이벤트(피격 등) 가 없으면 AggroTable 이 스스로 Clear 하고 true 반환
		// → 여기서 Return 으로 전이시켜 전투 종료. ReturnState::OnEnter 가 target/aggro cleanup.
		if (owner.TickAggroOOC(deltaTime))
		{
			owner.GetFSM().ChangeState(MonsterStateId::Return);
		}
	}
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
	// Phase 1: 매 틱 top aggro 재계산. 현재 target 과 달라졌으면 즉시 전환.
	// Phase 3 에서 transition 110% 규칙 도입 시 여기에 비교 로직 추가.
	const long long topGuid = owner.ResolveTopAggroGuid();
	if (topGuid != 0)
	{
		const auto cur = owner.GetTarget();
		if (cur == nullptr || cur->GetGuid() != topGuid)
			owner.SetTarget(topGuid);
	}

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
	// Leash 초과로 Return 진입 시 aggro 도 함께 초기화 (신규 전투 시작으로 간주)
	owner.ClearAggro();
	owner.Heal(owner.GetMaxHp());
	
	// TODO: Buff 시스템을 도입한 후, 무적 버프 추가 필요.
	
	if (auto zone = owner.GetZone())
	{
		Proto::S_UnitHp pkt;
		pkt.set_guid(owner.GetGuid());
		pkt.set_hp(owner.GetHp());
		pkt.set_max_hp(owner.GetMaxHp());
		zone->Broadcast(pkt);
	}
}

void ReturnState::OnUpdate(Monster& owner, float deltaTime)
{
	float dist = owner.DistanceTo(owner.GetSpawnPos());
	if (dist <= 1.0f)
	{
		owner.GetFSM().ChangeState(MonsterStateId::Idle);
		return;
	}

	owner.MoveToward(owner.GetSpawnPos(), deltaTime);
}
