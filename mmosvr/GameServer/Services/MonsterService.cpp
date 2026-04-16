#include "pch.h"
#include "MonsterService.h"
#include "../ZoneManager.h"
#include "../Zone.h"
#include "Packet/PacketHeader.h"
#include "Packet/PacketIdTraits.h"
#include "Network/SendBuffer.h"
#include "game.pb.h"


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
