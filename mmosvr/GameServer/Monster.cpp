#include "pch.h"
#include "Monster.h"
#include "Zone.h"
#include "Player.h"
#include "ResourceManager.h"
#include "SkillTemplate.h"
#include "SkillRuntime.h"
#include "PacketMaker.h"
#include "game.pb.h"
#include <cmath>
#include "PacketMaker.h"


void Monster::InitAI(const Proto::Vector3& spawnPos, Zone* zone)
{
	spawnPos_ = spawnPos;
	position_ = spawnPos;
	zone_ = zone;

	// GlobalState: detect player in Idle/Patrol -> Chase
	fsm_.SetGlobalState<MonsterGlobalState>();

	// 상태 등록
	fsm_.AddState<IdleState>(MonsterStateId::Idle);
	fsm_.AddState<PatrolState>(MonsterStateId::Patrol);
	fsm_.AddState<ChaseState>(MonsterStateId::Chase);
	fsm_.AddState<AttackState>(MonsterStateId::Attack);
	fsm_.AddState<ReturnState>(MonsterStateId::Return);

	// 상태 전환 콜백 (로그 + 브로드캐스트)
	fsm_.SetOnStateChanged([this](MonsterStateId prev, MonsterStateId next)
		{
			BroadcastState(prev, next);
		});

	// 시작 (Idle 상태로 시작)
	fsm_.Start(*this, MonsterStateId::Idle);
}

void Monster::Update(const float deltaTime)
{
	TickBuffs(deltaTime);
	// Stunned 상태에서는 FSM 을 돌리지 않아 이동/공격을 모두 차단한다.
	// (버프 tick 은 이미 수행됐으므로 stun 자체는 시간이 지나며 풀린다)
	if (IsStunned())
		return;
	fsm_.Update(deltaTime);
}

// ---------------------------------------------------------------------------
// Aggro (delegated to AggroTable)
// ---------------------------------------------------------------------------

void Monster::AddAggro(const long long playerGuid, const float amount)
{
	aggro_.Add(playerGuid, amount);
}

long long Monster::GetTopAggroGuid() const
{
	return aggro_.ResolveTop();
}

bool Monster::HasAggro() const
{
	return !aggro_.Empty();
}

void Monster::ClearAggro()
{
	aggro_.Clear();
}

// ---------------------------------------------------------------------------
// Public utilities (상태 클래스에서 호출)
// ---------------------------------------------------------------------------

std::shared_ptr<Player> Monster::GetTarget() const
{
	if (targetGuid_ == 0 || !zone_)
		return nullptr;
	return zone_->FindAs<Player>(targetGuid_);
}

float Monster::DistanceTo(const Proto::Vector3& target) const
{
	float dx = target.x() - position_.x();
	float dz = target.z() - position_.z();
	return std::sqrt(dx * dx + dz * dz);
}

float Monster::DistanceToSpawn() const
{
	return DistanceTo(spawnPos_);
}

void Monster::MoveToward(const Proto::Vector3& target, const float deltaTime)
{
	// Rooted 상태면 이동하지 못함 (공격은 다른 state 책임이라 여기선 이동만 차단).
	if (IsRooted())
		return;

	float dx = target.x() - position_.x();
	float dz = target.z() - position_.z();
	float dist = std::sqrt(dx * dx + dz * dz);

	if (dist < 0.001f)
		return;

	float step = GetEffectiveMoveSpeed(moveSpeed_) * deltaTime;
	if (step > dist)
		step = dist;

	float nx = dx / dist;
	float nz = dz / dist;

	position_.set_x(position_.x() + nx * step);
	position_.set_z(position_.z() + nz * step);
}

const SkillTemplate* Monster::GetBasicSkill() const
{
	const auto* skTable = GetResourceManager().Get<SkillTemplate>();
	return skTable ? skTable->Find(basicSkillId_) : nullptr;
}

void Monster::DoAttack(Player& target)
{
	if (!zone_) return;
	const SkillTemplate* sk = GetBasicSkill();
	if (!sk)
	{
		LOG_WARN("Monster [" + GetName() + "] basicSkillId=" +
			std::to_string(basicSkillId_) + " not found in SkillTable");
		return;
	}
	SkillRuntime::Cast(*sk, *this, target, *zone_);
}

// ---------------------------------------------------------------------------
// Broadcasting
// ---------------------------------------------------------------------------

void Monster::BroadcastState(MonsterStateId /*prev*/, MonsterStateId next)
{
	if (!zone_)
		return;
	zone_->Broadcast(PacketMaker::MakeMonsterState(*this, next));
}
