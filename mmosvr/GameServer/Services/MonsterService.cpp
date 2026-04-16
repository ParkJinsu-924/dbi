#include "pch.h"
#include "MonsterService.h"
#include "../ZoneManager.h"
#include "../Zone.h"
#include "Network/PacketSession.h"
#include "Network/Session.h"
#include "game.pb.h"

// We need MakeSendBuffer<T> which is defined on PacketSession (template).
// Zone::Broadcast takes SendBufferChunkPtr, so we build it once and reuse.
// Easiest route: use PacketSession's static MakeSendBufferRaw via a session?
// Actually it's private. Use PacketSession::MakeSendBuffer<T>. Since
// MonsterService isn't a session, we need an alternative.
//
// Solution: include the helper via a light utility.
// PacketSession::MakeSendBuffer is a non-static template. We replicate the
// serialization logic inline here (it's tiny).
//
// Packet layout: [uint16 size][uint32 id][protobuf payload]
#include "Packet/PacketHeader.h"
#include "Packet/PacketIdTraits.h"
#include "Network/SendBuffer.h"


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


void MonsterService::Init()
{
	LOG_INFO("MonsterService initialized");
}

void MonsterService::Shutdown()
{
	monsters_.Write([](auto& m) { m.clear(); });
	LOG_INFO("MonsterService shutdown");
}

void MonsterService::Update(float deltaTime)
{
	// Update all monster positions
	monsters_.Read([&](const auto& m)
		{
			for (const auto& [guid, monster] : m)
				monster->UpdateMovement(deltaTime);
		});

	// Throttle broadcast to ~10 Hz
	constexpr float BROADCAST_INTERVAL = 0.1f;
	broadcastAccumulator_ += deltaTime;
	if (broadcastAccumulator_ < BROADCAST_INTERVAL)
		return;
	broadcastAccumulator_ = 0.0f;

	// Collect snapshot (read lock short)
	std::vector<std::shared_ptr<Monster>> snapshot;
	monsters_.Read([&](const auto& m)
		{
			snapshot.reserve(m.size());
			for (const auto& [guid, monster] : m)
				snapshot.push_back(monster);
		});

	// Broadcast each monster's new position to its zone
	for (const auto& monster : snapshot)
	{
		Proto::S_MonsterMove pkt;
		pkt.set_guid(monster->GetGuid());
		*pkt.mutable_position() = monster->GetPosition();

		auto chunk = MakeChunk(pkt);

		if (auto* zone = GetZoneManager().GetZone(monster->GetZoneId()))
			zone->Broadcast(chunk);
	}
}

std::shared_ptr<Monster> MonsterService::Spawn(int32 zoneId, const std::string& name,
	const Proto::Vector3& center, float radius, float angularSpeedRad, float startAngleRad)
{
	auto monster = std::make_shared<Monster>(name);
	monster->SetZoneId(zoneId);
	monster->InitCircularMovement(center, radius, angularSpeedRad, startAngleRad);

	monsters_.Write([&](auto& m)
		{
			m[monster->GetGuid()] = monster;
		});

	if (auto* zone = GetZoneManager().GetZone(zoneId))
	{
		zone->Add(monster);

		// Broadcast spawn to existing players in the zone
		Proto::S_MonsterSpawn pkt;
		pkt.set_guid(monster->GetGuid());
		pkt.set_name(monster->GetName());
		*pkt.mutable_position() = monster->GetPosition();
		zone->Broadcast(MakeChunk(pkt));
	}

	LOG_INFO("Monster spawned: guid=" + std::to_string(monster->GetGuid())
		+ " name=" + name + " zone=" + std::to_string(zoneId));
	return monster;
}

void MonsterService::Despawn(long long guid)
{
	std::shared_ptr<Monster> monster;
	monsters_.Write([&](auto& m)
		{
			auto it = m.find(guid);
			if (it == m.end()) return;
			monster = it->second;
			m.erase(it);
		});

	if (!monster) return;

	if (auto* zone = GetZoneManager().GetZone(monster->GetZoneId()))
	{
		zone->Remove(guid);

		Proto::S_MonsterDespawn pkt;
		pkt.set_guid(guid);
		zone->Broadcast(MakeChunk(pkt));
	}

	LOG_INFO("Monster despawned: guid=" + std::to_string(guid));
}

std::shared_ptr<Monster> MonsterService::Find(long long guid) const
{
	return monsters_.Read([&](const auto& m) -> std::shared_ptr<Monster>
		{
			auto it = m.find(guid);
			if (it == m.end()) return nullptr;
			return it->second;
		});
}

std::vector<std::shared_ptr<Monster>> MonsterService::GetMonstersInZone(int32 zoneId) const
{
	return monsters_.Read([&](const auto& m)
		{
			std::vector<std::shared_ptr<Monster>> result;
			for (const auto& [guid, monster] : m)
			{
				if (monster->GetZoneId() == zoneId)
					result.push_back(monster);
			}
			return result;
		});
}
