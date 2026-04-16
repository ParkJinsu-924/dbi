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

	const char* StateIdToString(MonsterStateId id)
	{
		switch (id)
		{
		case MonsterStateId::Idle:    return "Idle";
		case MonsterStateId::Chase:   return "Chase";
		case MonsterStateId::Attack:  return "Attack";
		case MonsterStateId::Return:  return "Return";
		default:                      return "Unknown";
		}
	}
}


void Monster::InitAI(const Proto::Vector3& spawnPos, Zone* zone)
{
	spawnPos_ = spawnPos;
	position_ = spawnPos;
	zone_ = zone;

	// 상태 등록
	fsm_.AddState<IdleState>(MonsterStateId::Idle);
	fsm_.AddState<ChaseState>(MonsterStateId::Chase);
	fsm_.AddState<AttackState>(MonsterStateId::Attack);
	fsm_.AddState<ReturnState>(MonsterStateId::Return);

	// 상태 전환 콜백 (로그 + 브로드캐스트)
	fsm_.SetOnStateChanged([this](MonsterStateId prev, MonsterStateId next)
		{
			BroadcastState(prev, next);
			LOG_INFO("Monster [" + GetName() + "] " +
				StateIdToString(prev) + " -> " + StateIdToString(next));
		});

	// 시작
	fsm_.Start(*this, MonsterStateId::Idle);
}

void Monster::Update(const float deltaTime)
{
	fsm_.Update(*this, deltaTime);
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

void Monster::DoAttack(Player& target)
{
	if (false)  // damage temporarily disabled
	{
		target.TakeDamage(attackDamage_);
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
	zone_->Broadcast(MakeChunk(pkt));
}

void Monster::BroadcastAttack(long long targetGuid, int32 damage)
{
	if (!zone_)
		return;

	Proto::S_MonsterAttack pkt;
	pkt.set_monster_guid(GetGuid());
	pkt.set_target_guid(targetGuid);
	pkt.set_damage(damage);
	zone_->Broadcast(MakeChunk(pkt));
}
