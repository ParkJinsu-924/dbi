#include "pch.h"
#include "Monster.h"
#include "Zone.h"
#include "Player.h"
#include "ResourceManager.h"
#include "SkillTemplate.h"
#include "SkillRuntime.h"
#include "PacketMaker.h"
#include "Utils/MathUtil.h"
#include "game.pb.h"
#include <cmath>


void Monster::InitAI(const Proto::Vector2& spawnPos, Zone* zone)
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
	// 총체적 행동 불가 상태(현재는 Stun) 에서는 FSM 자체를 스킵.
	// (TickBuffs 는 먼저 수행돼 stun duration 이 시간이 지나며 풀린다)
	if (!CanAct())
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

float Monster::DistanceToSpawn() const
{
	return DistanceTo(spawnPos_);   // GameObject::DistanceTo 재사용
}

void Monster::MoveToward(const Proto::Vector2& target, const float deltaTime)
{
	// 이동 불가 CC(Stun/Root) 차단. 공격 가능 여부는 DoAttack 에서 별도 판정.
	if (!CanMove())
		return;

	const float dx = target.x() - position_.x();
	const float dz = target.y() - position_.y();
	const float dist = MathUtil::Length2D(dx, dz);

	if (dist < 0.001f)
		return;

	float step = GetEffectiveMoveSpeed(moveSpeed_) * deltaTime;
	if (step > dist)
		step = dist;

	position_.set_x(position_.x() + (dx / dist) * step);
	position_.set_y(position_.y() + (dz / dist) * step);
}

const SkillTemplate* Monster::GetBasicSkill() const
{
	const auto* skTable = GetResourceManager().Get<SkillTemplate>();
	return skTable ? skTable->Find(basicSkillId_) : nullptr;
}

void Monster::DoAttack(Player& target)
{
	if (!zone_) return;
	// Monster 평타는 "기본 공격" 성격이라 Silence 는 면역 (LoL 관습). Stun 만 차단.
	if (!CanAttack()) return;
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
