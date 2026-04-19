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
	fsm_.Update(deltaTime);
}

// ---------------------------------------------------------------------------
// Aggro (delegated to AggroTable)
// ---------------------------------------------------------------------------

void Monster::AddAggro(const long long playerGuid, const float amount)
{
	aggro_.Add(playerGuid, amount);
}

long long Monster::ResolveTopAggroGuid() const
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

bool Monster::TickAggroOOC(const float deltaTime)
{
	return aggro_.TickOOC(deltaTime);
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
	float dx = target.x() - position_.x();
	float dz = target.z() - position_.z();
	float dist = std::sqrt(dx * dx + dz * dz);

	if (dist < 0.001f)
		return;

	float step = moveSpeed_ * deltaTime;
	if (step > dist)
		step = dist;

	float nx = dx / dist;
	float nz = dz / dist;

	position_.set_x(position_.x() + nx * step);
	position_.set_z(position_.z() + nz * step);
}

void Monster::DoAttack(Player& target)
{
	switch (attackType_)
	{
	case AttackType::Melee:
	case AttackType::Hitscan:
	{
		target.TakeDamage(attackDamage_);

		if (attackType_ == AttackType::Hitscan)
			zone_->Broadcast(PacketMaker::MakeHitscanAttack(*this, target, attackDamage_));
		else
			BroadcastAttack(target.GetGuid(), attackDamage_);

		zone_->Broadcast(PacketMaker::MakeUnitHp(target));

		LOG_INFO("Monster [" + GetName() + "] attacks Player " +
			std::to_string(target.GetPlayerId()) +
			" for " + std::to_string(attackDamage_) + " dmg (HP: " +
			std::to_string(target.GetHp()) + "/" +
			std::to_string(target.GetMaxHp()) + ")");
		break;
	}
	case AttackType::Homing:
	case AttackType::Skillshot:
	{
		if (!zone_) break;
		const auto* skTable = GetResourceManager().Get<SkillTemplate>();
		const SkillTemplate* sk = skTable ? skTable->Find(skillId_) : nullptr;
		if (!sk)
		{
			LOG_WARN("Monster [" + GetName() + "] has attackType=" +
				std::to_string(static_cast<int32>(attackType_)) + " but skillId=" +
				std::to_string(skillId_) + " not found in SkillTable");
			break;
		}

		// SkillEffect 에 OnHit Damage 가 정의돼 있으면 그 합을 사용, 없으면 attackDamage_ 로 fallback.
		const int32 effectDmg = SkillRuntime::ComputeOnHitDamage(sk->sid);
		const int32 fallback  = (effectDmg > 0) ? 0 : attackDamage_;

		if (attackType_ == AttackType::Homing)
		{
			SkillRuntime::CastHoming(
				GetGuid(), GameObjectType::Monster, GetPosition(),
				target.GetGuid(), *sk, *zone_, fallback);
		}
		else if (attackType_ == AttackType::Skillshot)
		{
			// 발사 시점의 타겟 방향으로 직진
			float dx = target.GetPosition().x() - GetPosition().x();
			float dz = target.GetPosition().z() - GetPosition().z();
			const float len = std::sqrt(dx * dx + dz * dz);
			if (len > 1e-4f) { dx /= len; dz /= len; }
			else { dx = 1.0f; dz = 0.0f; }

			SkillRuntime::CastSkillshot(
				GetGuid(), GameObjectType::Monster, GetPosition(),
				dx, dz, *sk, *zone_, fallback);
		}

		LOG_INFO("Monster [" + GetName() + "] launches " + sk->name +
			" -> Player " + std::to_string(target.GetPlayerId()));
		break;
	}
	default:
		LOG_WARN("Monster [" + GetName() + "] unknown attackType=" +
			std::to_string(static_cast<int32>(attackType_)));
		break;
	}
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

void Monster::BroadcastAttack(long long targetGuid, int32 damage)
{
	if (!zone_)
		return;
	zone_->Broadcast(PacketMaker::MakeMonsterAttack(*this, targetGuid, damage));
}
