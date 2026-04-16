#include "pch.h"
#include "Monster.h"
#include "Zone.h"
#include "Player.h"
#include "game.pb.h"
#include <cmath>


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
			LOG_INFO("Monster [" + GetName() + "] " +
				EU::EnumToString(prev) + " -> " + EU::EnumToString(next));
		});

	// 시작 (Patrol 상태로 시작)
	fsm_.Start(*this, MonsterStateId::Patrol);
}

void Monster::Update(const float deltaTime)
{
	fsm_.Update(deltaTime);
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
	target.TakeDamage(attackDamage_);

	if (attackType_ == 1)  // Hitscan
	{
		Proto::S_HitscanAttack pkt;
		pkt.set_attacker_guid(GetGuid());
		pkt.set_target_guid(target.GetGuid());
		*pkt.mutable_start_position() = GetPosition();
		*pkt.mutable_hit_position() = target.GetPosition();
		pkt.set_damage(attackDamage_);
		zone_->Broadcast(pkt);
	}
	else  // Melee
	{
		BroadcastAttack(target.GetGuid(), attackDamage_);
	}

	Proto::S_PlayerHp hpPkt;
	hpPkt.set_hp(target.GetHp());
	hpPkt.set_max_hp(target.GetMaxHp());
	target.Send(hpPkt);

	LOG_INFO("Monster [" + GetName() + "] attacks Player " +
		std::to_string(target.GetPlayerId()) +
		" for " + std::to_string(attackDamage_) + " dmg (HP: " +
		std::to_string(target.GetHp()) + "/" +
		std::to_string(target.GetMaxHp()) + ")");
}

// ---------------------------------------------------------------------------
// Broadcasting
// ---------------------------------------------------------------------------

void Monster::BroadcastState(MonsterStateId /*prev*/, MonsterStateId next)
{
	if (!zone_)
		return;

	Proto::S_MonsterState pkt;
	pkt.set_guid(GetGuid());
	pkt.set_state(static_cast<uint32>(next));
	pkt.set_target_guid(targetGuid_);
	zone_->Broadcast(pkt);
}

void Monster::BroadcastAttack(long long targetGuid, int32 damage)
{
	if (!zone_)
		return;

	Proto::S_MonsterAttack pkt;
	pkt.set_monster_guid(GetGuid());
	pkt.set_target_guid(targetGuid);
	pkt.set_damage(damage);
	zone_->Broadcast(pkt);
}
