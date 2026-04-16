#include "pch.h"
#include "Monster.h"
#include "Zone.h"
#include "Player.h"
#include "Packet/PacketHeader.h"
#include "Packet/PacketIdTraits.h"
#include "Network/SendBuffer.h"
#include "game.pb.h"
#include <cmath>


namespace
{
	template<typename T>
	SendBufferChunkPtr MakeChunk(const T& msg)
	{
		constexpr uint16 packetId = static_cast<uint16>(PacketIdTraits<T>::Id);
		const int32 payloadSize = static_cast<int32>(msg.ByteSizeLong());
		const int32 totalSize = PACKET_HEADER_SIZE + payloadSize;

		auto chunk = std::make_shared<SendBufferChunk>(totalSize);

		PacketHeader header;
		header.size = static_cast<uint16>(totalSize);
		header.id = packetId;
		std::memcpy(chunk->Buffer(), &header, PACKET_HEADER_SIZE);
		msg.SerializeToArray(chunk->Buffer() + PACKET_HEADER_SIZE, payloadSize);
		chunk->SetSize(totalSize);
		return chunk;
	}
}


void Monster::InitAI(const Proto::Vector3& spawnPos, Zone* zone)
{
	spawnPos_ = spawnPos;
	position_ = spawnPos;
	zone_ = zone;
	state_ = MonsterState::Idle;
}

void Monster::Update(float deltaTime)
{
	if (!zone_)
		return;

	switch (state_)
	{
	case MonsterState::Idle:    UpdateIdle(deltaTime);    break;
	case MonsterState::Chase:   UpdateChase(deltaTime);   break;
	case MonsterState::Attack:  UpdateAttack(deltaTime);  break;
	case MonsterState::Return:  UpdateReturn(deltaTime);  break;
	}
}

// ---------------------------------------------------------------------------
// State handlers
// ---------------------------------------------------------------------------

void Monster::UpdateIdle(float /*deltaTime*/)
{
	auto player = zone_->FindNearestPlayer(position_, detectRange_);
	if (player)
	{
		targetGuid_ = player->GetGuid();
		ChangeState(MonsterState::Chase);
	}
}

void Monster::UpdateChase(float deltaTime)
{
	auto target = GetTarget();

	// Target lost or leash exceeded
	if (!target || !target->IsAlive() || DistanceToSpawn() > leashRange_)
	{
		targetGuid_ = 0;
		ChangeState(MonsterState::Return);
		return;
	}

	float dist = DistanceTo(target->GetPosition());

	// Close enough to attack
	if (dist <= attackRange_)
	{
		attackTimer_ = 0.0f;  // attack immediately on first contact
		ChangeState(MonsterState::Attack);
		return;
	}

	MoveToward(target->GetPosition(), deltaTime);
}

void Monster::UpdateAttack(float deltaTime)
{
	auto target = GetTarget();

	// Target lost or leash exceeded
	if (!target || !target->IsAlive() || DistanceToSpawn() > leashRange_)
	{
		targetGuid_ = 0;
		ChangeState(MonsterState::Return);
		return;
	}

	float dist = DistanceTo(target->GetPosition());

	// Target moved out of attack range — chase again
	if (dist > attackRange_)
	{
		ChangeState(MonsterState::Chase);
		return;
	}

	// Attack on cooldown
	attackTimer_ -= deltaTime;
	if (attackTimer_ <= 0.0f)
	{
		if (false)
		{
			target->TakeDamage(attackDamage_);

			// Notify clients
			BroadcastAttack(target->GetGuid(), attackDamage_);
		}

		// Send updated HP to the damaged player
		Proto::S_PlayerHp hpPkt;
		hpPkt.set_hp(target->GetHp());
		hpPkt.set_max_hp(target->GetMaxHp());
		target->Send(hpPkt);

		attackTimer_ = attackCooldown_;

		LOG_INFO("Monster [" + GetName() + "] attacks Player " +
			std::to_string(target->GetPlayerId()) +
			" for " + std::to_string(attackDamage_) + " dmg (HP: " +
			std::to_string(target->GetHp()) + "/" +
			std::to_string(target->GetMaxHp()) + ")");
	}
}

void Monster::UpdateReturn(float deltaTime)
{
	float dist = DistanceTo(spawnPos_);
	if (dist <= 1.0f)
	{
		position_ = spawnPos_;
		Heal(maxHp_);  // full HP on return
		ChangeState(MonsterState::Idle);
		return;
	}

	MoveToward(spawnPos_, deltaTime);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void Monster::ChangeState(MonsterState newState)
{
	if (state_ == newState)
		return;

	state_ = newState;
	BroadcastState();

	LOG_INFO("Monster [" + GetName() + "] -> " + [&]
		{
			switch (newState)
			{
			case MonsterState::Idle:    return "Idle";
			case MonsterState::Chase:   return "Chase";
			case MonsterState::Attack:  return "Attack";
			case MonsterState::Return:  return "Return";
			default:                    return "Unknown";
			}
		}());
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

void Monster::MoveToward(const Proto::Vector3& target, float deltaTime)
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

std::shared_ptr<Player> Monster::GetTarget() const
{
	if (targetGuid_ == 0)
		return nullptr;
	return zone_->FindAs<Player>(targetGuid_);
}

void Monster::BroadcastState()
{
	Proto::S_MonsterState pkt;
	pkt.set_guid(GetGuid());
	pkt.set_state(static_cast<uint32>(state_));
	pkt.set_target_guid(targetGuid_);
	zone_->Broadcast(MakeChunk(pkt));
}

void Monster::BroadcastAttack(long long targetGuid, int32 damage)
{
	Proto::S_MonsterAttack pkt;
	pkt.set_monster_guid(GetGuid());
	pkt.set_target_guid(targetGuid);
	pkt.set_damage(damage);
	zone_->Broadcast(MakeChunk(pkt));
}
