#include "pch.h"
#include "Zone.h"
#include "GameObject.h"
#include "Player.h"
#include "Network/Session.h"
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

	constexpr float MONSTER_BROADCAST_INTERVAL = 0.1f;  // 10 Hz
}


void Zone::Add(std::shared_ptr<GameObject> obj)
{
	long long guid = obj->GetGuid();
	objects_.Write([&](auto& m)
		{
			m[guid] = std::move(obj);
		});
}

void Zone::Remove(long long guid)
{
	objects_.Write([&](auto& m)
		{
			m.erase(guid);
		});
}

std::shared_ptr<GameObject> Zone::Find(long long guid) const
{
	return objects_.Read([&](const auto& m) -> std::shared_ptr<GameObject>
		{
			auto it = m.find(guid);
			if (it == m.end())
				return nullptr;
			return it->second;
		});
}

void Zone::Update(float deltaTime)
{
	// Tick all objects
	objects_.Read([&](const auto& m)
		{
			for (const auto& [guid, obj] : m)
				obj->Update(deltaTime);
		});

	// Broadcast monster positions periodically
	monsterBroadcastAccum_ += deltaTime;
	if (monsterBroadcastAccum_ >= MONSTER_BROADCAST_INTERVAL)
	{
		monsterBroadcastAccum_ = 0.0f;
		BroadcastMonsterPositions();
	}
}

void Zone::Broadcast(SendBufferChunkPtr chunk)
{
	objects_.Read([&](const auto& m)
		{
			for (const auto& [guid, obj] : m)
			{
				if (obj->GetType() != GameObjectType::Player)
					continue;

				auto player = std::static_pointer_cast<Player>(obj);
				if (auto session = player->GetSession())
				{
					if (session->IsConnected())
						std::static_pointer_cast<Session>(session)->Send(chunk);
				}
			}
		});
}

void Zone::BroadcastMonsterPositions()
{
	objects_.Read([&](const auto& m)
		{
			for (const auto& [guid, obj] : m)
			{
				if (obj->GetType() != GameObjectType::Monster)
					continue;

				Proto::S_MonsterMove pkt;
				pkt.set_guid(guid);
				*pkt.mutable_position() = obj->GetPosition();
				Broadcast(MakeChunk(pkt));
			}
		});
}
